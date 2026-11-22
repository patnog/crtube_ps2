#ifndef CRTUBE_H264_EDGE264_H
#define CRTUBE_H264_EDGE264_H

#include <stddef.h>
#include <stdint.h>

#include "h264_mp4.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct crtube_h264_edge264 crtube_h264_edge264_t;

typedef struct crtube_h264_edge264_memory_stats {
    size_t current_bytes;
    size_t peak_bytes;
    size_t allocation_limit;
    unsigned allocation_count;
    unsigned allocation_failures;
} crtube_h264_edge264_memory_stats_t;

typedef struct crtube_h264_edge264_frame {
    const uint8_t *y;
    const uint8_t *cb;
    const uint8_t *cr;
    unsigned width;
    unsigned height;
    unsigned chroma_width;
    unsigned chroma_height;
    unsigned stride_y;
    unsigned stride_c;
    unsigned bit_depth_y;
    unsigned bit_depth_c;
    int64_t display_poc;
} crtube_h264_edge264_frame_t;

/* The callback is synchronous. Frame planes remain valid only until the
   decoder is called again or destroyed. Return non-zero to stop decoding. */
typedef int (*crtube_h264_edge264_frame_cb)(
    const crtube_h264_edge264_frame_t *frame, void *opaque);

/* A zero memory limit means no explicit cap. The limit applies to edge264
   decoded-frame/ macroblock pools, not the decoder object or input scratch. */
crtube_h264_edge264_t *crtube_h264_edge264_create_with_memory_limit(
    size_t allocation_limit);
crtube_h264_edge264_t *crtube_h264_edge264_create(void);
void crtube_h264_edge264_get_memory_stats(
    const crtube_h264_edge264_t *decoder,
    crtube_h264_edge264_memory_stats_t *stats);
void crtube_h264_edge264_destroy(crtube_h264_edge264_t *decoder);
void crtube_h264_edge264_reset(crtube_h264_edge264_t *decoder);

/* Experimental EE boot-time decode smoke test; returns zero on success. */
int crtube_h264_edge264_runtime_smoke(unsigned *frames, size_t *peak_bytes);

/* Input must be Annex-B (start-code delimited) AVC. All NAL unit types are
   passed to edge264, including SPS/PPS/SEI/AUD. */
int crtube_h264_edge264_push_annexb(
    crtube_h264_edge264_t *decoder, const uint8_t *data, size_t size,
    crtube_h264_edge264_frame_cb callback, void *opaque);

/* Drain frames that are already display-ready without ending the sequence. */
int crtube_h264_edge264_drain(
    crtube_h264_edge264_t *decoder,
    crtube_h264_edge264_frame_cb callback, void *opaque);

/* Signal end of stream and emit delayed/reordered frames. */
int crtube_h264_edge264_finish(
    crtube_h264_edge264_t *decoder,
    crtube_h264_edge264_frame_cb callback, void *opaque);

/* MP4 adapter. The caller owns scratch; it must be large enough for the
   converted sample or parameter-set sequence. Call open_mp4 once per track,
   then decode samples in decode order, and finish at end of stream. */
int crtube_h264_edge264_open_mp4(
    crtube_h264_edge264_t *decoder, const crtube_mp4_t *mp4,
    uint8_t *scratch, size_t scratch_capacity);

int crtube_h264_edge264_decode_mp4_sample(
    crtube_h264_edge264_t *decoder, const crtube_mp4_t *mp4,
    unsigned sample_index, uint8_t *scratch, size_t scratch_capacity,
    crtube_h264_edge264_frame_cb callback, void *opaque);

#ifdef __cplusplus
}
#endif

#endif
