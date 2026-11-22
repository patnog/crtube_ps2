#include "h264_edge264_smb_playback.h"

#include "h264_edge264.h"
#include "h264_mp4.h"
#include "h264_mp4_smb.h"
#include "screen.h"
#include "debug_log.h"

#include <delaythread.h>
#include <timer.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define CRTUBE_BASELINE_MP4_CAPACITY (2u * 1024u * 1024u)
#define CRTUBE_EDGE264_MEMORY_LIMIT (12u * 1024u * 1024u)

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

static int log_plane_reference_frame(const crtube_h264_edge264_frame_t *frame,
                                     unsigned frame_number)
{
    uint32_t y_crc, cb_crc, cr_crc;
    unsigned cy, cx;

    if (!frame || !frame->y || !frame->cb || !frame->cr)
        return -1;
    y_crc = plane_crc32(frame->y, frame->width, frame->height,
                        frame->stride_y);
    cb_crc = plane_crc32(frame->cb, frame->chroma_width,
                         frame->chroma_height, frame->stride_c);
    cr_crc = plane_crc32(frame->cr, frame->chroma_width,
                         frame->chroma_height, frame->stride_c);

    /* A small coordinate fingerprint helps diagnose plane offsets/swaps
       without dumping hundreds of kilobytes to the PS2 debug log. */
    cy = frame->height / 2u;
    cx = frame->width / 2u;
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

typedef struct playback_state {
    unsigned frames;
    unsigned bad_frames;
    int render_error;
    u64 timeline_start;
    u64 render_ticks;
    u64 wait_ticks;
    u64 decode_est_us;
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
    u64 render_start;
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

    /* Log every raw decoder output before YUV->CT32/GS. Frame numbering is
       zero-based to match 240p-baseline-plane-crc.csv. */
    (void)log_plane_reference_frame(frame, state->frames);

    render_start = GetTimerSystemTime();
    result = crtube_screen_draw_edge264_frame(frame, 0, 0);
    if (result < 0) {
        state->render_error = result;
        crtube_log("H.264 GS upload failed: frame=%u result=%d\n",
                   state->frames, result);
        return -1;
    }

    render_end = GetTimerSystemTime();
    state->render_ticks += render_end - render_start;
    state->frames++;
    frame_number = state->frames;
    if (state->frames <= 2u || (state->frames % 60u) == 0u) {
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
        state->wait_ticks += GetTimerSystemTime() - wait_start;
    }
    return 0;
}

int crtube_h264_edge264_smb_playback(const char *path)
{
    uint8_t *file_data = NULL;
    uint8_t *scratch = NULL;
    size_t scratch_capacity = 0;
    size_t max_sample_size = 0;
    long file_size;
    crtube_mp4_t mp4;
    crtube_mp4_sample_t sample;
    crtube_h264_edge264_t *decoder = NULL;
    crtube_h264_edge264_memory_stats_t memory_stats;
    playback_state_t state;
    unsigned i;
    int result = -1;

    memset(&mp4, 0, sizeof(mp4));
    memset(&state, 0, sizeof(state));
    memset(&memory_stats, 0, sizeof(memory_stats));

    if (!path)
        return -1;

    crtube_log("H.264 SMB playback: opening %s\n", path);
    file_data = (uint8_t *)malloc(CRTUBE_BASELINE_MP4_CAPACITY);
    if (!file_data)
        return -2;

    file_size = crtube_h264_read_smb_file(
        path, file_data, CRTUBE_BASELINE_MP4_CAPACITY);
    if (file_size <= 0) {
        crtube_log("H.264 SMB playback: file read failed=%ld\n", file_size);
        result = -3;
        goto done;
    }
    crtube_log("H.264 SMB playback: file bytes=%ld\n", file_size);

    if (crtube_mp4_open_memory(&mp4, file_data, (size_t)file_size) < 0 ||
        !mp4.video_track_found || mp4.width != 320u || mp4.height != 240u ||
        !mp4.sample_count) {
        crtube_log("H.264 SMB playback: unsupported MP4 metadata "
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
    scratch = (uint8_t *)malloc(scratch_capacity);
    if (!scratch) {
        result = -7;
        goto done;
    }

    if (crtube_screen_init() < 0) {
        crtube_log("H.264 SMB playback: GS initialization failed\n");
        result = -8;
        goto done;
    }
    crtube_screen_set_visible(0);

    decoder = crtube_h264_edge264_create_with_memory_limit(
        CRTUBE_EDGE264_MEMORY_LIMIT);
    if (!decoder) {
        result = -9;
        goto done;
    }

    if (crtube_h264_edge264_open_mp4(
            decoder, &mp4, scratch, scratch_capacity) < 0) {
        crtube_log("H.264 SMB playback: avcC/SPS/PPS initialization failed\n");
        result = -10;
        goto done;
    }

    crtube_log("H.264 CRC reference: CRC-32/ISO-HDLC, visible plane bytes, "
               "zero-based frame index; compare Y/Cb/Cr with "
               "240p-baseline-plane-crc.csv\n");
    crtube_log("H.264 SMB playback: decode start samples=%lu "
               "dimensions=%lux%lu scratch=%lu\n",
               (unsigned long)mp4.sample_count,
               (unsigned long)mp4.width, (unsigned long)mp4.height,
               (unsigned long)scratch_capacity);

    state.timeline_start = GetTimerSystemTime();
    for (i = 0; i < mp4.sample_count; ++i) {
        u64 sample_start = GetTimerSystemTime();
        u64 render_before = state.render_ticks;
        u64 wait_before = state.wait_ticks;
        u64 sample_end;
        u32 sample_us, render_us, wait_us;
        result = crtube_h264_edge264_decode_mp4_sample(
            decoder, &mp4, i, scratch, scratch_capacity,
            playback_frame, &state);
        sample_end = GetTimerSystemTime();
        sample_us = timer_ticks_to_us(sample_end - sample_start);
        render_us = timer_ticks_to_us(state.render_ticks - render_before);
        wait_us = timer_ticks_to_us(state.wait_ticks - wait_before);
        if (sample_us > render_us + wait_us)
            state.decode_est_us += sample_us - render_us - wait_us;
        if ((i % 30u) == 0u || i + 1u == mp4.sample_count)
            crtube_log("H.264 timing sample=%u total_us=%lu render_us=%lu "
                       "wait_us=%lu decode_est_us=%lu frames=%u\n",
                       i + 1u, (unsigned long)sample_us,
                       (unsigned long)render_us, (unsigned long)wait_us,
                       (unsigned long)(sample_us > render_us + wait_us ?
                           sample_us - render_us - wait_us : 0u), state.frames);
        if (result < 0) {
            crtube_log("H.264 SMB playback: sample failed index=%u "
                       "decoder_result=%d frames=%u\n",
                       i, result, state.frames);
            goto done;
        }
    }

    result = crtube_h264_edge264_finish(decoder, playback_frame, &state);
    if (result < 0) {
        crtube_log("H.264 SMB playback: final drain failed=%d\n", result);
        goto done;
    }

    crtube_h264_edge264_get_memory_stats(decoder, &memory_stats);
    crtube_log("H.264 timing totals decode_est_us=%llu render_us=%lu "
               "wait_us=%lu frames=%u\n",
               (unsigned long long)state.decode_est_us,
               (unsigned long)timer_ticks_to_us(state.render_ticks),
               (unsigned long)timer_ticks_to_us(state.wait_ticks), state.frames);
    crtube_log("H.264 SMB playback: complete frames=%u samples=%lu "
               "bad=%u peak=%lu current=%lu alloc_fail=%u\n",
               state.frames, (unsigned long)mp4.sample_count,
               state.bad_frames,
               (unsigned long)memory_stats.peak_bytes,
               (unsigned long)memory_stats.current_bytes,
               memory_stats.allocation_failures);

    if (state.render_error || state.bad_frames ||
        state.frames != mp4.sample_count ||
        memory_stats.allocation_failures ||
        memory_stats.peak_bytes > memory_stats.allocation_limit) {
        result = -11;
        goto done;
    }
    result = 0;

done:
    crtube_h264_edge264_destroy(decoder);
    free(scratch);
    free(file_data);
    crtube_log("H.264 SMB playback: end result=%d frames=%u\n",
               result, state.frames);
    return result;
}
