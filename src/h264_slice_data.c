#include "h264_decoder.h"

#include <string.h>

static int read_bit(const uint8_t *data, size_t size, size_t *bit,
                    unsigned *value)
{
    if (!data || !bit || !value || size > SIZE_MAX / 8u || *bit >= size * 8u) return -1;
    *value = (data[*bit >> 3] >> (7u - (*bit & 7u))) & 1u;
    ++*bit;
    return 0;
}

static int only_rbsp_trailing_bits(const uint8_t *data, size_t size,
                                   size_t bit)
{
    unsigned b;
    size_t p = bit;
    if (read_bit(data, size, &p, &b) < 0 || b != 1u) return 0;
    while (p < size * 8u) {
        if (read_bit(data, size, &p, &b) < 0 || b != 0u) return 0;
    }
    return 1;
}

int crtube_h264_parse_i4x4_slice_data(
    const uint8_t *rbsp, size_t rbsp_size, size_t bit_offset,
    unsigned first_mb_in_slice, unsigned mb_width, unsigned mb_height,
    unsigned current_qp, unsigned transform_8x8_mode,
    crtube_h264_i4x4_mb_data_t *macroblocks, unsigned max_macroblocks)
{
    unsigned total_mbs, address, count = 0;
    uint8_t left_modes[4] = {2,2,2,2};
    uint8_t top_modes[30][4];
    uint8_t top_nz[30][4];
    uint8_t top_cb[30][2], top_cr[30][2];
    uint8_t left_nz[4] = {0,0,0,0};
    uint8_t left_cb[2] = {0,0}, left_cr[2] = {0,0};
    unsigned qp = current_qp;

    if (!rbsp || !rbsp_size || rbsp_size > SIZE_MAX / 8u || bit_offset > rbsp_size * 8u ||
        !mb_width || !mb_height || mb_width > 30u || mb_height > 17u ||
        current_qp > 51u || !macroblocks || !max_macroblocks ||
        transform_8x8_mode > 1u) return -1;
    total_mbs = mb_width * mb_height;
    if (first_mb_in_slice >= total_mbs) return -1;
    if (transform_8x8_mode) return -2;
    memset(top_modes, 2, sizeof(top_modes));
    memset(top_nz, 0, sizeof(top_nz));
    memset(top_cb, 0, sizeof(top_cb));
    memset(top_cr, 0, sizeof(top_cr));
    address = first_mb_in_slice;

    while (address < total_mbs) {
        crtube_h264_intra4x4_mb_header_t header;
        crtube_h264_i4x4_mb_data_t *out;
        size_t before = bit_offset;
        unsigned mb_x = address % mb_width;
        unsigned mb_y = address / mb_width;
        unsigned have_left = mb_x != 0u && address - 1u >= first_mb_in_slice;
        unsigned have_top = mb_y != 0u && address - mb_width >= first_mb_in_slice;
        int16_t chroma_dc[2][4];
        int16_t chroma_ac[2][4][15];
        uint8_t chroma_dc_total[2], chroma_ac_total[2][4];

        if (only_rbsp_trailing_bits(rbsp, rbsp_size, bit_offset)) break;
        if (!have_left) {
            memset(left_modes, 2, sizeof(left_modes));
            memset(left_nz, 0, sizeof(left_nz));
            memset(left_cb, 0, sizeof(left_cb));
            memset(left_cr, 0, sizeof(left_cr));
        }
        if (count >= max_macroblocks) return -1;
        if (crtube_h264_parse_intra4x4_mb_header(
                rbsp, rbsp_size, &bit_offset,
                have_left ? left_modes : NULL,
                have_top ? top_modes[mb_x] : NULL,
                have_left, have_top, transform_8x8_mode, qp, &header) < 0) {
            bit_offset = before;
            return -2;
        }
        out = &macroblocks[count];
        memset(out, 0, sizeof(*out));
        out->mb_address = address;
        out->slice_have_top = (uint8_t)have_top;
        out->slice_have_left = (uint8_t)have_left;
        out->slice_have_top_right = (uint8_t)(have_top &&
            mb_x + 1u < mb_width && address - mb_width + 1u >= first_mb_in_slice);
        memcpy(out->modes, header.modes, sizeof(out->modes));
        out->chroma_pred_mode = header.chroma_pred_mode;
        out->coded_block_pattern = header.coded_block_pattern;
        out->qp_y = header.qp_y;

        if (crtube_h264_parse_intra4x4_luma_residual(
                rbsp, rbsp_size, &bit_offset, header.coded_block_pattern,
                have_left ? left_nz : NULL, have_top ? top_nz[mb_x] : NULL,
                have_left, have_top, out->luma_levels,
                out->luma_total_coeff) < 0) return -1;
        if (crtube_h264_parse_intra4x4_chroma_residual(
                rbsp, rbsp_size, &bit_offset, header.coded_block_pattern >> 4,
                have_left ? left_cb : NULL, have_top ? top_cb[mb_x] : NULL,
                have_left ? left_cr : NULL, have_top ? top_cr[mb_x] : NULL,
                have_left, have_top, chroma_dc, chroma_ac,
                chroma_dc_total, chroma_ac_total) < 0) return -1;
        memcpy(out->chroma_dc, chroma_dc, sizeof(chroma_dc));
        memcpy(out->chroma_ac, chroma_ac, sizeof(chroma_ac));
        memcpy(out->chroma_dc_total, chroma_dc_total, sizeof(chroma_dc_total));
        memcpy(out->chroma_ac_total, chroma_ac_total, sizeof(chroma_ac_total));

        /* Retain the right and bottom edge contexts for the next MB. This
           initial parser is intended for raster-ordered single-slice tests;
           cross-slice neighbor availability is deliberately not inferred. */
        left_modes[0] = header.modes[5];
        left_modes[1] = header.modes[7];
        left_modes[2] = header.modes[13];
        left_modes[3] = header.modes[15];
        left_nz[0] = out->luma_total_coeff[5];
        left_nz[1] = out->luma_total_coeff[7];
        left_nz[2] = out->luma_total_coeff[13];
        left_nz[3] = out->luma_total_coeff[15];
        left_cb[0] = out->chroma_ac_total[0][1];
        left_cb[1] = out->chroma_ac_total[0][3];
        left_cr[0] = out->chroma_ac_total[1][1];
        left_cr[1] = out->chroma_ac_total[1][3];
        top_modes[mb_x][0] = header.modes[10];
        top_modes[mb_x][1] = header.modes[11];
        top_modes[mb_x][2] = header.modes[14];
        top_modes[mb_x][3] = header.modes[15];
        top_nz[mb_x][0] = out->luma_total_coeff[10];
        top_nz[mb_x][1] = out->luma_total_coeff[11];
        top_nz[mb_x][2] = out->luma_total_coeff[14];
        top_nz[mb_x][3] = out->luma_total_coeff[15];
        top_cb[mb_x][0] = out->chroma_ac_total[0][2];
        top_cb[mb_x][1] = out->chroma_ac_total[0][3];
        top_cr[mb_x][0] = out->chroma_ac_total[1][2];
        top_cr[mb_x][1] = out->chroma_ac_total[1][3];
        qp = header.qp_y;
        ++count;
        ++address;
    }
    return (int)count;
}
