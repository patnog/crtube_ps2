#include "h264_edge264.h"

#include <errno.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#if defined(CRTUBE_EDGE264_PROFILE)
#include <timer.h>
#endif

#include "edge264.h"

#if defined(CRTUBE_EDGE264_PROFILE)
uint64_t crtube_edge264_profile_ticks(void)
{
    return (uint64_t)GetTimerSystemTime();
}
#endif


typedef struct crtube_h264_edge264_allocator {
    size_t current_bytes, peak_bytes, allocation_limit;
    unsigned allocation_count, allocation_failures;
} crtube_h264_edge264_allocator_t;

typedef struct crtube_h264_edge264_allocation {
    void *raw;
    size_t bytes;
} crtube_h264_edge264_allocation_t;

struct crtube_h264_edge264 {
    Edge264Decoder *core;
    uint8_t *input_buffer;
    size_t input_capacity;
    unsigned frames_emitted;
    crtube_h264_edge264_allocator_t allocator;
};

static void edge264_frame_alloc(
    void **samples, unsigned samples_size, void **mbs, unsigned mbs_size,
    int errno_on_fail, void *opaque)
{
    crtube_h264_edge264_allocator_t *a =
        (crtube_h264_edge264_allocator_t *)opaque;
    const size_t alignment = 64u;
    size_t samples_span;
    size_t total;
    size_t overhead = sizeof(crtube_h264_edge264_allocation_t) + alignment - 1u;
    uintptr_t aligned;
    void *raw;
    crtube_h264_edge264_allocation_t *record;
    (void)errno_on_fail;
    *samples = NULL;
    *mbs = NULL;
    if ((size_t)samples_size > SIZE_MAX - (alignment - 1u)) {
        if (a) a->allocation_failures++;
        return;
    }
    samples_span = ((size_t)samples_size + alignment - 1u) &
                   ~(alignment - 1u);
    if (samples_span > SIZE_MAX - (size_t)mbs_size) {
        if (a) a->allocation_failures++;
        return;
    }
    total = samples_span + (size_t)mbs_size;
    if (!a || total < samples_size || total > SIZE_MAX - overhead ||
        (a->allocation_limit &&
         (a->current_bytes > a->allocation_limit ||
          total > a->allocation_limit - a->current_bytes))) {
        if (a) a->allocation_failures++;
        return;
    }
    raw = malloc(total + overhead);
    if (!raw) {
        a->allocation_failures++;
        return;
    }
    aligned = ((uintptr_t)raw + sizeof(*record) + alignment - 1u) &
              ~(uintptr_t)(alignment - 1u);
    record = (crtube_h264_edge264_allocation_t *)(aligned - sizeof(*record));
    record->raw = raw;
    record->bytes = total;
    *samples = (void *)aligned;
    *mbs = (uint8_t *)*samples + samples_span;
    a->current_bytes += total;
    if (a->current_bytes > a->peak_bytes) a->peak_bytes = a->current_bytes;
    a->allocation_count++;
}

static void edge264_frame_free(void *samples, void *mbs, void *opaque)
{
    crtube_h264_edge264_allocator_t *a =
        (crtube_h264_edge264_allocator_t *)opaque;
    crtube_h264_edge264_allocation_t *record;
    (void)mbs;
    if (!samples || !a) return;
    record = (crtube_h264_edge264_allocation_t *)
        ((uintptr_t)samples - sizeof(*record));
    if (record->bytes <= a->current_bytes) a->current_bytes -= record->bytes;
    else a->current_bytes = 0;
    free(record->raw);
}

static const uint8_t *find_annexb_start_code(const uint8_t *data,
                                              const uint8_t *end,
                                              unsigned *prefix_size)
{
    const uint8_t *p;
    if (!data || !end || !prefix_size || data >= end)
        return end;
    for (p = data; (size_t)(end - p) >= 3u; ++p) {
        if (p[0] != 0 || p[1] != 0)
            continue;
        if ((size_t)(end - p) >= 4u && p[2] == 0 && p[3] == 1) {
            *prefix_size = 4u;
            return p;
        }
        if (p[2] == 1) {
            *prefix_size = 3u;
            return p;
        }
    }
    return end;
}

