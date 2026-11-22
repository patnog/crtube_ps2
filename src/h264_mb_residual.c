#include "h264_decoder.h"

#include <string.h>

static int block_index_at(unsigned x, unsigned y)
{
    static const uint8_t block_x[16] = {
        0, 1, 0, 1, 2, 3, 2, 3, 0, 1, 0, 1, 2, 3, 2, 3
    };
    static const uint8_t block_y[16] = {
        0, 0, 1, 1, 0, 0, 1, 1, 2, 2, 3, 3, 2, 2, 3, 3
    };
    unsigned i;

    for (i = 0; i < 16; ++i) {
        if (block_x[i] == x && block_y[i] == y)
            return (int)i;
    }
    return -1;
}

static int predicted_nz(const uint8_t counts[16], const uint8_t left_nz[4],
                        const uint8_t top_nz[4], unsigned have_left,
                        unsigned have_top, unsigned x, unsigned y)
{
    int a = -1;
    int b = -1;
    int index;

    if (x != 0) {
        index = block_index_at(x - 1u, y);
        if (index < 0 || (unsigned)index >= 16u)
            return -1;
        a = counts[index];
    } else if (have_left) {
        a = left_nz[y];
    }

    if (y != 0) {
        index = block_index_at(x, y - 1u);
        if (index < 0 || (unsigned)index >= 16u)
            return -1;
        b = counts[index];
    } else if (have_top) {
        b = top_nz[x];
    }

    if (a >= 0 && b >= 0)
        return (a + b + 1) >> 1;
    if (a >= 0)
        return a;
    if (b >= 0)
        return b;
    return 0;
}

int crtube_h264_parse_intra4x4_luma_residual(
    const uint8_t *rbsp, size_t rbsp_size, size_t *bit_offset,
    unsigned coded_block_pattern, const uint8_t left_nz[4],
    const uint8_t top_nz[4], unsigned have_left, unsigned have_top,
    int16_t levels[16][16], uint8_t total_coeff[16])
{
    static const uint8_t block_x[16] = {
        0, 1, 0, 1, 2, 3, 2, 3, 0, 1, 0, 1, 2, 3, 2, 3
    };
    static const uint8_t block_y[16] = {
        0, 0, 1, 1, 0, 0, 1, 1, 2, 2, 3, 3, 2, 2, 3, 3
    };
    int16_t parsed_levels[16][16];
    uint8_t parsed_total[16];
    size_t parsed_offset;
    unsigned block;

    if (!rbsp || !rbsp_size || !bit_offset ||
        rbsp_size > SIZE_MAX / 8u || *bit_offset > rbsp_size * 8u || !levels || !total_coeff ||
        coded_block_pattern > 47u || have_left > 1u || have_top > 1u ||
        (have_left && !left_nz) || (have_top && !top_nz))
        return -1;

    if (have_left) {
        for (block = 0; block < 4; ++block)
            if (left_nz[block] > 16u) return -1;
    }
    if (have_top) {
        for (block = 0; block < 4; ++block)
            if (top_nz[block] > 16u) return -1;
    }

    memset(parsed_levels, 0, sizeof(parsed_levels));
    memset(parsed_total, 0, sizeof(parsed_total));
    parsed_offset = *bit_offset;

    for (block = 0; block < 16; ++block) {
        unsigned x = block_x[block];
        unsigned y = block_y[block];
        unsigned group = (y >> 1) * 2u + (x >> 1);

        if ((coded_block_pattern & 15u) & (1u << group)) {
            int n_c = predicted_nz(parsed_total, left_nz, top_nz,
                                   have_left, have_top, x, y);
            unsigned total = 0;

            if (n_c < 0 || n_c > 16 ||
                crtube_h264_cavlc_decode_block(rbsp, rbsp_size,
                                                &parsed_offset, n_c, 16u,
                                                parsed_levels[block],
                                                &total) < 0)
                return -1;
            parsed_total[block] = (uint8_t)total;
        }
    }

    memcpy(levels, parsed_levels, sizeof(parsed_levels));
    memcpy(total_coeff, parsed_total, sizeof(parsed_total));
    *bit_offset = parsed_offset;
    return 0;
}


