#include "h264_decoder.h"

#include <limits.h>
#include <string.h>

typedef struct mb_bit_reader {
    const uint8_t *data;
    size_t size;
    size_t bit;
} mb_bit_reader_t;

static int mb_read_bit(mb_bit_reader_t *br, unsigned *value)
{
    if (!br || !value || br->size > SIZE_MAX / 8u || br->bit >= br->size * 8u) return -1;
    *value = (br->data[br->bit >> 3] >> (7u - (br->bit & 7u))) & 1u;
    ++br->bit;
    return 0;
}

static int mb_read_bits(mb_bit_reader_t *br, unsigned count, unsigned *value)
{
    unsigned i, bit, result = 0;
    if (!br || !value || count > 32) return -1;
    for (i = 0; i < count; ++i) {
        if (mb_read_bit(br, &bit) < 0) return -1;
        result = (result << 1) | bit;
    }
    *value = result;
    return 0;
}

static int mb_read_ue(mb_bit_reader_t *br, unsigned *value)
{
    unsigned zeros = 0, bit, suffix = 0;
    if (!br || !value) return -1;
    for (;;) {
        if (mb_read_bit(br, &bit) < 0) return -1;
        if (bit) break;
        if (++zeros > 31) return -1;
    }
    if (zeros && mb_read_bits(br, zeros, &suffix) < 0) return -1;
    *value = ((1u << zeros) - 1u) + suffix;
    return 0;
}

static int mb_read_se(mb_bit_reader_t *br, int *value)
{
    unsigned code;
    int64_t mapped;
    if (!value || mb_read_ue(br, &code) < 0) return -1;
    mapped = (code & 1u) ? (int64_t)(((uint64_t)code + 1u) >> 1)
                         : -(int64_t)(code >> 1);
    if (mapped < INT_MIN || mapped > INT_MAX) return -1;
    *value = (int)mapped;
    return 0;
}

static int block_index_at(unsigned x, unsigned y)
{
    static const uint8_t block_x[16] = {
        0, 1, 0, 1, 2, 3, 2, 3, 0, 1, 0, 1, 2, 3, 2, 3
    };
    static const uint8_t block_y[16] = {
        0, 0, 1, 1, 0, 0, 1, 1, 2, 2, 3, 3, 2, 2, 3, 3
    };
    unsigned i;
    for (i = 0; i < 16; ++i)
        if (block_x[i] == x && block_y[i] == y) return (int)i;
    return -1;
}

static unsigned predicted_mode(const uint8_t modes[16], unsigned x,
                               unsigned y, const uint8_t left_modes[4],
                               const uint8_t top_modes[4],
                               unsigned have_left, unsigned have_top)
{
    unsigned a = 2, b = 2;
    int index;
    if (x) {
        index = block_index_at(x - 1, y);
        if (index >= 0) a = modes[index];
    } else if (have_left && left_modes) {
        a = left_modes[y];
    }
    if (y) {
        index = block_index_at(x, y - 1);
        if (index >= 0) b = modes[index];
    } else if (have_top && top_modes) {
        b = top_modes[x];
    }
    return a < b ? a : b;
}

int crtube_h264_parse_intra4x4_mb_header(
    const uint8_t *rbsp, size_t rbsp_size, size_t *bit_offset,
    const uint8_t left_modes[4], const uint8_t top_modes[4],
    unsigned have_left, unsigned have_top, unsigned transform_8x8_mode,
    unsigned current_qp, crtube_h264_intra4x4_mb_header_t *header)
{
    /* Table 9-4, Intra-coded macroblock coded_block_pattern mapping. */
    static const uint8_t intra_cbp[48] = {
        47,31,15,0,23,27,29,30,7,11,13,14,39,43,45,46,
        16,3,5,10,12,19,21,26,28,35,37,42,44,1,2,4,
        8,17,18,20,24,6,9,22,25,32,33,34,36,40,41,38
    };
    static const uint8_t block_x[16] = {
        0,1,0,1,2,3,2,3,0,1,0,1,2,3,2,3
    };
    static const uint8_t block_y[16] = {
        0,0,1,1,0,0,1,1,2,2,3,3,2,2,3,3
    };
    mb_bit_reader_t br;
    crtube_h264_intra4x4_mb_header_t parsed;
    unsigned mb_type, i, flag, rem, code_num;
    int qp_delta = 0;

    if (!rbsp || !rbsp_size || !bit_offset || rbsp_size > SIZE_MAX / 8u || *bit_offset > rbsp_size * 8u ||
        !header || current_qp > 51 || transform_8x8_mode > 1 ||
        (have_left && !left_modes) || (have_top && !top_modes)) return -1;
    if (transform_8x8_mode) return -2;
    if (have_left) {
        unsigned i;
        for (i = 0; i < 4; ++i) if (left_modes[i] > 8) return -1;
    }
    if (have_top) {
        unsigned i;
        for (i = 0; i < 4; ++i) if (top_modes[i] > 8) return -1;
    }

    memset(&parsed, 0, sizeof(parsed));
    br.data = rbsp;
    br.size = rbsp_size;
    br.bit = *bit_offset;

    if (mb_read_ue(&br, &mb_type) < 0) return -1;
    if (mb_type != 0) return -2;

    for (i = 0; i < 16; ++i) {
        unsigned mpm = predicted_mode(parsed.modes, block_x[i], block_y[i],
                                      left_modes, top_modes,
                                      have_left, have_top);
        if (mb_read_bit(&br, &flag) < 0) return -1;
        if (flag) {
            parsed.modes[i] = (uint8_t)mpm;
        } else {
            if (mb_read_bits(&br, 3, &rem) < 0) return -1;
            parsed.modes[i] = (uint8_t)(rem < mpm ? rem : rem + 1u);
        }
    }

    if (mb_read_ue(&br, &parsed.chroma_pred_mode) < 0 ||
        parsed.chroma_pred_mode > 3 || mb_read_ue(&br, &code_num) < 0 ||
        code_num >= 48) return -1;
    parsed.coded_block_pattern_code_num = code_num;
    parsed.coded_block_pattern = intra_cbp[code_num];

    if (parsed.coded_block_pattern != 0) {
        if (mb_read_se(&br, &qp_delta) < 0 || qp_delta < -26 || qp_delta > 25)
            return -1;
    }
    parsed.mb_qp_delta = qp_delta;
    parsed.qp_y = (unsigned)((int)current_qp + qp_delta + 52) % 52u;
    *header = parsed;
    *bit_offset = br.bit;
    return 0;
}
