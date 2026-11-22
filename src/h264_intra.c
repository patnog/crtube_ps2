#include "h264_decoder.h"

#include <limits.h>
#include <string.h>

/* H.264 4x4 inverse-quantization factors. The three position classes are
   selected by (x parity, y parity): even/even, mixed, and odd/odd. */
static const uint8_t dequant_coeff[6][3] = {
    {10, 13, 16}, {11, 14, 18}, {13, 16, 20},
    {14, 18, 23}, {16, 20, 25}, {18, 23, 29}
};

static unsigned position_class(unsigned x, unsigned y)
{
    if (!(x & 1u) && !(y & 1u)) return 0;
    if ((x & 1u) && (y & 1u)) return 2;
    return 1;
}

int crtube_h264_inverse_quant4x4(const int16_t levels[16], unsigned qp,
                                  const uint8_t scaling[16],
                                  int16_t coeff[16])
{
    unsigned i, qdiv, qrem;
    if (!levels || !coeff || qp > 51) return -1;
    qdiv = qp / 6;
    qrem = qp % 6;

    for (i = 0; i < 16; ++i) {
        unsigned x = i & 3u;
        unsigned y = i >> 2;
        unsigned scale = scaling ? scaling[i] : 16u;
        int64_t value = (int64_t)levels[i] *
                        dequant_coeff[qrem][position_class(x, y)] * scale;
        if (qdiv >= 4) {
            value *= (int64_t)1 << (qdiv - 4);
        } else {
            unsigned shift = 4 - qdiv;
            value = (value + ((int64_t)1 << (shift - 1))) >> shift;
        }
        if (value < INT16_MIN || value > INT16_MAX) return -1;
        coeff[i] = (int16_t)value;
    }
    return 0;
}

static uint8_t avg2(uint8_t a, uint8_t b)
{
    return (uint8_t)(((unsigned)a + b + 1u) >> 1);
}

static uint8_t avg3(uint8_t a, uint8_t b, uint8_t c)
{
    return (uint8_t)(((unsigned)a + 2u * b + c + 2u) >> 2);
}

static void predict_vertical_right(const uint8_t top[5],
                                   const uint8_t left[4], uint8_t top_left,
                                   uint8_t out[16])
{
    uint8_t p[4][4];
    p[0][0] = avg3(top_left, top[0], top[1]);
    p[0][1] = avg3(top[0], top[1], top[2]);
    p[0][2] = avg3(top[1], top[2], top[3]);
    p[0][3] = avg3(top[2], top[3], top[4]);

    p[1][0] = avg3(left[0], top_left, top[0]);
    p[1][1] = p[0][0];
    p[1][2] = p[0][1];
    p[1][3] = p[0][2];

    p[2][0] = avg3(left[1], left[0], top_left);
    p[2][1] = p[1][0];
    p[2][2] = p[0][0];
    p[2][3] = p[0][1];

    p[3][0] = avg3(left[2], left[1], left[0]);
    p[3][1] = p[2][0];
    p[3][2] = p[1][0];
    p[3][3] = p[0][0];

    memcpy(out, p, sizeof(p));
}

