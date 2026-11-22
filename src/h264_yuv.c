#include "h264_yuv.h"

static unsigned clamp_u8(int value)
{
    if (value < 0) return 0u;
    if (value > 255) return 255u;
    return (unsigned)value;
}

int crtube_h264_yuv420_to_ct32(
    const uint8_t *y, const uint8_t *cb, const uint8_t *cr,
    unsigned width, unsigned height, unsigned stride_y, unsigned stride_c,
    unsigned int *destination, unsigned destination_stride)
{
    unsigned row, col;

    if (!y || !cb || !cr || !destination || !width || !height ||
        stride_y < width || stride_c < (width + 1u) / 2u ||
        destination_stride < width)
        return -1;

    for (row = 0; row < height; ++row) {
        const uint8_t *yp = y + (size_t)row * stride_y;
        const uint8_t *up = cb + (size_t)(row >> 1) * stride_c;
        const uint8_t *vp = cr + (size_t)(row >> 1) * stride_c;
        unsigned int *dst = destination + (size_t)row * destination_stride;

        /* Chroma contribution is shared by both luma samples in each
           4:2:0 pair. This removes repeated chroma multiplies from the
           per-pixel path while preserving the previous BT.601 rounding. */
        for (col = 0; col < width; col += 2u) {
            int cb_value = (int)up[col >> 1] - 128;
            int cr_value = (int)vp[col >> 1] - 128;
            int red_chroma = 409 * cr_value;
            int green_chroma = -100 * cb_value - 208 * cr_value;
            int blue_chroma = 516 * cb_value;
            unsigned count = width - col;
            unsigned k;

            if (count > 2u) count = 2u;
            for (k = 0; k < count; ++k) {
                int luminance = (int)yp[col + k] - 16;
                int c = luminance > 0 ? luminance : 0;
                unsigned red = clamp_u8((298 * c + red_chroma + 128) >> 8);
                unsigned green = clamp_u8(
                    (298 * c + green_chroma + 128) >> 8);
                unsigned blue = clamp_u8(
                    (298 * c + blue_chroma + 128) >> 8);
                dst[col + k] = 0x80000000u | (blue << 16) |
                               (green << 8) | red;
            }
        }
    }
    return 0;
}
