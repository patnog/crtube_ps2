#ifndef CRTUBE_H264_YUV_H
#define CRTUBE_H264_YUV_H

#include <stddef.h>
#include <stdint.h>

/* Convert 8-bit planar YUV420 to the GS CT32 byte layout (0x80BBGGRR).
   Destination stride is measured in uint32_t pixels. */
int crtube_h264_yuv420_to_ct32(
    const uint8_t *y, const uint8_t *cb, const uint8_t *cr,
    unsigned width, unsigned height, unsigned stride_y, unsigned stride_c,
    unsigned int *destination, unsigned destination_stride);

#endif
