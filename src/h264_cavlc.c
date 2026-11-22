#include "h264_decoder.h"

#include <limits.h>
#include <string.h>

typedef struct cavlc_vlc_table {
    const uint8_t *lengths;
    const uint8_t *bits;
    unsigned rows;
    unsigned cols;
} cavlc_vlc_table_t;

typedef struct cavlc_bit_reader {
    const uint8_t *data;
    size_t size;
    size_t bit;
} cavlc_bit_reader_t;

static const uint8_t coeff_token_len[4][68] = {
    {1,0,0,0, 6,2,0,0, 8,6,3,0, 9,8,7,5, 10,9,8,6, 11,10,9,7, 13,11,10,8, 13,13,11,9, 13,13,13,10, 14,14,13,11, 14,14,14,13, 15,15,14,14, 15,15,15,14, 16,15,15,15, 16,16,16,15, 16,16,16,16, 16,16,16,16},
    {2,0,0,0, 6,2,0,0, 6,5,3,0, 7,6,6,4, 8,6,6,4, 8,7,7,5, 9,8,8,6, 11,9,9,6, 11,11,11,7, 12,11,11,9, 12,12,12,11, 12,12,12,11, 13,13,13,12, 13,13,13,13, 13,14,13,13, 14,14,14,13, 14,14,14,14},
    {4,0,0,0, 6,4,0,0, 6,5,4,0, 6,5,5,4, 7,5,5,4, 7,5,5,4, 7,6,6,4, 7,6,6,4, 8,7,7,5, 8,8,7,6, 9,8,8,7, 9,9,8,8, 9,9,9,8, 10,9,9,9, 10,10,10,10, 10,10,10,10, 10,10,10,10},
    {6,0,0,0, 6,6,0,0, 6,6,6,0, 6,6,6,6, 6,6,6,6, 6,6,6,6, 6,6,6,6, 6,6,6,6, 6,6,6,6, 6,6,6,6, 6,6,6,6, 6,6,6,6, 6,6,6,6, 6,6,6,6, 6,6,6,6, 6,6,6,6, 6,6,6,6}
};
static const uint8_t coeff_token_bits[4][68] = {
    {1,0,0,0, 5,1,0,0, 7,4,1,0, 7,6,5,3, 7,6,5,3, 7,6,5,4, 15,6,5,4, 11,14,5,4, 8,10,13,4, 15,14,9,4, 11,10,13,12, 15,14,9,12, 11,10,13,8, 15,1,9,12, 11,14,13,8, 7,10,9,12, 4,6,5,8},
    {3,0,0,0, 11,2,0,0, 7,7,3,0, 7,10,9,5, 7,6,5,4, 4,6,5,6, 7,6,5,8, 15,6,5,4, 11,14,13,4, 15,10,9,4, 11,14,13,12, 8,10,9,8, 15,14,13,12, 11,10,9,12, 7,11,6,8, 9,8,10,1, 7,6,5,4},
    {15,0,0,0, 15,14,0,0, 11,15,13,0, 8,12,14,12, 15,10,11,11, 11,8,9,10, 9,14,13,9, 8,10,9,8, 15,14,13,13, 11,14,10,12, 15,10,13,12, 11,14,9,12, 8,10,13,8, 13,7,9,12, 9,12,11,10, 5,8,7,6, 1,4,3,2},
    {3,0,0,0, 0,1,0,0, 4,5,6,0, 8,9,10,11, 12,13,14,15, 16,17,18,19, 20,21,22,23, 24,25,26,27, 28,29,30,31, 32,33,34,35, 36,37,38,39, 40,41,42,43, 44,45,46,47, 48,49,50,51, 52,53,54,55, 56,57,58,59, 60,61,62,63}
};
static const uint8_t total_zeros_len[15][16] = {
    {1,3,3,4,4,5,5,6,6,7,7,8,8,9,9,9},
    {3,3,3,3,3,4,4,4,4,5,5,6,6,6,6},
    {4,3,3,3,4,4,3,3,4,5,5,6,5,6},
    {5,3,4,4,3,3,3,4,3,4,5,5,5},
    {4,4,4,3,3,3,3,3,4,5,4,5},
    {6,5,3,3,3,3,3,3,4,3,6},
    {6,5,3,3,3,2,3,4,3,6},
    {6,4,5,3,2,2,3,3,6},
    {6,6,4,2,2,3,2,5},
    {5,5,3,2,2,2,4},
    {4,4,3,3,1,3},
    {4,4,2,1,3},
    {3,3,1,2},
    {2,2,1},
    {1,1}
};
static const uint8_t total_zeros_bits[15][16] = {
    {1,3,2,3,2,3,2,3,2,3,2,3,2,3,2,1},
    {7,6,5,4,3,5,4,3,2,3,2,3,2,1,0},
    {5,7,6,5,4,3,4,3,2,3,2,1,1,0},
    {3,7,5,4,6,5,4,3,3,2,2,1,0},
    {5,4,3,7,6,5,4,3,2,1,1,0},
    {1,1,7,6,5,4,3,2,1,1,0},
    {1,1,5,4,3,3,2,1,1,0},
    {1,1,1,3,3,2,2,1,0},
    {1,0,1,3,2,1,1,1},
    {1,0,1,3,2,1,1},
    {0,1,1,2,1,3},
    {0,1,1,1,1},
    {0,1,1,1},
    {0,1,1},
    {0,1}
};
static const uint8_t chroma_dc_token_len[5][4] = {
    {2,0,0,0}, {6,1,0,0}, {6,6,3,0}, {6,7,7,6}, {6,8,8,7}
};
static const uint8_t chroma_dc_token_bits[5][4] = {
    {1,0,0,0}, {7,1,0,0}, {4,6,1,0}, {3,3,2,5}, {2,3,2,0}
};
static const uint8_t chroma_dc_zeros_len[3][4] = {
    {1,2,3,3}, {1,2,2,0}, {1,1,0,0}
};
static const uint8_t chroma_dc_zeros_bits[3][4] = {
    {1,1,1,0}, {1,1,0,0}, {1,0,0,0}
};
static const uint8_t run_len[7][16] = {
    {1,1}, {1,2,2}, {2,2,2,2}, {2,2,2,3,3},
    {2,2,3,3,3,3}, {2,3,3,3,3,3,3},
    {3,3,3,3,3,3,3,4,5,6,7,8,9,10,11}
};
static const uint8_t run_bits[7][16] = {
    {1,0}, {1,1,0}, {3,2,1,0}, {3,2,1,1,0},
    {3,2,3,2,1,0}, {3,0,1,3,2,5,4},
    {7,6,5,4,3,2,1,1,1,1,1,1,1,1,1}
};