static int predicted_chroma_nz(const uint8_t counts[4],
                               const uint8_t left_nz[2],
                               const uint8_t top_nz[2],
                               unsigned have_left, unsigned have_top,
                               unsigned x, unsigned y)
{
    int a = -1;
    int b = -1;
    if (x != 0u)
        a = counts[y * 2u + (x - 1u)];
    else if (have_left)
        a = left_nz[y];
    if (y != 0u)
        b = counts[(y - 1u) * 2u + x];
    else if (have_top)
        b = top_nz[x];
    if (a >= 0 && b >= 0) return (a + b + 1) >> 1;
    if (a >= 0) return a;
    if (b >= 0) return b;
    return 0;
}

int crtube_h264_parse_intra4x4_chroma_residual(
    const uint8_t *rbsp, size_t rbsp_size, size_t *bit_offset,
    unsigned chroma_cbp, const uint8_t left_nz_cb[2],
    const uint8_t top_nz_cb[2], const uint8_t left_nz_cr[2],
    const uint8_t top_nz_cr[2], unsigned have_left, unsigned have_top,
    int16_t dc_levels[2][4], int16_t ac_levels[2][4][15],
    uint8_t dc_total_coeff[2], uint8_t ac_total_coeff[2][4])
{
    int16_t parsed_dc[2][4];
    int16_t parsed_ac[2][4][15];
    uint8_t parsed_dc_total[2];
    uint8_t parsed_ac_total[2][4];
    const uint8_t *left_edges[2] = {left_nz_cb, left_nz_cr};
    const uint8_t *top_edges[2] = {top_nz_cb, top_nz_cr};
    size_t parsed_offset;
    unsigned plane, block;

    if (!rbsp || !rbsp_size || !bit_offset ||
        rbsp_size > SIZE_MAX / 8u || *bit_offset > rbsp_size * 8u || chroma_cbp > 2u ||
        !dc_levels || !ac_levels || !dc_total_coeff || !ac_total_coeff ||
        have_left > 1u || have_top > 1u ||
        (have_left && (!left_nz_cb || !left_nz_cr)) ||
        (have_top && (!top_nz_cb || !top_nz_cr)))
        return -1;

    if (have_left) {
        for (plane = 0; plane < 2; ++plane)
            for (block = 0; block < 2; ++block)
                if (left_edges[plane][block] > 15u) return -1;
    }
    if (have_top) {
        for (plane = 0; plane < 2; ++plane)
            for (block = 0; block < 2; ++block)
                if (top_edges[plane][block] > 15u) return -1;
    }

    memset(parsed_dc, 0, sizeof(parsed_dc));
    memset(parsed_ac, 0, sizeof(parsed_ac));
    memset(parsed_dc_total, 0, sizeof(parsed_dc_total));
    memset(parsed_ac_total, 0, sizeof(parsed_ac_total));
    parsed_offset = *bit_offset;

    if (chroma_cbp != 0u) {
        for (plane = 0; plane < 2; ++plane) {
            unsigned total = 0;
            int16_t coeffs[16];
            if (crtube_h264_cavlc_decode_block(rbsp, rbsp_size,
                    &parsed_offset, -1, 4u, coeffs, &total) < 0)
                return -1;
            memcpy(parsed_dc[plane], coeffs, sizeof(parsed_dc[plane]));
            parsed_dc_total[plane] = (uint8_t)total;
        }
    }

    if (chroma_cbp == 2u) {
        for (plane = 0; plane < 2; ++plane) {
            uint8_t counts[4] = {0, 0, 0, 0};
            for (block = 0; block < 4; ++block) {
                unsigned x = block & 1u;
                unsigned y = block >> 1;
                unsigned total = 0;
                int16_t coeffs[16];
                int n_c = predicted_chroma_nz(counts, left_edges[plane],
                    top_edges[plane], have_left, have_top, x, y);
                if (crtube_h264_cavlc_decode_block(rbsp, rbsp_size,
                        &parsed_offset, n_c, 15u, coeffs, &total) < 0)
                    return -1;
                memcpy(parsed_ac[plane][block], coeffs,
                       sizeof(parsed_ac[plane][block]));
                counts[block] = (uint8_t)total;
                parsed_ac_total[plane][block] = (uint8_t)total;
            }
        }
    }

    memcpy(dc_levels, parsed_dc, sizeof(parsed_dc));
    memcpy(ac_levels, parsed_ac, sizeof(parsed_ac));
    memcpy(dc_total_coeff, parsed_dc_total, sizeof(parsed_dc_total));
    memcpy(ac_total_coeff, parsed_ac_total, sizeof(parsed_ac_total));
    *bit_offset = parsed_offset;
    return 0;
}
