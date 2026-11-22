#ifndef CRTUBE_H264_PLAYBACK_H
#define CRTUBE_H264_PLAYBACK_H

#include "h264_decoder.h"
#include "h264_mp4.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Caller-owned scratch for decoding one MP4 AVC sample (normally one frame).
   annexb_capacity must fit the largest converted sample; rbsp_capacity must
   fit its largest NAL payload. Macroblock storage must hold width*height/256
   entries. No heap allocation is performed by the playback controller. */
typedef struct crtube_h264_playback_workspace {
    uint8_t *annexb;
    size_t annexb_capacity;
    uint8_t *rbsp;
    size_t rbsp_capacity;
    crtube_h264_i4x4_mb_data_t *macroblocks;
    unsigned macroblock_capacity;
} crtube_h264_playback_workspace_t;

typedef struct crtube_h264_playback_info {
    unsigned width;
    unsigned height;
    unsigned frame_num;
    unsigned first_mb_in_slice;
    unsigned slice_type;
    unsigned nal_type;
    unsigned macroblocks_decoded;
    unsigned sample_index;
    int poc;
} crtube_h264_playback_info_t;

/* Decodes one length-prefixed AVC MP4 sample into current. Supports multiple
   non-overlapping raster-ordered slices covering one complete picture,
   Baseline-style CAVLC, 8-bit 4:2:0,
   Intra_16x16 and Intra_4x4 I macroblocks, P_Skip and P_L0_16x16,
   and a single reference frame.
   SPS/PPS are read from avcC. Returns 1 for a decoded frame, 0 for a sample
   without VCL data, -1 for malformed input, -2 for unsupported syntax/state. */
int crtube_h264_decode_mp4_sample(
    const crtube_mp4_t *mp4, unsigned sample_index,
    crtube_h264_frame_t *current, crtube_h264_dpb_t *dpb,
    crtube_h264_playback_workspace_t *workspace,
    crtube_h264_playback_info_t *info);

#ifdef __cplusplus
}
#endif
#endif
