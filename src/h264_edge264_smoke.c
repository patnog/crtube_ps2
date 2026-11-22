#include "h264_edge264.h"
#include "debug_log.h"

#include <stdlib.h>
#include <string.h>

static const char fixture_b64[] =
    "AAAAAWdkAAqs2RyAeEAgywAAAAFozgGuIAAAAAEluEA///kgAAAAASG4mP//5IAAAAABIbkM///k"
    "gAAAAAEBuYX//8kAAAABAbmJ///JAAAAAQG5kf//yQAAAAEBuZX//8kAAAABIbmw///kgAAAAAEh"
    "uiT//+SAAAAAAQG6nf//yQAAAAEBuqH//8kAAAABCgAAAAFnZAAKrNpyAeEAg3AAAAABaM4BriAA"
    "AAABJbhAP//5IAAAAAEhuJD//+SAAAAAAQG5Cf//yQAAAAEBuQX//8kAAAABIbkg///kgAAAAAEB"
    "uZn//8kAAAABAbmV///JAAAAAQoAAAABZ2QACqzacgHhAIVAAAAAAWjOAa4gAAAAASW4QD//+SAA"
    "AAABIbiM///kgAAAAAEBuQn//8kAAAABAbkF///JAAAAASG5EP//5IAAAAABCgAAAAFnZAAKrNtx"
    "AAAAAWjOAa4gAAAAASW4QD//+SAAAAABIbiQ///kgAAAAAEBuQn//8kAAAABJbggD//+SAAAAAEh"
    "uIj//+SAAAAAAQG5Bf//yQAAAAEluEC///kgAAAAASW4IA///kgAAAABIbiE///kgAAAAAEKAAAA"
    "AWdkAAqs23EAAAABaM4BriAAAAABJbhAP//5IAAAAAEhuJj//+SAAAAAAQG5Cf//yQAAAAEhuRE3"
    "//+SAAAAAQG4/f//yQAAAAEhuIj//+SAAAAAAQoAAAABZ2QACqzbcgHhAINwAAAAAWjOAa4gAAAA"
    "ASW4QH//+SAAAAABIbiQ///kgAAAAAEBuQn//8kAAAABIbkhf///JAAAAAEBuZn//8kAAAABIbmx"
    "Iq///5IAAAABAbop///JAAAAASG6OS///8kAAAABCgAAAAFnZAAKrNvyAeEAg3AAAAABaM4BriAA"
    "AAABJbhAP//5IAAAAAEhuJD//+SAAAAAAQG5Cf//yQAAAAEhuiD//+SAAAAAAQG6mf//yQAAAAEh"
    "vTD//+SAAAAAAQoAAAABZ2QACqy05APCAQSgAAAAAWjOAa4gAAAAASW4T//+SAAAAAEhuL//+SAA"
    "AAABAbl///JAAAAAASG5P//5IAAAAAEBuf//8kAAAAABIbm///kgAAAAAQoAAAABZ2QACqypaEJy"
    "AeEAgnAAAAABaM4BriAAAAABJbhP//5IAAAAASG4v//5IAAAAAEBuX//8kAAAAABIbk///kgAAAA"
    "AQG5///yQAAAAAEKAAAAAWdkAAqs2nEAAAABaM4BriAAAAABJbhAP//5IAAAAAEhuIT//+SAAAAA"
    "ASG5CP//5IAAAAABIbmM///kgAAAAAEhuhD//+SAAAAAASG6lP//5IAAAAABIbsY///kgAAAAAEh"
    "u5z//+SAAAAAASG8IP//5IAAAAABIbyk///kgAAAAAEhvSj//+SAAAAAASG9rP//5IAAAAABIb4w"
    "///kgAAAAAEhvrT//+SAAAAAASG/OP//5IAAAAABIb+8///kgAAAAAEhuED//+SAAAAAASG4xP//"
    "5IAAAAABCgAAAAFnZAAKrNtyAeEAg1AAAAABaM4BriAAAAABJbhAP//5IAAAAAEhuJD//+SAAAAA"
    "AQG5Cf//yQAAAAEhuSD//+SAAAAAAQoAAAABZ24QCqzacQAAAAFozgGuIAAAAAEluEA///kgAAAA"
    "ASW4IA///kgAAAABJbhAP//5IAAAAAEKAAAAAWdkAAqs23EAAAABaM4BriAAAAABJbhAP//5IAAA"
    "AAEhuJD//+SAAAAAAQG5Cf//yQAAAAFnZAAKrNtxAAAAAWjOAa4gAAAAAQG5Bf//yQAAAAFnZAAK"
    "rNosgHhAIJQAAAABaM4BriAAAAABJbhAP//5IAAAAAElThAP//5IAAAAASG4kP//5IAAAAABIU4k"
    "P//5IAAAAAEBuQn//8kAAAABAU5Cf//yQAAAAAEKAAAAAWdkAAqs2nIB4QCCcAAAAAFozgGuIAAA"
    "AAEluEA///kgAAAAASG4kP//5IAAAAABIbkQ///kgAAAAAEBuZH//8kAAAABIbmY///kgAAAAAEK"
    "AAAAASW4QD//+SAAAAABIbiI///kgAAAAAEKAAAAAWdkAAqs23EAAAABaM4BriAAAAABJbhAP//5"
    "IAAAAAEhuMD//+SAAAAAAQG5If//yQAAAAEhuQD//+SAAAAAAQoAAAABZ2QACqzacgHhAIJQAAAA"
    "AWjOAa4gAAAAASW4QH//+SAAAAABIbiE///kgAAAAAEhuQj//+SAAAAAAQoAAAABZ2QACqzdyAeE"
    "AhwAAAABaM4BriAAAAABJbhAP//5IAAAAAEluCAP//5IAAAAASW4QD//+SAAAAABCgAAAAFnbhAK"
    "rN3EAAAAAWjOAa4gAAAAASW4QD//+SAAAAABJbggD//+SAAAAAEKAAAAAWdkAAqs2yxAAAAAAWjO"
    "Aa4gAAAAASW4QD//+SAAAAABJU4QD//+SAAAAAEhuIj//+SAAAAAASG5EP//5IAAAAABIU5EP//5"
    "IAAAAAEBuY3//8k="
