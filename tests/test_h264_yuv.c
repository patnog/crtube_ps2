#include "h264_yuv.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned clamp_ref(int value)
{
    if (value < 0) return 0u;
    if (value > 255) return 255u;
    return (unsigned)value;
}

static uint32_t reference_pixel(unsigned y, unsigned cb, unsigned cr)
{
    int c = (int)y - 16;
    int u = (int)cb - 128;
    int v = (int)cr - 128;
    unsigned r, g, b;
    if (c < 0) c = 0;
    r = clamp_ref((298 * c + 409 * v + 128) >> 8);
    g = clamp_ref((298 * c - 100 * u - 208 * v + 128) >> 8);
    b = clamp_ref((298 * c + 516 * u + 128) >> 8);
    return 0x80000000u | (b << 16) | (g << 8) | r;
}

static uint32_t random_state = 0x6d2b79f5u;
static unsigned random_byte(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state & 255u;
}

int main(void)
{
    enum { W = 321, H = 241, SY = 336, SC = 176, SD = 336 };
    uint8_t *y = (uint8_t *)malloc(SY * H);
    uint8_t *cb = (uint8_t *)malloc(SC * ((H + 1) / 2));
    uint8_t *cr = (uint8_t *)malloc(SC * ((H + 1) / 2));
    uint32_t *dst = (uint32_t *)malloc(SD * H * sizeof(*dst));
    unsigned row, col;
    if (!y || !cb || !cr || !dst) return 2;

    for (row = 0; row < H; ++row)
        for (col = 0; col < SY; ++col) y[row * SY + col] = random_byte();
    for (row = 0; row < (H + 1) / 2; ++row)
        for (col = 0; col < SC; ++col) {
            cb[row * SC + col] = random_byte();
            cr[row * SC + col] = random_byte();
        }
    memset(dst, 0xa5, SD * H * sizeof(*dst));

    if (crtube_h264_yuv420_to_ct32(y, cb, cr, W, H, SY, SC,
                                   dst, SD) != 0)
        return 3;
    for (row = 0; row < H; ++row) {
        for (col = 0; col < W; ++col) {
            uint32_t expected = reference_pixel(
                y[row * SY + col], cb[(row / 2) * SC + col / 2],
                cr[(row / 2) * SC + col / 2]);
            if (dst[row * SD + col] != expected) {
                fprintf(stderr, "pixel mismatch at %u,%u: %08x != %08x\n",
                        col, row, dst[row * SD + col], expected);
                return 4;
            }
        }
        for (col = W; col < SD; ++col)
            if (dst[row * SD + col] != 0xa5a5a5a5u) return 5;
    }

    if (crtube_h264_yuv420_to_ct32(NULL, cb, cr, W, H, SY, SC,
                                   dst, SD) != -1 ||
        crtube_h264_yuv420_to_ct32(y, cb, cr, W, H, W - 1, SC,
                                   dst, SD) != -1 ||
        crtube_h264_yuv420_to_ct32(y, cb, cr, W, H, SY, SC,
                                   dst, W - 1) != -1)
        return 6;

    free(dst); free(cr); free(cb); free(y);
    puts("H.264 YUV420 CT32 conversion: PASS (321x241 randomized, padded strides)");
    return 0;
}