static int validate_vlc(const uint8_t *lengths, const uint8_t *bits,
                        unsigned count, unsigned max_length)
{
    unsigned i, j;
    if (!lengths || !bits || !count || !max_length || max_length > 16u)
        return -1;
    for (i = 0; i < count; ++i) {
        if (!lengths[i]) continue;
        if (lengths[i] > max_length ||
            bits[i] >= (1u << lengths[i])) return -1;
        for (j = i + 1u; j < count; ++j) {
            unsigned shorter, longer, a, b;
            if (!lengths[j]) continue;
            shorter = lengths[i] < lengths[j] ? lengths[i] : lengths[j];
            longer = lengths[i] < lengths[j] ? lengths[j] : lengths[i];
            a = lengths[i] < lengths[j] ? bits[i] : bits[j];
            b = lengths[i] < lengths[j] ? bits[j] : bits[i];
            if (shorter == longer) {
                if (a == b) return -1;
            } else if ((b >> (longer - shorter)) == a) {
                return -1;
            }
        }
    }
    return 0;
}

int crtube_h264_cavlc_validate_tables(void)
{
    unsigned row, count;
    for (row = 0; row < 4u; ++row)
        if (validate_vlc(coeff_token_len[row], coeff_token_bits[row],
                         68u, 16u) < 0) return -1;
    for (row = 0; row < 15u; ++row) {
        count = 16u - row;
        if (validate_vlc(total_zeros_len[row], total_zeros_bits[row],
                         count, 16u) < 0) return -1;
    }
    for (row = 0; row < 5u; ++row)
        if (validate_vlc(chroma_dc_token_len[row], chroma_dc_token_bits[row],
                         4u, 16u) < 0) return -1;
    for (row = 0; row < 3u; ++row) {
        count = 4u - row;
        if (validate_vlc(chroma_dc_zeros_len[row], chroma_dc_zeros_bits[row],
                         count, 16u) < 0) return -1;
    }
    for (row = 0; row < 7u; ++row) {
        count = row == 6u ? 15u : row + 2u;
        if (validate_vlc(run_len[row], run_bits[row], count, 16u) < 0)
            return -1;
    }
    return 0;
}