;

static int decode_base64(const char *src, uint8_t *dst, size_t capacity,
                         size_t *written)
{
    unsigned accumulator = 0;
    unsigned bits = 0;
    size_t out = 0;
    const unsigned char *p = (const unsigned char *)src;

    while (*p) {
        unsigned value;
        if (*p >= 'A' && *p <= 'Z') value = *p - 'A';
        else if (*p >= 'a' && *p <= 'z') value = *p - 'a' + 26u;
        else if (*p >= '0' && *p <= '9') value = *p - '0' + 52u;
        else if (*p == '+') value = 62u;
        else if (*p == '/') value = 63u;
        else if (*p == '=') break;
        else { ++p; continue; }

        accumulator = (accumulator << 6) | value;
        bits += 6u;
        if (bits >= 8u) {
            bits -= 8u;
            if (out >= capacity) return -1;
            dst[out++] = (uint8_t)(accumulator >> bits);
        }
        ++p;
    }
    *written = out;
    return out ? 0 : -1;
}

typedef struct smoke_state {
    unsigned frames;
    unsigned bad_frames;
    uint32_t last_display_order;
    uint32_t min_display_order;
    uint32_t max_display_order;
    int32_t min_poc;
    int32_t max_poc;
    int have_display_order;
    int have_poc;
} smoke_state_t;

static int smoke_frame(const crtube_h264_edge264_frame_t *frame, void *opaque)
{
    smoke_state_t *state = (smoke_state_t *)opaque;
    if (!frame || !frame->y || !frame->cb || !frame->cr ||
        frame->bit_depth_y != 8u || frame->bit_depth_c != 8u ||
        !frame->width || !frame->height ||
        frame->chroma_width != (frame->width + 1u) / 2u ||
        frame->chroma_height != (frame->height + 1u) / 2u ||
        frame->stride_y < frame->width ||
        frame->stride_c < frame->chroma_width) {
        state->bad_frames++;
        crtube_log("H.264 frame validation failure: index=%u frame=%p\n",
                   state->frames, (const void *)frame);
        if (frame != NULL) {
            crtube_log("H.264 invalid frame: y=%p cb=%p cr=%p size=%ux%u chroma=%ux%u stride=%u/%u depth=%u/%u\n",
                       (const void *)frame->y, (const void *)frame->cb,
                       (const void *)frame->cr, frame->width, frame->height,
                       frame->chroma_width, frame->chroma_height,
                       frame->stride_y, frame->stride_c,
                       frame->bit_depth_y, frame->bit_depth_c);
        }
        return -1;
    }
    {
        uint64_t display_poc = (uint64_t)frame->display_poc;
        uint32_t display_order = (uint32_t)(display_poc >> 32);
        int32_t poc = (int32_t)(uint32_t)display_poc;

        /* edge264 packs its monotonic display-order counter in the upper
           32 bits and the AVC PicOrderCnt in the lower 32 bits. */
        if (state->have_display_order &&
            display_order < state->last_display_order) {
            state->bad_frames++;
            crtube_log("H.264 display-order regression: frame=%u previous=%lu current=%lu raw=%08lx%08lx\n",
                       state->frames,
                       (unsigned long)state->last_display_order,
                       (unsigned long)display_order,
                       (unsigned long)(uint32_t)(display_poc >> 32),
                       (unsigned long)(uint32_t)display_poc);
            return -1;
        }
        state->last_display_order = display_order;
        if (!state->have_display_order ||
            display_order < state->min_display_order)
            state->min_display_order = display_order;
        if (!state->have_display_order ||
            display_order > state->max_display_order)
            state->max_display_order = display_order;
        state->have_display_order = 1;

        if (!state->have_poc || poc < state->min_poc)
            state->min_poc = poc;
        if (!state->have_poc || poc > state->max_poc)
            state->max_poc = poc;
        state->have_poc = 1;
    }

    state->frames++;
    if (state->frames <= 3u || (state->frames % 30u) == 0u ||
        state->frames == 116u) {
        uint64_t display_poc = (uint64_t)frame->display_poc;
        crtube_log("H.264 frame[%u]: display_order=%lu poc=%ld raw=%08lx%08lx size=%ux%u chroma=%ux%u stride_y=%u stride_c=%u depth_y=%u depth_c=%u\n",
                   state->frames - 1u,
                   (unsigned long)(uint32_t)(display_poc >> 32),
                   (long)(int32_t)(uint32_t)display_poc,
                   (unsigned long)(uint32_t)(display_poc >> 32),
                   (unsigned long)(uint32_t)display_poc,
                   frame->width, frame->height,
                   frame->chroma_width, frame->chroma_height,
                   frame->stride_y, frame->stride_c,
                   frame->bit_depth_y, frame->bit_depth_c);
    }
    return 0;
}