static int emit_ready_frames(crtube_h264_edge264_t *decoder,
                             crtube_h264_edge264_frame_cb callback,
                             void *opaque)
{
    Edge264Frame decoded;
    int result;
    if (!decoder || !decoder->core)
        return -1;

    while ((result = edge264_get_frame(decoder->core, &decoded, 0)) == 0) {
        crtube_h264_edge264_frame_t frame;
        if (decoded.bit_depth_Y != 8 || decoded.bit_depth_C != 8 ||
            decoded.width_Y <= 0 || decoded.height_Y <= 0 ||
            decoded.width_C <= 0 || decoded.height_C <= 0 ||
            decoded.stride_Y < decoded.width_Y ||
            decoded.stride_C < decoded.width_C ||
            !decoded.samples[0] || !decoded.samples[1] || !decoded.samples[2])
            return -2;

        /* edge264_get_frame already offsets samples[] for frame cropping and
           reports visible dimensions in width_Y/height_Y/width_C/height_C.
           Keep the coded strides; do not apply frame_crop_offsets twice. */
        memset(&frame, 0, sizeof(frame));
        frame.y = decoded.samples[0];
        frame.cb = decoded.samples[1];
        frame.cr = decoded.samples[2];
        frame.width = (unsigned)decoded.width_Y;
        frame.height = (unsigned)decoded.height_Y;
        frame.chroma_width = (unsigned)decoded.width_C;
        frame.chroma_height = (unsigned)decoded.height_C;
        frame.stride_y = (unsigned)decoded.stride_Y;
        frame.stride_c = (unsigned)decoded.stride_C;
        frame.bit_depth_y = (unsigned)decoded.bit_depth_Y;
        frame.bit_depth_c = (unsigned)decoded.bit_depth_C;
        frame.display_poc = decoded.display_poc;

        if (!frame.width || !frame.height ||
            frame.chroma_width == 0 || frame.chroma_height == 0)
            return -2;

        decoder->frames_emitted++;
        if (callback && callback(&frame, opaque) != 0)
            return -3;
    }

    return result == ENOMSG ? 0 : -4;
}

crtube_h264_edge264_t *crtube_h264_edge264_create_with_memory_limit(
    size_t allocation_limit)
{
    crtube_h264_edge264_t *decoder =
        (crtube_h264_edge264_t *)calloc(1, sizeof(*decoder));
    if (!decoder) return NULL;
    decoder->allocator.allocation_limit = allocation_limit;
    /* n_threads=0: no pthread locks, condition variables or worker threads. */
    decoder->core = edge264_alloc(0, NULL, NULL, 0,
                                  edge264_frame_alloc, edge264_frame_free,
                                  &decoder->allocator);
    if (!decoder->core) {
        free(decoder);
        return NULL;
    }
    return decoder;
}

crtube_h264_edge264_t *crtube_h264_edge264_create(void)
{
    return crtube_h264_edge264_create_with_memory_limit(12u * 1024u * 1024u);
}

void crtube_h264_edge264_get_memory_stats(
    const crtube_h264_edge264_t *decoder,
    crtube_h264_edge264_memory_stats_t *stats)
{
    if (!stats) return;
    memset(stats, 0, sizeof(*stats));
    if (!decoder) return;
    stats->current_bytes = decoder->allocator.current_bytes;
    stats->peak_bytes = decoder->allocator.peak_bytes;
    stats->allocation_limit = decoder->allocator.allocation_limit;
    stats->allocation_count = decoder->allocator.allocation_count;
    stats->allocation_failures = decoder->allocator.allocation_failures;
}

void crtube_h264_edge264_destroy(crtube_h264_edge264_t *decoder)
{
    if (!decoder)
        return;
    edge264_free(&decoder->core);
    free(decoder->input_buffer);
    free(decoder);
}

void crtube_h264_edge264_reset(crtube_h264_edge264_t *decoder)
{
    if (!decoder || !decoder->core)
        return;
    edge264_flush(decoder->core);
    decoder->frames_emitted = 0;
}

int crtube_h264_edge264_drain(
    crtube_h264_edge264_t *decoder,
    crtube_h264_edge264_frame_cb callback, void *opaque)
{
    return emit_ready_frames(decoder, callback, opaque);
}

