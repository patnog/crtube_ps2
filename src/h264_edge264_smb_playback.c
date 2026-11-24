#include "h264_edge264_smb_playback.h"

#include "h264_edge264.h"
#include "../third_party/edge264/ps2/edge264_r5900_mmi.h"
#include "h264_mp4.h"
#include "h264_mp4_smb.h"
#include "screen.h"
#include "debug_log.h"
#include "edge264.h"
#include "edge264_test_mp4.h"

#if defined(CRTUBE_R5900_DECODER_TEST_ONLY)
extern void crtube_vu_inter_luma_set_enabled(int enabled);
extern void crtube_vu_inter_luma_reset_stats(void);
extern void crtube_vu_inter_luma_log_stats(void);
#endif

#include <delaythread.h>
#include <malloc.h>
#include <timer.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define CRTUBE_BASELINE_MP4_CAPACITY (2u * 1024u * 1024u)
#define CRTUBE_EDGE264_MEMORY_LIMIT (12u * 1024u * 1024u)
#ifndef CRTUBE_H264_TEST_MAX_SAMPLES
#define CRTUBE_H264_TEST_MAX_SAMPLES 120u
#endif
#ifndef CRTUBE_H264_PLANE_CRC_EVERY_FRAME
#define CRTUBE_H264_PLANE_CRC_EVERY_FRAME 1
#endif

/* CRC-32/ISO-HDLC over visible plane bytes only. Padding in decoder strides
   is deliberately excluded so host and EE results are directly comparable. */
static uint32_t plane_crc32(const uint8_t *plane, unsigned width,
                             unsigned height, unsigned stride)
{
    static const uint32_t crc_table[16] = {
        0x00000000u, 0x1db71064u, 0x3b6e20c8u, 0x26d930acu,
        0x76dc4190u, 0x6b6b51f4u, 0x4db26158u, 0x5005713cu,
        0xedb88320u, 0xf00f9344u, 0xd6d6a3e8u, 0xcb61b38cu,
        0x9b64c2b0u, 0x86d3d2d4u, 0xa00ae278u, 0xbdbdf21cu
    };
    uint32_t crc = 0xffffffffu;
    unsigned row, col;

    for (row = 0; row < height; ++row) {
        const uint8_t *line = plane + (size_t)row * stride;
        for (col = 0; col < width; ++col) {
            crc ^= line[col];
            crc = (crc >> 4) ^ crc_table[crc & 0x0fu];
            crc = (crc >> 4) ^ crc_table[crc & 0x0fu];
        }
    }
    return crc ^ 0xffffffffu;
}

typedef struct h264_plane_reference {
    unsigned frame_number;
    uint32_t y_crc, cb_crc, cr_crc;
} h264_plane_reference_t;

/* FFmpeg yuv420p CRC-32/ISO-HDLC references for the embedded 180-frame
   mc_coverage.mp4 fixture. Frame numbers are zero-based, matching playback. */
static const h264_plane_reference_t embedded_plane_references[] = {
    {   0u, 0xb3c38190u, 0x38d0f1d6u, 0xad485129u },
    {   1u, 0xc86f3621u, 0x6d7429c1u, 0x0a61e012u },
    {   2u, 0x95507ae4u, 0xc21a8351u, 0x52d3a889u },
    {  29u, 0x12f64232u, 0x13c73017u, 0x68a72e7fu },
    {  59u, 0xc21a9e76u, 0x0f7bf41fu, 0x4cbf3a80u },
    {  89u, 0xd7235850u, 0x54f2c776u, 0xf677fa8eu },
    { 119u, 0x8772b0c7u, 0xa123d3b9u, 0x4a2de49bu },
    { 149u, 0xe08c4f31u, 0xdad3266au, 0x469406f8u },
    { 179u, 0x43c7fee8u, 0x5e2c9691u, 0x098445f1u }
};

