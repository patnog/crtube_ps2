#include "h264_decoder.h"

#include <limits.h>
#include <string.h>

static const uint8_t chroma_dequant[6] = {10, 11, 13, 14, 16, 18};
static const uint8_t chroma_qp_map[52] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29,
    29, 30, 31, 32, 32, 33, 34, 34, 35, 35, 36, 36,
    37, 37, 37, 38, 38, 38, 39, 39, 39, 39
};

static int clip_sample(int value)
{
    if (value < 0) return 0;
    if (value > 255) return 255;
    return value;
}

static int floor_shift(int value, unsigned shift)
{
    if (shift == 0) return value;
    if (value >= 0) return value >> shift;
    return -(((-value) + ((1 << shift) - 1)) >> shift);
}

unsigned crtube_h264_chroma_qp(unsigned qp_y, int chroma_qp_index_offset)
{
    int index;
    if (qp_y > 51u) return 0;
    index = (int)qp_y + chroma_qp_index_offset;
    if (index < 0) index = 0;
    if (index > 51) index = 51;
    return chroma_qp_map[index];
}

int crtube_h264_inverse_chroma_dc4x4(const int16_t dc_levels[4],
                                      unsigned qp_c, int16_t dc_scaled[4])
{
    int32_t c0, c1, c2, c3;
    int32_t f[4];
    int64_t scale;
    unsigned i;
    if (!dc_levels || !dc_scaled || qp_c > 51u) return -1;

    c0 = dc_levels[0];
    c1 = dc_levels[1];
    c2 = dc_levels[2];
    c3 = dc_levels[3];
    f[0] = c0 + c1 + c2 + c3;
    f[1] = c0 - c1 + c2 - c3;
    f[2] = c0 + c1 - c2 - c3;
    f[3] = c0 - c1 - c2 + c3;
    for (i = 0; i < 4; ++i)
        if (f[i] < INT16_MIN || f[i] > INT16_MAX) return -1;

    scale = chroma_dequant[qp_c % 6u];
    for (i = 0; i < 4; ++i) {
        int64_t value = (int64_t)f[i] * scale;
        if (qp_c >= 6u)
            value <<= (qp_c / 6u) - 1u;
        else
            value = floor_shift((int)value, 1u);
        if (value < INT16_MIN || value > INT16_MAX) return -1;
        dc_scaled[i] = (int16_t)value;
    }
    return 0;
}

static unsigned edge_sum(const uint8_t *edge, unsigned start)
{
    return (unsigned)edge[start] + edge[start + 1u] +
           edge[start + 2u] + edge[start + 3u];
}

static void fill_quad(uint8_t prediction[64], unsigned x0, unsigned y0,
                      uint8_t value)
{
    unsigned x, y;
    for (y = 0; y < 4; ++y)
        for (x = 0; x < 4; ++x)
            prediction[(y0 + y) * 8u + x0 + x] = value;
}

