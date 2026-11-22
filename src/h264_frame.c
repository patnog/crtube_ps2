#include "h264_decoder.h"

#include <limits.h>
#include <string.h>

static unsigned clip_byte(int value)
{
    if (value < 0) return 0u;
    if (value > 255) return 255u;
    return (unsigned)value;
}

size_t crtube_h264_frame_bytes(unsigned width, unsigned height)
{
    size_t pixels;
    if (!width || !height || (width & 15u) || (height & 15u) ||
        width > CRTUBE_H264_MAX_WIDTH || height > CRTUBE_H264_MAX_HEIGHT)
        return 0;
    if ((size_t)width > SIZE_MAX / (size_t)height)
        return 0;
    pixels = (size_t)width * (size_t)height;
    if (pixels > SIZE_MAX - pixels / 2u)
        return 0;
    return pixels + pixels / 2u;
}

int crtube_h264_frame_init(crtube_h264_frame_t *frame, uint8_t *storage,
                           size_t storage_size, unsigned width,
                           unsigned height)
{
    size_t required = crtube_h264_frame_bytes(width, height);
    size_t luma_size;
    if (!frame || !storage || !required || storage_size < required)
        return -1;
    luma_size = (size_t)width * (size_t)height;
    frame->storage = storage;
    frame->y = storage;
    frame->cb = storage + luma_size;
    frame->cr = frame->cb + luma_size / 4u;
    frame->width = width;
    frame->height = height;
    frame->chroma_width = width / 2u;
    frame->chroma_height = height / 2u;
    frame->storage_size = storage_size;
    return 0;
}

void crtube_h264_frame_clear(crtube_h264_frame_t *frame, uint8_t y,
                             uint8_t cb, uint8_t cr)
{
    size_t luma_size;
    size_t chroma_size;
    if (!frame || !frame->storage || !frame->y || !frame->cb || !frame->cr)
        return;
    luma_size = (size_t)frame->width * frame->height;
    chroma_size = luma_size / 4u;
    memset(frame->y, y, luma_size);
    memset(frame->cb, cb, chroma_size);
    memset(frame->cr, cr, chroma_size);
}

int crtube_h264_frame_put_macroblock(crtube_h264_frame_t *frame,
                                     unsigned mb_x, unsigned mb_y,
                                     const uint8_t luma[256],
                                     const uint8_t cb[64],
                                     const uint8_t cr[64])
{
    unsigned row;
    unsigned x = mb_x * 16u;
    unsigned y = mb_y * 16u;
    unsigned cx = mb_x * 8u;
    unsigned cy = mb_y * 8u;
    if (!frame || !frame->y || !frame->cb || !frame->cr ||
        !luma || !cb || !cr || mb_x >= frame->width / 16u ||
        mb_y >= frame->height / 16u)
        return -1;

    for (row = 0; row < 16u; ++row)
        memcpy(frame->y + (size_t)(y + row) * frame->width + x,
               luma + row * 16u, 16u);
    for (row = 0; row < 8u; ++row) {
        memcpy(frame->cb + (size_t)(cy + row) * frame->chroma_width + cx,
               cb + row * 8u, 8u);
        memcpy(frame->cr + (size_t)(cy + row) * frame->chroma_width + cx,
               cr + row * 8u, 8u);
    }
    return 0;
}

int crtube_h264_frame_to_rgb24(const crtube_h264_frame_t *frame,
                               uint8_t *rgb, size_t rgb_size)
{
    size_t required;
    unsigned x, y;
    if (!frame || !frame->y || !frame->cb || !frame->cr || !rgb ||
        !frame->width || !frame->height)
        return -1;
    if ((size_t)frame->width > SIZE_MAX / (size_t)frame->height / 3u)
        return -1;
    required = (size_t)frame->width * frame->height * 3u;
    if (rgb_size < required)
        return -1;

    for (y = 0; y < frame->height; ++y) {
        for (x = 0; x < frame->width; ++x) {
            int yy = (int)frame->y[(size_t)y * frame->width + x] - 16;
            int u = (int)frame->cb[(size_t)(y >> 1) * frame->chroma_width + (x >> 1)] - 128;
            int v = (int)frame->cr[(size_t)(y >> 1) * frame->chroma_width + (x >> 1)] - 128;
            int c = yy < 0 ? 0 : yy;
            int r = (298 * c + 409 * v + 128) >> 8;
            int g = (298 * c - 100 * u - 208 * v + 128) >> 8;
            int b = (298 * c + 516 * u + 128) >> 8;
            size_t out = ((size_t)y * frame->width + x) * 3u;
            rgb[out + 0u] = (uint8_t)clip_byte(r);
            rgb[out + 1u] = (uint8_t)clip_byte(g);
            rgb[out + 2u] = (uint8_t)clip_byte(b);
        }
    }
    return 0;
}