static int log_plane_reference_frame(const crtube_h264_edge264_frame_t *frame,
                                     unsigned frame_number,
                                     int check_reference,
                                     int log_details,
                                     uint32_t *out_y,
                                     uint32_t *out_cb,
                                     uint32_t *out_cr)
{
    uint32_t y_crc, cb_crc, cr_crc;
    unsigned cy, cx, reference_index;

    if (!frame || !frame->y || !frame->cb || !frame->cr)
        return -1;
    y_crc = plane_crc32(frame->y, frame->width, frame->height,
                        frame->stride_y);
    cb_crc = plane_crc32(frame->cb, frame->chroma_width,
                         frame->chroma_height, frame->stride_c);
    cr_crc = plane_crc32(frame->cr, frame->chroma_width,
                         frame->chroma_height, frame->stride_c);
    if (out_y) *out_y = y_crc;
    if (out_cb) *out_cb = cb_crc;
    if (out_cr) *out_cr = cr_crc;

    /* A small coordinate fingerprint helps diagnose plane offsets/swaps
       without dumping hundreds of kilobytes to the PS2 debug log. */
    cy = frame->height / 2u;
    cx = frame->width / 2u;
    for (reference_index = 0;
         check_reference &&
         reference_index < sizeof(embedded_plane_references) /
                           sizeof(embedded_plane_references[0]);
         ++reference_index) {
        const h264_plane_reference_t *reference =
            &embedded_plane_references[reference_index];
        if (reference->frame_number == frame_number) {
            if (y_crc != reference->y_crc ||
                cb_crc != reference->cb_crc ||
                cr_crc != reference->cr_crc) {
                crtube_log("H.264 REFERENCE MISMATCH frame=%u "
                           "got=%08lx/%08lx/%08lx "
                           "expected=%08lx/%08lx/%08lx\n",
                           frame_number,
                           (unsigned long)y_crc, (unsigned long)cb_crc,
                           (unsigned long)cr_crc,
                           (unsigned long)reference->y_crc,
                           (unsigned long)reference->cb_crc,
                           (unsigned long)reference->cr_crc);
                return -1;
            }
            if (log_details)
                crtube_log("H.264 REFERENCE frame=%u result=PASS\n",
                           frame_number);
            break;
        }
    }

    if (log_details)
        crtube_log("H.264 PLANES frame=%u poc=%lld size=%ux%u chroma=%ux%u "
                   "stride=%u/%u crc_y=%08lx crc_cb=%08lx crc_cr=%08lx "
                   "y=%u,%u,%u cb=%u,%u,%u cr=%u,%u,%u\n",
                   frame_number, (long long)frame->display_poc,
                   frame->width, frame->height, frame->chroma_width,
                   frame->chroma_height, frame->stride_y, frame->stride_c,
                   (unsigned long)y_crc, (unsigned long)cb_crc,
                   (unsigned long)cr_crc,
                   frame->y[0], frame->y[(size_t)cy * frame->stride_y + cx],
                   frame->y[(size_t)(frame->height - 1u) * frame->stride_y +
                            frame->width - 1u],
                   frame->cb[0],
                   frame->cb[(size_t)(cy / 2u) * frame->stride_c + cx / 2u],
                   frame->cb[(size_t)(frame->chroma_height - 1u) *
                             frame->stride_c + frame->chroma_width - 1u],
                   frame->cr[0],
                   frame->cr[(size_t)(cy / 2u) * frame->stride_c + cx / 2u],
                   frame->cr[(size_t)(frame->chroma_height - 1u) *
                             frame->stride_c + frame->chroma_width - 1u]);
    return 0;
}

#if defined(CRTUBE_H264_MMI_MATRIX_TEST)
#define CRTUBE_H264_MATRIX_MAX_FRAMES 256u
static uint32_t h264_matrix_reference[CRTUBE_H264_MATRIX_MAX_FRAMES][3];
static unsigned h264_matrix_reference_frames;
static unsigned h264_matrix_last_frames;
static int h264_matrix_active;
static int h264_matrix_capture;
static unsigned h264_matrix_display_pass;
static unsigned h264_matrix_display_pass_count;
static const char *h264_matrix_display_name;
static int h264_matrix_mmi_selftest_done;

static void h264_matrix_screen_status(unsigned frames, const char *phase)
{
    if (crtube_screen_init() < 0)
        return;

    crtube_screen_set_visible(1);
    crtube_screen_clear();
    scr_printf("CRTube H.264 MMI MATRIX\n");
    scr_printf("Pass %u/%u\n", h264_matrix_display_pass,
               h264_matrix_display_pass_count);
    scr_printf("%s\n", h264_matrix_display_name ?
               h264_matrix_display_name : "Preparing");
    scr_printf("%s\n", phase ? phase : "Running");
    scr_printf("Frames: %u/180\n", frames > 180u ? 180u : frames);
    scr_printf("Please wait; do not power off.\n");
}
#endif

typedef struct playback_state {
    int matrix_mode;
    int matrix_capture;
    unsigned frames;
    unsigned bad_frames;
    int render_error;
    u64 timeline_start;
    u64 render_ticks;
    u64 wait_ticks;
    u64 decode_est_us;
    int quiet;
    int check_embedded_reference;
} playback_state_t;

static u32 timer_ticks_to_us(u64 ticks)
{
    return (u32)((ticks * 125u) / 18432u);
}