static int get_bit(cavlc_bit_reader_t *br, unsigned *v)
{
    if (!br || !v || br->size > SIZE_MAX / 8u || br->bit >= br->size * 8u) return -1;
    *v = (br->data[br->bit >> 3] >> (7 - (br->bit & 7))) & 1u;
    ++br->bit;
    return 0;
}

static int get_bits(cavlc_bit_reader_t *br, unsigned n, unsigned *v)
{
    unsigned i, b, x = 0;
    if (!br || !v || n > 32u || br->size > SIZE_MAX / 8u ||
        br->bit > br->size * 8u ||
        n > br->size * 8u - br->bit) return -1;
    for (i = 0; i < n; ++i) {
        if (get_bit(br, &b) < 0) return -1;
        x = (x << 1) | b;
    }
    *v = x;
    return 0;
}

static int decode_vlc(cavlc_bit_reader_t *br, const uint8_t *lengths,
                      const uint8_t *bits, unsigned count, unsigned *symbol)
{
    unsigned code = 0, n, i;
    if (!br || !lengths || !bits || !symbol) return -1;
    for (n = 1; n <= 16; ++n) {
        unsigned b;
        if (get_bit(br, &b) < 0) return -1;
        code = (code << 1) | b;
        for (i = 0; i < count; ++i) {
            if (lengths[i] == n && bits[i] == code) {
                *symbol = i;
                return 0;
            }
        }
    }
    return -1;
}

static int decode_level(cavlc_bit_reader_t *br, unsigned suffix_length,
                        int first_non_trailing, unsigned trailing_ones,
                        int *level)
{
    unsigned prefix = 0, bit, suffix_size, suffix = 0;
    unsigned long long code;
    int64_t value;
    do {
        if (get_bit(br, &bit) < 0) return -1;
        if (!bit && ++prefix > 28) return -1;
    } while (!bit);

    if (prefix == 14 && suffix_length == 0) suffix_size = 4;
    else if (prefix >= 15) suffix_size = prefix - 3;
    else suffix_size = suffix_length;
    if (suffix_size > 32 || get_bits(br, suffix_size, &suffix) < 0) return -1;

    code = (unsigned long long)(prefix < 15 ? prefix : 15) << suffix_length;
    if (prefix >= 15 && suffix_length == 0)
        code += (1ULL << (prefix - 3)) - 4096ULL;
    code += suffix;
    if (first_non_trailing && trailing_ones < 3) code += 2;
    if (code > (unsigned long long)INT16_MAX * 2ULL + 1ULL)
        return -1;
    value = (code & 1ULL) ? (int64_t)((code + 1ULL) >> 1)
                          : -(int64_t)(code >> 1);
    if (value < INT16_MIN || value > INT16_MAX) return -1;
    *level = (int)value;
    return 0;
}

static int decode_total_zeros(cavlc_bit_reader_t *br, unsigned total_coeff,
                              unsigned max_coeff, unsigned *zeros)
{
    unsigned symbol, row, count;
    if (total_coeff >= max_coeff) { *zeros = 0; return 0; }
    if (max_coeff == 4) {
        row = total_coeff - 1;
        count = 4 - total_coeff + 1;
        if (decode_vlc(br, chroma_dc_zeros_len[row], chroma_dc_zeros_bits[row], count, &symbol) < 0) return -1;
    } else if (max_coeff == 15 || max_coeff == 16) {
        row = total_coeff - 1;
        /* Chroma AC uses the regular total_zeros VLC, but its scan
           contains only positions 1..15 (15 coefficients). */
        count = (max_coeff == 15) ? (16u - total_coeff)
                                  : (17u - total_coeff);
        if (decode_vlc(br, total_zeros_len[row], total_zeros_bits[row],
                       count, &symbol) < 0) return -1;
    } else return -1;
    *zeros = symbol;
    return (*zeros <= max_coeff - total_coeff) ? 0 : -1;
}

