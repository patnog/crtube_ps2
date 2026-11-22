#include "h264_decoder.h"

#include <limits.h>

static const uint8_t zigzag4x4[16] = {
    0, 1, 4, 8,
    5, 2, 3, 6,
    9, 12, 13, 10,
    7, 11, 14, 15
};

void crtube_h264_scan4x4_to_raster(const int16_t scan[16], int16_t raster[16])
{
    unsigned i;
    if (!scan || !raster) return;
    for (i = 0; i < 16; ++i) raster[zigzag4x4[i]] = scan[i];
}

static int checked_stage(int64_t value, int32_t *out)
{
    if (value < INT32_MIN || value > INT32_MAX) return -1;
    *out = (int32_t)value;
    return 0;
}

int crtube_h264_inverse_transform4x4(const int16_t coeff[16], int16_t residual[16])
{
    int32_t tmp[16];
    int32_t a0, a1, a2, a3;
    unsigned row, col;
    if (!coeff || !residual) return -1;

    /* Horizontal pass. */
    for (row = 0; row < 4; ++row) {
        int32_t x0 = coeff[row * 4 + 0];
        int32_t x1 = coeff[row * 4 + 1];
        int32_t x2 = coeff[row * 4 + 2];
        int32_t x3 = coeff[row * 4 + 3];
        a0 = x0 + x2;
        a1 = x0 - x2;
        a2 = (x1 >> 1) - x3;
        a3 = x1 + (x3 >> 1);
        if (checked_stage((int64_t)a0 + a3, &tmp[row * 4 + 0]) < 0 ||
            checked_stage((int64_t)a1 + a2, &tmp[row * 4 + 1]) < 0 ||
            checked_stage((int64_t)a1 - a2, &tmp[row * 4 + 2]) < 0 ||
            checked_stage((int64_t)a0 - a3, &tmp[row * 4 + 3]) < 0) return -1;
    }

    /* Vertical pass and normative rounding by 64. */
    for (col = 0; col < 4; ++col) {
        int64_t x0 = tmp[col + 0];
        int64_t x1 = tmp[col + 4];
        int64_t x2 = tmp[col + 8];
        int64_t x3 = tmp[col + 12];
        int64_t y[4];
        int64_t b0 = x0 + x2;
        int64_t b1 = x0 - x2;
        int64_t b2 = (x1 >> 1) - x3;
        int64_t b3 = x1 + (x3 >> 1);
        y[0] = b0 + b3;
        y[1] = b1 + b2;
        y[2] = b1 - b2;
        y[3] = b0 - b3;
        for (row = 0; row < 4; ++row) {
            int64_t value = (y[row] + 32) >> 6;
            if (value < INT16_MIN || value > INT16_MAX) return -1;
            residual[row * 4 + col] = (int16_t)value;
        }
    }
    return 0;
}

void crtube_h264_add_residual4x4(const uint8_t prediction[16],
                                  const int16_t residual[16], uint8_t output[16])
{
    unsigned i;
    if (!prediction || !residual || !output) return;
    for (i = 0; i < 16; ++i) {
        int value = (int)prediction[i] + (int)residual[i];
        if (value < 0) value = 0;
        else if (value > 255) value = 255;
        output[i] = (uint8_t)value;
    }
}