static int playback_frame(const crtube_h264_edge264_frame_t *frame,
                          void *opaque)
{
    playback_state_t *state = (playback_state_t *)opaque;
    int result;
    u64 render_start = 0;
    u64 render_end;
    u64 now;
    u64 wait_start;
    u32 elapsed_us;
    u32 deadline_us;
    unsigned frame_number;

    if (!state || !frame || frame->width != 320u ||
        frame->height != 240u || frame->bit_depth_y != 8u ||
        frame->bit_depth_c != 8u) {
        if (state) state->bad_frames++;
        return -1;
    }

#if defined(CRTUBE_H264_MMI_MATRIX_TEST)
    if (state->matrix_mode) {
        uint32_t y_crc, cb_crc, cr_crc;
        int detail = state->matrix_capture &&
            (state->frames < 3u || state->frames == 29u ||
             state->frames == 59u || state->frames == 89u ||
             state->frames == 119u || state->frames == 149u ||
             state->frames == 179u);
        if (state->frames >= CRTUBE_H264_MATRIX_MAX_FRAMES ||
            log_plane_reference_frame(frame, state->frames,
                state->matrix_capture && state->check_embedded_reference,
                detail, &y_crc, &cb_crc, &cr_crc) < 0) {
            state->bad_frames++;
            return -2;
        }
        if (state->matrix_capture) {
            h264_matrix_reference[state->frames][0] = y_crc;
            h264_matrix_reference[state->frames][1] = cb_crc;
            h264_matrix_reference[state->frames][2] = cr_crc;
        } else if (state->frames >= h264_matrix_reference_frames ||
                   y_crc != h264_matrix_reference[state->frames][0] ||
                   cb_crc != h264_matrix_reference[state->frames][1] ||
                   cr_crc != h264_matrix_reference[state->frames][2]) {
            crtube_log("H.264 MATRIX MISMATCH frame=%u got=%08lx/%08lx/%08lx "
                       "scalar=%08lx/%08lx/%08lx\n",
                       state->frames, (unsigned long)y_crc,
                       (unsigned long)cb_crc, (unsigned long)cr_crc,
                       (unsigned long)(state->frames < h264_matrix_reference_frames ?
                           h264_matrix_reference[state->frames][0] : 0u),
                       (unsigned long)(state->frames < h264_matrix_reference_frames ?
                           h264_matrix_reference[state->frames][1] : 0u),
                       (unsigned long)(state->frames < h264_matrix_reference_frames ?
                           h264_matrix_reference[state->frames][2] : 0u));
            state->bad_frames++;
            return -3;
        }
        frame_number = state->frames + 1u;
        if (frame_number == 1u || (frame_number % 15u) == 0u ||
            frame_number == 180u) {
            u64 screen_start = GetTimerSystemTime();
            h264_matrix_screen_status(frame_number, "Decoding / CRC compare");
            state->wait_ticks += GetTimerSystemTime() - screen_start;
        }
        state->frames++;
        return 0;
    }
#endif

    /* Diagnostic mode fingerprints every displayed frame before GS upload.
       The zero-based frame index matches the FFmpeg reference CSV. This is
       intentionally expensive and must not be used for performance timing. */
#if CRTUBE_H264_PLANE_CRC_EVERY_FRAME
    if (!state->quiet &&
        log_plane_reference_frame(frame, state->frames,
                                  state->check_embedded_reference, 1,
                                  NULL, NULL, NULL) < 0) {
        state->bad_frames++;
        return -2;
    }
#endif

    if (!state->quiet)
        render_start = GetTimerSystemTime();
    result = crtube_screen_draw_edge264_frame(frame, 0, 0);
    if (result < 0) {
        state->render_error = result;
        crtube_log("H.264 GS upload failed: frame=%u result=%d\n",
                   state->frames, result);
        return -1;
    }

    if (!state->quiet) {
        render_end = GetTimerSystemTime();
        state->render_ticks += render_end - render_start;
    }
    state->frames++;
    frame_number = state->frames;
    if (!state->quiet &&
        (state->frames <= 2u || (state->frames % 60u) == 0u)) {
        crtube_log("H.264 playback frame=%u size=%ux%u stride=%u/%u\n",
                   state->frames, frame->width, frame->height,
                   frame->stride_y, frame->stride_c);
    }

    /* Pace against an absolute clip timeline. Decode time before this
       callback is included, so it is never added on top of a fresh 41.667 ms
       sleep for every frame. */
    now = GetTimerSystemTime();
    elapsed_us = timer_ticks_to_us(now - state->timeline_start);
    deadline_us = frame_number * 41667u;
    if (elapsed_us < deadline_us) {
        wait_start = now;
        DelayThread((int)(deadline_us - elapsed_us));
        if (!state->quiet)
            state->wait_ticks += GetTimerSystemTime() - wait_start;
    }
    return 0;
}

