#ifndef CRTUBE_H264_MP4_H
#define CRTUBE_H264_MP4_H

#include <stddef.h>
#include <stdint.h>

#define CRTUBE_MP4_MAX_SPS 32
#define CRTUBE_MP4_MAX_PPS 256

typedef struct crtube_mp4_stsc_entry {
    uint32_t first_chunk;
    uint32_t samples_per_chunk;
    uint32_t sample_description_index;
} crtube_mp4_stsc_entry_t;

typedef struct crtube_mp4 {
    const uint8_t *data;
    size_t size;
    const uint8_t *avcc;
    size_t avcc_size;
    const uint8_t *stsz;
    uint32_t sample_count;
    uint32_t default_sample_size;
    const uint8_t *chunk_offsets;
    uint32_t chunk_count;
    unsigned chunk_offset_width;
    const uint8_t *stsc;
    uint32_t stsc_count;
    uint32_t nal_length_size;
    uint32_t width;
    uint32_t height;
    uint32_t video_track_found;
} crtube_mp4_t;

typedef struct crtube_mp4_sample {
    const uint8_t *data;
    size_t size;
    uint32_t index;
} crtube_mp4_sample_t;

int crtube_mp4_open_memory(crtube_mp4_t *mp4, const uint8_t *data, size_t size);
int crtube_mp4_get_sample(const crtube_mp4_t *mp4, uint32_t index,
                          crtube_mp4_sample_t *sample);
int crtube_mp4_get_sps(const crtube_mp4_t *mp4, unsigned index,
                       const uint8_t **nal, size_t *size);
int crtube_mp4_get_pps(const crtube_mp4_t *mp4, unsigned index,
                       const uint8_t **nal, size_t *size);
int crtube_mp4_write_parameter_sets_annexb(const crtube_mp4_t *mp4,
                                           uint8_t *output, size_t capacity,
                                           size_t *output_size);
int crtube_mp4_sample_to_annexb(const crtube_mp4_t *mp4,
                                const crtube_mp4_sample_t *sample,
                                uint8_t *output, size_t capacity,
                                size_t *output_size);

#endif