int crtube_h264_intra_predict4x4(unsigned mode,
                                  const uint8_t top[8],
                                  const uint8_t left[4],
                                  uint8_t top_left,
                                  unsigned have_top,
                                  unsigned have_left,
                                  unsigned have_top_left,
                                  uint8_t prediction[16])
{
    unsigned x, y;
    uint8_t fallback_top[8] = {128,128,128,128,128,128,128,128};
    uint8_t fallback_left[4] = {128,128,128,128};
    if (!prediction || mode > 8 || (have_top && !top) ||
        (have_left && !left)) return -1;
    if (!top) top = fallback_top;
    if (!left) left = fallback_left;

    if (mode == 2) {
        unsigned sum = 0;
        uint8_t dc;
        if (have_top && have_left) {
            for (x = 0; x < 4; ++x) sum += top[x] + left[x];
            dc = (uint8_t)((sum + 4u) >> 3);
        } else if (have_top) {
            for (x = 0; x < 4; ++x) sum += top[x];
            dc = (uint8_t)((sum + 2u) >> 2);
        } else if (have_left) {
            for (y = 0; y < 4; ++y) sum += left[y];
            dc = (uint8_t)((sum + 2u) >> 2);
        } else {
            dc = 128;
        }
        memset(prediction, dc, 16);
        return 0;
    }

    if ((mode == 0 || mode == 3 || mode == 7) && !have_top) return -2;
    if ((mode == 1 || mode == 8) && !have_left) return -2;
    if ((mode == 4 || mode == 5 || mode == 6) &&
        (!have_top || !have_left || !have_top_left)) return -2;

    switch (mode) {
    case 0: /* Vertical. */
        for (y = 0; y < 4; ++y)
            for (x = 0; x < 4; ++x)
                prediction[y * 4 + x] = top[x];
        break;

    case 1: /* Horizontal. */
        for (y = 0; y < 4; ++y)
            for (x = 0; x < 4; ++x)
                prediction[y * 4 + x] = left[y];
        break;

    case 3: /* Diagonal down-left. top[4..7] must be edge-extended by caller. */
        for (y = 0; y < 4; ++y) {
            for (x = 0; x < 4; ++x) {
                unsigned k = x + y;
                unsigned a = k > 7 ? 7 : k;
                unsigned b = k + 1 > 7 ? 7 : k + 1;
                unsigned c = k + 2 > 7 ? 7 : k + 2;
                prediction[y * 4 + x] = avg3(top[a], top[b], top[c]);
            }
        }
        break;

    case 4: /* Diagonal down-right. */
        for (y = 0; y < 4; ++y) {
            for (x = 0; x < 4; ++x) {
                int d = (int)x - (int)y;
                if (d > 0) {
                    unsigned k = (unsigned)d;
                    if (k == 1)
                        prediction[y * 4 + x] = avg3(top_left, top[0], top[1]);
                    else
                        prediction[y * 4 + x] = avg3(top[k - 2], top[k - 1], top[k]);
                } else if (d == 0) {
                    prediction[y * 4 + x] = avg3(left[0], top_left, top[0]);
                } else {
                    unsigned k = (unsigned)(-d);
                    if (k == 1)
                        prediction[y * 4 + x] = avg3(left[1], left[0], top_left);
                    else
                        prediction[y * 4 + x] = avg3(left[k], left[k - 1], left[k - 2]);
                }
            }
        }
        break;

    case 5: /* Vertical-right. */
        predict_vertical_right(top, left, top_left, prediction);
        break;

    case 6: /* Horizontal-down is the transpose of vertical-right. */
    {
        uint8_t transposed[16], swapped_top[5], swapped_left[4];
        for (x = 0; x < 4; ++x) swapped_top[x] = left[x];
        swapped_top[4] = left[3];
        for (y = 0; y < 4; ++y) swapped_left[y] = top[y];
        predict_vertical_right(swapped_top, swapped_left, top_left, transposed);
        for (y = 0; y < 4; ++y)
            for (x = 0; x < 4; ++x)
                prediction[y * 4 + x] = transposed[x * 4 + y];
        break;
    }

    case 7: /* Vertical-left. */
        for (y = 0; y < 4; ++y) {
            for (x = 0; x < 4; ++x) {
                unsigned k = x + (y >> 1);
                prediction[y * 4 + x] = (y & 1u)
                    ? avg3(top[k], top[k + 1], top[k + 2])
                    : avg2(top[k], top[k + 1]);
            }
        }
        break;

    case 8: /* Horizontal-up. */
        for (y = 0; y < 4; ++y) {
            for (x = 0; x < 4; ++x) {
                unsigned k = y + (x >> 1);
                uint8_t a = left[k > 3 ? 3 : k];
                uint8_t b = left[(k + 1) > 3 ? 3 : (k + 1)];
                uint8_t c = left[(k + 2) > 3 ? 3 : (k + 2)];
                prediction[y * 4 + x] = (x & 1u) ? avg3(a, b, c) : avg2(a, b);
            }
        }
        break;

    default:
        return -1;
    }
    return 0;
}