static int h264_edge264_playback_run(const char *path,
                                     const uint8_t *embedded_data,
                                     size_t embedded_size, int quiet)
{
    uint8_t *file_data = NULL;
    uint8_t *scratch = NULL;
    size_t scratch_capacity = 0;
    size_t max_sample_size = 0;
    size_t decode_sample_count = 0;
    long file_size;
    crtube_mp4_t mp4;
    crtube_mp4_sample_t sample;
    crtube_h264_edge264_t *decoder = NULL;
    crtube_h264_edge264_memory_stats_t memory_stats;
    playback_state_t state;
    unsigned i;
    int result = -1;
    u64 run_start = 0u;
    u64 run_end = 0u;

    memset(&mp4, 0, sizeof(mp4));
    memset(&state, 0, sizeof(state));
    memset(&memory_stats, 0, sizeof(memory_stats));
#if defined(CRTUBE_H264_MMI_MATRIX_TEST)
    state.matrix_mode = h264_matrix_active;
    state.matrix_capture = h264_matrix_capture;
#endif
    if ((!path && !embedded_data) || (embedded_data && !embedded_size))
        return -1;
#if defined(CRTUBE_R5900_MMI_ENABLED) && CRTUBE_R5900_MMI_ENABLED
#if defined(CRTUBE_H264_MMI_MATRIX_TEST)
    if (!state.matrix_mode || !h264_matrix_mmi_selftest_done) {
#else
    {
#endif
        static const char *const mmi_names[] = {
            "paddub", "paddsh", "pmaddh", "pminub", "pmaxub",
            "psubub", "pminh", "pmaxh", "pcgth", "pceqh",
            "pand", "por", "pxor", "paddw", "psubw", "psraw1",
            "paddh", "psubh", "psrah1", "psrah2", "psrah6"
        };
#if defined(CRTUBE_H264_MMI_MATRIX_TEST)
        if (state.matrix_mode && !h264_matrix_mmi_selftest_done)
            h264_matrix_screen_status(0u, "Validating MMI primitives");
#endif
        int mmi_failures = edge264_r5900_mmi_selftest();
        unsigned mmi_index;
        for (mmi_index = 0;
             mmi_index < sizeof(mmi_names) / sizeof(mmi_names[0]);
             ++mmi_index) {
            crtube_log("H.264 MMI primitive=%s result=%s\n",
                       mmi_names[mmi_index],
                       (mmi_failures & (1 << mmi_index)) ? "FAIL" : "PASS");
        }
        crtube_log("H.264 R5900 MMI selftest: failures=0x%08x\n",
                   (unsigned)mmi_failures);
        if (mmi_failures != 0) {
            crtube_log("H.264 R5900 MMI disabled for this run: primitive validation failed\n");
            return -20;
        }
#if defined(CRTUBE_H264_MMI_MATRIX_TEST)
        if (state.matrix_mode)
            h264_matrix_mmi_selftest_done = 1;
#endif
    }
#endif
    state.quiet = quiet;
    /* Matrix mode establishes its own scalar baseline and compares each
       MMI pass against it. The independent FFmpeg fingerprint table remains
       active in the normal playback runner, not in this differential test. */
    state.check_embedded_reference = embedded_data != NULL &&
                                     !state.matrix_mode;
    if (quiet)
        crtube_log_set_enabled(0);
    run_start = GetTimerSystemTime();

    if (embedded_data) {
        file_data = (uint8_t *)embedded_data;
        file_size = (long)embedded_size;
        crtube_log("H.264 embedded playback: memory source bytes=%ld\n",
                   file_size);
    } else {
        crtube_log("H.264 SMB playback: opening %s\n", path);
        file_data = (uint8_t *)malloc(CRTUBE_BASELINE_MP4_CAPACITY);
        if (!file_data) {
            result = -2;
            goto done;
        }
        file_size = crtube_h264_read_smb_file(
            path, file_data, CRTUBE_BASELINE_MP4_CAPACITY);
        if (file_size <= 0) {
            crtube_log("H.264 SMB playback: file read failed=%ld\n", file_size);
            result = -3;
            goto done;
        }
        crtube_log("H.264 SMB playback: file bytes=%ld\n", file_size);
    }

    if (crtube_mp4_open_memory(&mp4, file_data, (size_t)file_size) < 0 ||
        !mp4.video_track_found || mp4.width != 320u || mp4.height != 240u ||
        !mp4.sample_count) {
        crtube_log("H.264 playback: unsupported MP4 metadata "
                   "width=%lu height=%lu samples=%lu\n",
                   (unsigned long)mp4.width, (unsigned long)mp4.height,
                   (unsigned long)mp4.sample_count);
        result = -4;
        goto done;
    }

    for (i = 0; i < mp4.sample_count; ++i) {
        if (crtube_mp4_get_sample(&mp4, i, &sample) < 0) {
            result = -5;
            goto done;
        }
        if (sample.size > max_sample_size)
            max_sample_size = sample.size;
    }
    if (!max_sample_size || max_sample_size > (SIZE_MAX - 64u) / 2u) {
        result = -6;
        goto done;
    }

    scratch_capacity = max_sample_size * 2u + 64u;
    scratch = (uint8_t *)memalign(64u, scratch_capacity);
    if (!scratch) {
        result = -7;
        goto done;
    }

    if (crtube_screen_init() < 0) {
        crtube_log("H.264 playback: GS initialization failed\n");
        result = -8;
        goto done;
    }
#if defined(CRTUBE_H264_MMI_MATRIX_TEST)
    if (state.matrix_mode)
        h264_matrix_screen_status(0u, "Preparing decoder");
    else
#endif
        crtube_screen_set_visible(0);

    if (!quiet)
        edge264_profile_reset();
    else
        edge264_profile_disable();
    decoder = crtube_h264_edge264_create_with_memory_limit(
        CRTUBE_EDGE264_MEMORY_LIMIT);
    if (!decoder) {
        result = -9;
        goto done;
    }

    if (crtube_h264_edge264_open_mp4(
            decoder, &mp4, scratch, scratch_capacity) < 0) {
        crtube_log("H.264 playback: avcC/SPS/PPS initialization failed\n");
        result = -10;
        goto done;
    }

    decode_sample_count = mp4.sample_count;
#if CRTUBE_H264_TEST_MAX_SAMPLES > 0
    if (decode_sample_count > CRTUBE_H264_TEST_MAX_SAMPLES)
        decode_sample_count = CRTUBE_H264_TEST_MAX_SAMPLES;
#endif
#if defined(CRTUBE_H264_MMI_MATRIX_TEST)
    crtube_log("H.264 backend: R5900 MMI-capable runtime_mask=0x%08x\n",
               edge264_r5900_mmi_get_mode());
    crtube_log("H.264 MMI diagnostic: paths are compiled in; runtime mask "
               "selects which operations execute\n");
#elif defined(CRTUBE_R5900_MMI_ENABLED) && CRTUBE_R5900_MMI_ENABLED
    crtube_log("H.264 backend: R5900 MMI=ENABLED\n");
#if defined(EDGE264_R5900_DISABLE_MINMAX_U8)
    crtube_log("H.264 MMI diagnostic: unsigned-byte min/max=SCALAR\n");
#else
    crtube_log("H.264 MMI diagnostic: unsigned-byte min/max=MMI\n");
#endif
#if defined(EDGE264_R5900_DISABLE_CHROMA_MC)
    crtube_log("H.264 MMI diagnostic: chroma motion compensation=SCALAR\n");
#elif defined(EDGE264_R5900_DISABLE_CHROMA_MADDABCD)
    crtube_log("H.264 MMI diagnostic: chroma maddABCD=SCALAR weighted=C16/C8/C4=MMI\n");
#elif defined(EDGE264_R5900_DISABLE_CHROMA_WEIGHTED)
    crtube_log("H.264 MMI diagnostic: chroma maddABCD=MMI weighted=C16/C8/C4=SCALAR\n");
#else
    crtube_log("H.264 MMI diagnostic: chroma motion compensation=MMI\n");
#endif
#else
    crtube_log("H.264 backend: R5900 MMI=DISABLED (scalar fallback)\n");
#endif
    crtube_log("H.264 CRC reference: every displayed frame; CRC-32/ISO-HDLC; "
               "visible Y/Cb/Cr bytes only; zero-based frame index\n");
#if CRTUBE_H264_PLANE_CRC_EVERY_FRAME
    crtube_log("H.264 CRC instrumentation: ALL FRAMES (diagnostic; timing invalid)\n");
#else
    crtube_log("H.264 CRC instrumentation: DISABLED\n");
#endif
    crtube_log("H.264 playback: decode start samples=%lu "
               "dimensions=%lux%lu scratch=%lu\n",
               (unsigned long)decode_sample_count,
               (unsigned long)mp4.width, (unsigned long)mp4.height,
               (unsigned long)scratch_capacity);

    crtube_log("H.264 playback: test sample limit=%lu (%s)\n",
               (unsigned long)decode_sample_count,
               decode_sample_count == mp4.sample_count ? "full stream" : "prefix");

    state.timeline_start = GetTimerSystemTime();
    for (i = 0; i < decode_sample_count; ++i) {
        u64 sample_start = quiet ? 0u : GetTimerSystemTime();
        u64 render_before = state.render_ticks;
        u64 wait_before = state.wait_ticks;
        u64 sample_end;
        u32 sample_us, render_us, wait_us;
        result = crtube_h264_edge264_decode_mp4_sample(
            decoder, &mp4, i, scratch, scratch_capacity,
            playback_frame, &state);
        if (!quiet) {
            sample_end = GetTimerSystemTime();
            sample_us = timer_ticks_to_us(sample_end - sample_start);
            render_us = timer_ticks_to_us(state.render_ticks - render_before);
            wait_us = timer_ticks_to_us(state.wait_ticks - wait_before);
            if (sample_us > render_us + wait_us)
                state.decode_est_us += sample_us - render_us - wait_us;
            if ((i % 30u) == 0u || i + 1u == decode_sample_count)
                crtube_log("H.264 timing sample=%u total_us=%lu render_us=%lu "
                           "wait_us=%lu decode_est_us=%lu frames=%u\n",
                           i + 1u, (unsigned long)sample_us,
                           (unsigned long)render_us, (unsigned long)wait_us,
                           (unsigned long)(sample_us > render_us + wait_us ?
                               sample_us - render_us - wait_us : 0u), state.frames);
        }
        if (result < 0) {
            crtube_log("H.264 playback: sample failed index=%u "
                       "decoder_result=%d frames=%u\n",
                       i, result, state.frames);
            goto done;
        }
    }

    result = crtube_h264_edge264_finish(decoder, playback_frame, &state);
    if (result < 0) {
        crtube_log("H.264 playback: final drain failed=%d\n", result);
        goto done;
    }

    crtube_h264_edge264_get_memory_stats(decoder, &memory_stats);
    crtube_log("H.264 timing totals decode_est_us=%llu render_us=%lu "
               "wait_us=%lu frames=%u\n",
               (unsigned long long)state.decode_est_us,
               (unsigned long)timer_ticks_to_us(state.render_ticks),
               (unsigned long)timer_ticks_to_us(state.wait_ticks), state.frames);
    crtube_log("H.264 playback: complete frames=%u samples=%lu "
               "bad=%u peak=%lu current=%lu alloc_fail=%u\n",
               state.frames, (unsigned long)decode_sample_count,
               state.bad_frames,
               (unsigned long)memory_stats.peak_bytes,
               (unsigned long)memory_stats.current_bytes,
               memory_stats.allocation_failures);
    if (!quiet) {
        static const char *const profile_names[EDGE264_PROFILE_COUNT] = {
            "inter_mc", "intra4x4", "intra8x8", "intra16x16",
            "intra_chroma", "idct4x4", "idct8x8", "dc4x4",
            "dc_transforms", "deblock_mb", "cavlc_residual",
            "cavlc_slice_inclusive"
        };
        Edge264Profile profile;
        unsigned profile_index;
        edge264_profile_read(&profile);
        for (profile_index = 0; profile_index < EDGE264_PROFILE_COUNT;
             ++profile_index) {
            crtube_log("H.264 PROFILE op=%s calls=%lu inclusive_us=%lu\n",
                       profile_names[profile_index],
                       (unsigned long)profile.calls[profile_index],
                       (unsigned long)timer_ticks_to_us(
                           profile.ticks[profile_index]));
        }
    }

    if (state.render_error || state.bad_frames ||
        state.frames != decode_sample_count ||
        memory_stats.allocation_failures ||
        memory_stats.peak_bytes > memory_stats.allocation_limit) {
        result = -11;
        goto done;
    }
    result = 0;

done:
    crtube_h264_edge264_destroy(decoder);
    free(scratch);
    if (!embedded_data)
        free(file_data);
    run_end = GetTimerSystemTime();
    if (quiet)
        crtube_log_set_enabled(1);
    crtube_log("H.264 %s RUN timestamps start=%llu end=%llu "
               "elapsed_us=%lu result=%d frames=%u\n",
               quiet ? "QUIET" : "INSTRUMENTED",
               (unsigned long long)run_start,
               (unsigned long long)run_end,
               (unsigned long)timer_ticks_to_us(run_end - run_start),
               result, state.frames);
#if defined(CRTUBE_H264_MMI_MATRIX_TEST)
    if (state.matrix_mode)
        h264_matrix_last_frames = state.frames;
#endif
    return result;
}

int crtube_h264_edge264_smb_playback(const char *path)
{
    return h264_edge264_playback_run(path, NULL, 0u, 0);
}

int crtube_h264_edge264_smb_playback_quiet(const char *path)
{
    return h264_edge264_playback_run(path, NULL, 0u, 1);
}

#if defined(CRTUBE_H264_MMI_MATRIX_TEST)
static int h264_edge264_playback_matrix_run(void)
{
    static const struct {
        const char *name;
        unsigned mask;
    } passes[] = {
        { "SCALAR_BASELINE", 0u },
        { "MC_DOTPROD_MMI_ONLY", EDGE264_R5900_MMI_OP_CHROMA },
        { "WEIGHTED_MC_MMI_ONLY", EDGE264_R5900_MMI_OP_WEIGHTED },
        { "MINMAX_MMI_ONLY", EDGE264_R5900_MMI_OP_MINMAX },
        { "RESIDUAL_MMI_ONLY", EDGE264_R5900_MMI_OP_RESIDUAL },
        { "DEBLOCK_ADDSU8_MMI_ONLY", EDGE264_R5900_MMI_OP_DEBLOCK_ADD },
        { "DEBLOCK_PACKABD_MAXMIN_MMI", EDGE264_R5900_MMI_OP_DEBLOCK_PACKABD },
        { "DEBLOCK_PACKABD_CMPXOR_MMI", EDGE264_R5900_MMI_OP_DEBLOCK_PACKABD_ALT },
        { "ALL_AVAILABLE_MMI", EDGE264_R5900_MMI_OP_CHROMA |
                               EDGE264_R5900_MMI_OP_WEIGHTED |
                               EDGE264_R5900_MMI_OP_MINMAX |
                               EDGE264_R5900_MMI_OP_RESIDUAL |
                               EDGE264_R5900_MMI_OP_DEBLOCK_ADD |
                               EDGE264_R5900_MMI_OP_DEBLOCK_PACKABD }
    };
    unsigned pass, completed_passes = 0;
    unsigned calls_chroma, calls_weighted, calls_minmax, calls_residual;
    unsigned calls_deblock_add, calls_deblock_packabd;
    unsigned calls_deblock_packabd_alt;
    int result = 0, pass_result;

    h264_matrix_active = 1;
    h264_matrix_capture = 1;
    h264_matrix_display_pass = 0u;
    h264_matrix_display_pass_count =
        (unsigned)(sizeof(passes) / sizeof(passes[0]));
    h264_matrix_display_name = "Starting";
    h264_matrix_mmi_selftest_done = 0;
    h264_matrix_reference_frames = 0;
    h264_matrix_last_frames = 0;
    crtube_log("H.264 MMI MATRIX: begin passes=%u fixture_frames=180\n",
               (unsigned)(sizeof(passes) / sizeof(passes[0])));
    crtube_log("H.264 MMI MATRIX timing=diagnostic-only; CRC callbacks are "
               "included and this is not an FPS benchmark\n");
    crtube_log("H.264 MMI MATRIX scalar-only paths: packus16, maddubx, "
               "avgu8, vector shuffle/zip\n");
    for (pass = 0; pass < sizeof(passes) / sizeof(passes[0]); ++pass) {
        h264_matrix_capture = pass == 0u;
        h264_matrix_display_pass = pass + 1u;
        h264_matrix_display_name = passes[pass].name;
        h264_matrix_screen_status(0u, "Starting pass");
        edge264_r5900_mmi_set_mode(passes[pass].mask);
        edge264_r5900_mmi_reset_counters();
        crtube_log("H.264 MMI MATRIX pass=%u name=%s mask=0x%02x start\n",
                   pass, passes[pass].name, passes[pass].mask);
        pass_result = h264_edge264_playback_run(NULL, edge264_test_mp4,
                    (size_t)size_edge264_test_mp4, 0);
        result = pass_result;
        ++completed_passes;
        calls_chroma = edge264_r5900_mmi_get_counter(EDGE264_R5900_MMI_OP_CHROMA);
        calls_weighted = edge264_r5900_mmi_get_counter(EDGE264_R5900_MMI_OP_WEIGHTED);
        calls_minmax = edge264_r5900_mmi_get_counter(EDGE264_R5900_MMI_OP_MINMAX);
        calls_residual = edge264_r5900_mmi_get_counter(EDGE264_R5900_MMI_OP_RESIDUAL);
        calls_deblock_add = edge264_r5900_mmi_get_counter(EDGE264_R5900_MMI_OP_DEBLOCK_ADD);
        calls_deblock_packabd = edge264_r5900_mmi_get_counter(EDGE264_R5900_MMI_OP_DEBLOCK_PACKABD);
        calls_deblock_packabd_alt = edge264_r5900_mmi_get_counter(EDGE264_R5900_MMI_OP_DEBLOCK_PACKABD_ALT);
        crtube_log("H.264 MMI MATRIX pass=%u name=%s result=%s frames=%u "
                   "calls_dotprod=%u calls_weighted=%u calls_minmax=%u "
                   "calls_residual=%u calls_deblock_add=%u calls_packabd=%u "
                   "calls_packabd_alt=%u\n",
                   pass, passes[pass].name, pass_result == 0 ? "PASS" : "FAIL",
                   h264_matrix_last_frames, calls_chroma, calls_weighted,
                   calls_minmax, calls_residual, calls_deblock_add,
                   calls_deblock_packabd, calls_deblock_packabd_alt);
        h264_matrix_screen_status(h264_matrix_last_frames,
                                  pass_result == 0 ? "Pass validated" : "PASS FAILED");
        if (result != 0)
            break;
        if (pass == 0u) {
            h264_matrix_reference_frames = h264_matrix_last_frames;
            if (h264_matrix_reference_frames != 180u) {
                crtube_log("H.264 MMI MATRIX baseline frame count invalid=%u\n",
                           h264_matrix_reference_frames);
                result = -30;
                break;
            }
        } else if (h264_matrix_last_frames != h264_matrix_reference_frames) {
            crtube_log("H.264 MMI MATRIX frame-count mismatch pass=%u "
                       "expected=%u got=%u\n", pass,
                       h264_matrix_reference_frames, h264_matrix_last_frames);
            if (result == 0)
                result = -31;
        }
    }
    edge264_r5900_mmi_set_mode(0u);
    h264_matrix_capture = 0;
    h264_matrix_active = 0;
    crtube_log("H.264 MMI MATRIX final: %s completed_passes=%u\n",
               result == 0 ? "PASS" : "FAIL", completed_passes);
    h264_matrix_display_name = "MMI MATRIX";
    h264_matrix_screen_status(h264_matrix_last_frames,
                              result == 0 ? "ALL PASSES: PASS" : "TEST FAILED");
    scr_printf("See SMB log for details.\n");
    return result;
}
#endif

int crtube_h264_edge264_embedded_playback(void)
{
#if defined(CRTUBE_H264_MMI_MATRIX_TEST)
    return h264_edge264_playback_matrix_run();
#else
    return h264_edge264_playback_run(NULL, edge264_test_mp4,
                                     (size_t)size_edge264_test_mp4, 0);
#endif
}

int crtube_h264_edge264_embedded_playback_quiet(void)
{
    return h264_edge264_playback_run(NULL, edge264_test_mp4,
                                     (size_t)size_edge264_test_mp4, 1);
}


int crtube_h264_edge264_embedded_four_pass(void)
{
#if defined(CRTUBE_H264_MMI_MATRIX_TEST)
    static const unsigned all_mmi =
        EDGE264_R5900_MMI_OP_CHROMA |
        EDGE264_R5900_MMI_OP_MINMAX |
        EDGE264_R5900_MMI_OP_RESIDUAL |
        EDGE264_R5900_MMI_OP_WEIGHTED |
        EDGE264_R5900_MMI_OP_DEBLOCK_ADD |
        EDGE264_R5900_MMI_OP_DEBLOCK_PACKABD |
        EDGE264_R5900_MMI_OP_DEBLOCK_PACKABD_ALT;
    int result = 0;
    int pass_result;
    unsigned pass;
    unsigned completed = 0u;
    unsigned calls_chroma, calls_weighted, calls_minmax, calls_residual;
    unsigned calls_deblock_add, calls_packabd, calls_packabd_alt;

    struct {
        const char *name;
        unsigned mode;
        int quiet;
        int vu;
    } passes[6] = {
        { "SCALAR_INSTRUMENTED", 0u, 0, 0 },
        { "SCALAR_QUIET", 0u, 1, 0 },
        { "MMI_INSTRUMENTED", 0u, 0, 0 },
        { "MMI_QUIET", 0u, 1, 0 },
        { "MMI_VU_INSTRUMENTED", 0u, 0, 1 },
        { "MMI_VU_QUIET", 0u, 1, 1 }
    };
    passes[2].mode = all_mmi;
    passes[3].mode = all_mmi;
    passes[4].mode = all_mmi;
    passes[5].mode = all_mmi;

    crtube_log("H.264 EE/VU differential run: begin passes=6 "
               "order=scalar-instrumented,scalar-quiet,mmi-instrumented,mmi-quiet,mmi-vu-instrumented,mmi-vu-quiet\n");
    for (pass = 0; pass < 6u; ++pass) {
        edge264_r5900_mmi_set_mode(passes[pass].mode);
#if defined(CRTUBE_R5900_DECODER_TEST_ONLY)
        crtube_vu_inter_luma_set_enabled(passes[pass].vu);
        crtube_vu_inter_luma_reset_stats();
#endif
        edge264_r5900_mmi_reset_counters();
        crtube_log("H.264 EE/VU begin pass=%u name=%s mmi_mask=0x%08x\n",
                   pass + 1u, passes[pass].name, passes[pass].mode);
        pass_result = h264_edge264_playback_run(
            NULL, edge264_test_mp4, (size_t)size_edge264_test_mp4,
            passes[pass].quiet);
        calls_chroma = edge264_r5900_mmi_get_counter(EDGE264_R5900_MMI_OP_CHROMA);
        calls_weighted = edge264_r5900_mmi_get_counter(EDGE264_R5900_MMI_OP_WEIGHTED);
        calls_minmax = edge264_r5900_mmi_get_counter(EDGE264_R5900_MMI_OP_MINMAX);
        calls_residual = edge264_r5900_mmi_get_counter(EDGE264_R5900_MMI_OP_RESIDUAL);
        calls_deblock_add = edge264_r5900_mmi_get_counter(EDGE264_R5900_MMI_OP_DEBLOCK_ADD);
        calls_packabd = edge264_r5900_mmi_get_counter(EDGE264_R5900_MMI_OP_DEBLOCK_PACKABD);
        calls_packabd_alt = edge264_r5900_mmi_get_counter(EDGE264_R5900_MMI_OP_DEBLOCK_PACKABD_ALT);
#if defined(CRTUBE_R5900_DECODER_TEST_ONLY)
        crtube_vu_inter_luma_log_stats();
#endif
        crtube_log("H.264 EE/VU counters pass=%u name=%s dotprod=%u "
                   "weighted=%u minmax=%u residual=%u deblock_add=%u "
                   "packabd=%u packabd_alt=%u\n",
                   pass + 1u, passes[pass].name, calls_chroma,
                   calls_weighted, calls_minmax, calls_residual,
                   calls_deblock_add, calls_packabd, calls_packabd_alt);
        crtube_log("H.264 EE/VU end pass=%u name=%s result=%d\n",
                   pass + 1u, passes[pass].name, pass_result);
        ++completed;
        if (pass_result != 0 && result == 0)
            result = pass_result;
    }
    edge264_r5900_mmi_set_mode(0u);
#if defined(CRTUBE_R5900_DECODER_TEST_ONLY)
    crtube_vu_inter_luma_set_enabled(0);
#endif
    crtube_log("H.264 EE/VU final: %s completed=%u result=%d\n",
               result == 0 ? "PASS" : "FAIL", completed, result);
    return result;
#else
    return -90;
#endif
}