int crtube_h264_intra_predict_chroma8x8(unsigned mode,
    const uint8_t top[8], const uint8_t left[8], uint8_t top_left,
    unsigned have_top, unsigned have_left, unsigned have_top_left,
    uint8_t prediction[64])
{
    unsigned x, y;
    (void)top_left;
    if (!prediction || mode > 3u || have_top > 1u || have_left > 1u ||
        have_top_left > 1u || (have_top && !top) ||
        (have_left && !left)) return -1;

    if (mode == 0u) {
        unsigned top_a = have_top ? edge_sum(top, 0) : 0;
        unsigned top_b = have_top ? edge_sum(top, 4) : 0;
        unsigned left_a = have_left ? edge_sum(left, 0) : 0;
        unsigned left_b = have_left ? edge_sum(left, 4) : 0;
        uint8_t dc;

        if (have_top && have_left)
            dc = (uint8_t)((top_a + left_a + 4u) >> 3);
        else if (have_top)
            dc = (uint8_t)((top_a + 2u) >> 2);
        else if (have_left)
            dc = (uint8_t)((left_a + 2u) >> 2);
        else
            dc = 128;
        fill_quad(prediction, 0, 0, dc);

        if (have_top)
            dc = (uint8_t)((top_b + 2u) >> 2);
        else if (have_left)
            dc = (uint8_t)((left_a + 2u) >> 2);
        else
            dc = 128;
        fill_quad(prediction, 4, 0, dc);

        if (have_left)
            dc = (uint8_t)((left_b + 2u) >> 2);
        else if (have_top)
            dc = (uint8_t)((top_a + 2u) >> 2);
        else
            dc = 128;
        fill_quad(prediction, 0, 4, dc);

        if (have_top && have_left)
            dc = (uint8_t)((top_b + left_b + 4u) >> 3);
        else if (have_top)
            dc = (uint8_t)((top_b + 2u) >> 2);
        else if (have_left)
            dc = (uint8_t)((left_b + 2u) >> 2);
        else
            dc = 128;
        fill_quad(prediction, 4, 4, dc);
        return 0;
    }

    if (mode == 1u) {
        if (!have_left) return -2;
        for (y = 0; y < 8; ++y)
            for (x = 0; x < 8; ++x)
                prediction[y * 8u + x] = left[y];
        return 0;
    }

    if (mode == 2u) {
        if (!have_top) return -2;
        for (y = 0; y < 8; ++y)
            for (x = 0; x < 8; ++x)
                prediction[y * 8u + x] = top[x];
        return 0;
    }

    if (!have_top || !have_left || !have_top_left) return -2;
    {
        int h = 0, v = 0;
        int a, b, c;
        for (x = 0; x < 4; ++x) {
            h += (int)(x + 1u) * ((int)top[4u + x] -
                    (int)(x == 3u ? top_left : top[2u - x]));
            v += (int)(x + 1u) * ((int)left[4u + x] -
                    (int)(x == 3u ? top_left : left[2u - x]));
        }
        a = 16 * ((int)top[7] + (int)left[7]);
        b = floor_shift(17 * h + 16, 5);
        c = floor_shift(17 * v + 16, 5);
        for (y = 0; y < 8; ++y) {
            for (x = 0; x < 8; ++x) {
                int value = floor_shift(a + b * ((int)x - 3) +
                                        c * ((int)y - 3) + 16, 5);
                prediction[y * 8u + x] = (uint8_t)clip_sample(value);
            }
        }
    }
    return 0;
}

int crtube_h264_reconstruct_chroma8x8(unsigned mode,
    const uint8_t top[8], const uint8_t left[8], uint8_t top_left,
    unsigned have_top, unsigned have_left, unsigned have_top_left,
    const int16_t dc_levels[4], const int16_t ac_levels[4][15],
    unsigned qp_c, uint8_t output[64])
{
    uint8_t prediction[64];
    int16_t dc_scaled[4];
    unsigned block;
    if (!dc_levels || !ac_levels || !output || qp_c > 51u) return -1;
    if (crtube_h264_intra_predict_chroma8x8(mode, top, left, top_left,
            have_top, have_left, have_top_left, prediction) < 0)
        return -2;
    if (crtube_h264_inverse_chroma_dc4x4(dc_levels, qp_c, dc_scaled) < 0)
        return -1;

    for (block = 0; block < 4; ++block) {
        int16_t scan[16] = {0};
        int16_t raster[16];
        int16_t quantized[16];
        int16_t residual[16];
        uint8_t pred_block[16], out_block[16];
        unsigned x0 = (block & 1u) * 4u;
        unsigned y0 = (block >> 1) * 4u;
        unsigned x, y;

        for (x = 0; x < 15; ++x)
            scan[x + 1u] = ac_levels[block][x];
        crtube_h264_scan4x4_to_raster(scan, raster);
        if (crtube_h264_inverse_quant4x4(raster, qp_c, NULL, quantized) < 0)
            return -1;
        quantized[0] = dc_scaled[block];
        if (crtube_h264_inverse_transform4x4(quantized, residual) < 0)
            return -1;

        for (y = 0; y < 4; ++y)
            for (x = 0; x < 4; ++x)
                pred_block[y * 4u + x] = prediction[(y0 + y) * 8u + x0 + x];
        crtube_h264_add_residual4x4(pred_block, residual, out_block);
        for (y = 0; y < 4; ++y)
            for (x = 0; x < 4; ++x)
                output[(y0 + y) * 8u + x0 + x] = out_block[y * 4u + x];
    }
    return 0;
}