/* Runs a real Annex-B decode on the EE. The fixture is the same 116-frame
   stream used by the host adapter regression test. */
int crtube_h264_edge264_runtime_smoke(unsigned *frames, size_t *peak_bytes)
{
    crtube_h264_edge264_t *decoder = NULL;
    crtube_h264_edge264_memory_stats_t stats;
    smoke_state_t state;
    uint8_t *input = NULL;
    size_t input_size = 0;
    int result = -1;

    memset(&state, 0, sizeof(state));
    memset(&stats, 0, sizeof(stats));
    crtube_log("H.264 smoke: starting; fixture_b64_bytes=%lu\n",
               (unsigned long)sizeof(fixture_b64));
    if (frames) *frames = 0;
    if (peak_bytes) *peak_bytes = 0;

    crtube_log("H.264 smoke: allocating fixture buffer\n");
    input = (uint8_t *)malloc(sizeof(fixture_b64));
    if (!input) {
        crtube_log("H.264 smoke: fixture allocation failed\n");
        return -1;
    }
    if (decode_base64(fixture_b64, input, sizeof(fixture_b64),
                      &input_size) != 0) {
        crtube_log("H.264 smoke: base64 conversion failed\n");
        goto done;
    }
    crtube_log("H.264 smoke: Annex-B fixture bytes=%lu\n",
               (unsigned long)input_size);

    crtube_log("H.264 smoke: creating decoder\n");
    decoder = crtube_h264_edge264_create();
    if (!decoder) {
        result = -2;
        crtube_log("H.264 smoke: decoder creation failed\n");
        goto done;
    }
    crtube_log("H.264 smoke: decoder created; push starting\n");

    result = crtube_h264_edge264_push_annexb(
        decoder, input, input_size, smoke_frame, &state);
    crtube_log("H.264 smoke: push emitted=%d frames=%u invalid=%u\n",
               result, state.frames, state.bad_frames);
    if (result < 0) {
        result = -3;
        goto done;
    }
    crtube_log("H.264 smoke: finish/drain starting\n");
    result = crtube_h264_edge264_finish(decoder, smoke_frame, &state);
    crtube_log("H.264 smoke: finish emitted=%d frames=%u invalid=%u\n",
               result, state.frames, state.bad_frames);
    if (result < 0 || state.bad_frames || state.frames != 116u ||
        !state.have_display_order || !state.have_poc) {
        result = -4;
        goto done;
    }
    crtube_log("H.264 presentation: display_order=%lu..%lu last=%lu poc=%ld..%ld monotonic=PASS\n",
               (unsigned long)state.min_display_order,
               (unsigned long)state.max_display_order,
               (unsigned long)state.last_display_order,
               (long)state.min_poc, (long)state.max_poc);

    crtube_h264_edge264_get_memory_stats(decoder, &stats);
    crtube_log("H.264 memory: current=%lu peak=%lu limit=%lu allocations=%u failures=%u\n",
               (unsigned long)stats.current_bytes,
               (unsigned long)stats.peak_bytes,
               (unsigned long)stats.allocation_limit,
               stats.allocation_count, stats.allocation_failures);
    if (!stats.allocation_count || !stats.peak_bytes ||
        stats.allocation_failures ||
        stats.peak_bytes > stats.allocation_limit ||
        stats.current_bytes > stats.peak_bytes) {
        result = -5;
        goto done;
    }

    if (frames) *frames = state.frames;
    if (peak_bytes) *peak_bytes = stats.peak_bytes;
    result = 0;

done:
    crtube_log("H.264 smoke: cleanup result=%d frames=%u invalid=%u\n",
               result, state.frames, state.bad_frames);
    crtube_h264_edge264_destroy(decoder);
    free(input);
    crtube_log("H.264 smoke: decoder and fixture released\n");
    return result;
}