int crtube_h264_edge264_push_annexb(
    crtube_h264_edge264_t *decoder, const uint8_t *data, size_t size,
    crtube_h264_edge264_frame_cb callback, void *opaque)
{
    uint8_t *padded;
    const uint8_t *cursor, *end;
    unsigned decoded_before;
    int final_result = -1;

    if (!decoder || !decoder->core || !data || !size ||
        size > SIZE_MAX - 16u)
        return -1;

    /* edge264's vector bit reader may load the aligned 16-byte word that
       contains the final NAL bytes. Give it readable zero padding so callers
       can pass ordinary MP4/network buffers without hidden over-read hazards. */
    if (size + 16u > decoder->input_capacity) {
        uint8_t *grown = (uint8_t *)memalign(64u, size + 16u);
        if (!grown)
            return -6;
        free(decoder->input_buffer);
        decoder->input_buffer = grown;
        decoder->input_capacity = size + 16u;
    }
    padded = decoder->input_buffer;
    memcpy(padded, data, size);
    memset(padded + size, 0, 16u);

    cursor = padded;
    end = padded + size;
    decoded_before = decoder->frames_emitted;

    while (cursor < end) {
        unsigned prefix_size = 0;
        const uint8_t *start_code =
            find_annexb_start_code(cursor, end, &prefix_size);
        const uint8_t *nal;
        const uint8_t *nal_end;
        const uint8_t *next;
        int result;

        if (start_code == end)
            break;
        nal = start_code + prefix_size;
        if (nal > end) {
            final_result = -1;
            goto done;
        }
        next = find_annexb_start_code(nal, end, &prefix_size);
        nal_end = next;
        while (nal_end > nal && nal_end[-1] == 0)
            --nal_end;
        if (nal_end <= nal) {
            cursor = next < end ? next : end;
            continue;
        }

        do {
            result = edge264_decode_NAL(decoder->core, nal, nal_end,
                                        NULL, NULL);
            if (emit_ready_frames(decoder, callback, opaque) < 0) {
                final_result = -3;
                goto done;
            }
        } while (result == ENOBUFS);

        if (result == ENOTSUP) {
            final_result = -2;
            goto done;
        }
        if (result == EBADMSG) {
            final_result = -4;
            goto done;
        }
        if (result != 0) {
            final_result = -5;
            goto done;
        }

        cursor = next < end ? next : end;
    }

    final_result = (int)(decoder->frames_emitted - decoded_before);

done:
    return final_result;
}

int crtube_h264_edge264_finish(
    crtube_h264_edge264_t *decoder,
    crtube_h264_edge264_frame_cb callback, void *opaque)
{
    static const uint8_t empty = 0;
    unsigned decoded_before;
    int result;
    if (!decoder || !decoder->core)
        return -1;

    decoded_before = decoder->frames_emitted;
    do {
        result = edge264_decode_NAL(decoder->core, &empty, &empty, NULL, NULL);
        if (emit_ready_frames(decoder, callback, opaque) < 0)
            return -3;
    } while (result == ENOBUFS);
    if (result != ENODATA && result != 0)
        return -2;
    return (int)(decoder->frames_emitted - decoded_before);
}

int crtube_h264_edge264_open_mp4(
    crtube_h264_edge264_t *decoder, const crtube_mp4_t *mp4,
    uint8_t *scratch, size_t scratch_capacity)
{
    size_t parameter_sets_size = 0;
    int result;
    if (!decoder || !mp4 || !scratch || !scratch_capacity)
        return -1;
    result = crtube_mp4_write_parameter_sets_annexb(
        mp4, scratch, scratch_capacity, &parameter_sets_size);
    if (result < 0 || parameter_sets_size == 0)
        return -2;
    result = crtube_h264_edge264_push_annexb(
        decoder, scratch, parameter_sets_size, NULL, NULL);
    return result < 0 ? result : 0;
}

int crtube_h264_edge264_decode_mp4_sample(
    crtube_h264_edge264_t *decoder, const crtube_mp4_t *mp4,
    unsigned sample_index, uint8_t *scratch, size_t scratch_capacity,
    crtube_h264_edge264_frame_cb callback, void *opaque)
{
    crtube_mp4_sample_t sample;
    size_t annexb_size = 0;
    int result;
    if (!decoder || !mp4 || !scratch || !scratch_capacity)
        return -1;
    if (crtube_mp4_get_sample(mp4, sample_index, &sample) < 0)
        return -2;
    if (crtube_mp4_sample_to_annexb(
            mp4, &sample, scratch, scratch_capacity, &annexb_size) < 0)
        return -3;
    result = crtube_h264_edge264_push_annexb(
        decoder, scratch, annexb_size, callback, opaque);
    if (result < 0)
        return result;
    return crtube_h264_edge264_drain(decoder, callback, opaque) < 0
        ? -4 : result;
}