static int decode_run_before(cavlc_bit_reader_t *br, unsigned zeros_left,
                             unsigned *run)
{
    unsigned row, count, symbol;
    if (!zeros_left) { *run = 0; return 0; }
    row = zeros_left >= 7 ? 6 : zeros_left - 1;
    count = zeros_left >= 7 ? 15 : zeros_left + 1;
    if (decode_vlc(br, run_len[row], run_bits[row], count, &symbol) < 0) return -1;
    *run = symbol;
    return *run <= zeros_left ? 0 : -1;
}

int crtube_h264_cavlc_decode_block(const uint8_t *rbsp, size_t rbsp_size,
                                    size_t *bit_offset, int n_c,
                                    unsigned max_coeff, int16_t coeffs[16],
                                    unsigned *total_coeff_out)
{
    cavlc_bit_reader_t br;
    unsigned token, total_coeff, trailing_ones, table, i, total_zeros, zeros_left;
    int levels[16];
    unsigned runs[16];
    if (!rbsp || !rbsp_size || rbsp_size > SIZE_MAX / 8u ||
        !bit_offset || *bit_offset > rbsp_size * 8u ||
        !coeffs || !total_coeff_out || (max_coeff != 4 && max_coeff != 15 && max_coeff != 16) ||
        n_c < -1 || n_c > 16 || crtube_h264_cavlc_validate_tables() < 0)
        return -1;
    memset(coeffs, 0, 16 * sizeof(*coeffs));
    *total_coeff_out = 0;
    br.data = rbsp; br.size = rbsp_size; br.bit = *bit_offset;

    if (max_coeff == 4) {
        unsigned symbol;
        if (n_c != -1) return -1;
        if (decode_vlc(&br, &chroma_dc_token_len[0][0],
                       &chroma_dc_token_bits[0][0], 20, &symbol) < 0) return -1;
        token = symbol;
    } else {
        table = n_c < 2 ? 0 : (n_c < 4 ? 1 : (n_c < 8 ? 2 : 3));
        if (decode_vlc(&br, coeff_token_len[table], coeff_token_bits[table], 68, &token) < 0) return -1;
    }
    total_coeff = token >> 2;
    trailing_ones = token & 3u;
    if (total_coeff > max_coeff || trailing_ones > total_coeff) return -1;
    if (!total_coeff) {
        *bit_offset = br.bit;
        return 0;
    }

    for (i = 0; i < trailing_ones; ++i) {
        unsigned sign;
        if (get_bit(&br, &sign) < 0) return -1;
        levels[i] = sign ? -1 : 1;
    }
    if (trailing_ones < total_coeff) {
        unsigned suffix_length = (total_coeff > 10 && trailing_ones < 3) ? 1 : 0;
        for (i = trailing_ones; i < total_coeff; ++i) {
            int level;
            if (decode_level(&br, suffix_length, i == trailing_ones,
                             trailing_ones, &level) < 0) return -1;
            levels[i] = level;
            if (suffix_length == 0) suffix_length = 1;
            if (suffix_length < 6) {
                unsigned abs_level = (unsigned)(level < 0
                    ? -(int64_t)level : (int64_t)level);
                if (abs_level > (3u << (suffix_length - 1))) ++suffix_length;
            }
        }
    }

    if (decode_total_zeros(&br, total_coeff, max_coeff, &total_zeros) < 0) return -1;
    zeros_left = total_zeros;
    for (i = 0; i + 1 < total_coeff; ++i) {
        if (decode_run_before(&br, zeros_left, &runs[i]) < 0) return -1;
        zeros_left -= runs[i];
    }
    runs[total_coeff - 1] = zeros_left;

    {
        int scan = (int)(total_coeff + total_zeros - 1);
        for (i = 0; i < total_coeff; ++i) {
            if (scan < 0 || scan >= (int)max_coeff || levels[i] < INT16_MIN || levels[i] > INT16_MAX) return -1;
            coeffs[scan] = (int16_t)levels[i];
            scan -= (int)runs[i] + 1;
        }
    }
    *bit_offset = br.bit;
    *total_coeff_out = total_coeff;
    return 0;
}

