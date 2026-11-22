#include "h264_decoder.h"

#include <string.h>

/*
 * Connects the supported P macroblock parser output to prediction and frame
 * reconstruction. The prediction-only entry point below remains limited
 * to zero-CBP macroblocks; the residual-aware entry point handles P_L0_16x16.
 */
int crtube_h264_reconstruct_p_mb_to_frame(
    crtube_h264_frame_t *current,
    const crtube_h264_dpb_t *dpb,
    const crtube_h264_p_mb_header_t *header)
{
    if (!current || !dpb || !header)
        return -1;

    if (header->ref_idx_l0 != 0u)
        return -2;

    if (header->coded_block_pattern != 0u)
        return -3;

    if (!header->skipped && header->mb_type != 0u)
        return -2;

    return crtube_h264_reconstruct_p16x16_prediction(
        current, dpb, header);
}

/* Reconstructs a parsed P macroblock with its already decoded residual.
 * P_Skip carries no residual and uses the prediction-only path. */
int crtube_h264_reconstruct_p_mb_with_residual(
    crtube_h264_frame_t *current,
    const crtube_h264_dpb_t *dpb,
    const crtube_h264_p_mb_header_t *header,
    const crtube_h264_p_mb_residual_t *residual,
    unsigned qp_y, int chroma_qp_index_offset,
    int second_chroma_qp_index_offset)
{
    if (!current || !dpb || !header || !residual)
        return -1;

    if (header->ref_idx_l0 != 0u)
        return -2;

    if (header->skipped) {
        if (header->coded_block_pattern != 0u)
            return -2;
        return crtube_h264_reconstruct_p16x16_prediction(
            current, dpb, header);
    }

    if (header->mb_type != 0u)
        return -2;

    return crtube_h264_reconstruct_p16x16(
        current, dpb, header, residual, qp_y,
        chroma_qp_index_offset, second_chroma_qp_index_offset);
}


int crtube_h264_decode_p_l0_16x16_mb(
    const uint8_t *rbsp, size_t rbsp_size, size_t *bit_offset,
    crtube_h264_frame_t *current, const crtube_h264_dpb_t *dpb,
    unsigned mb_address, unsigned num_ref_idx_l0_active,
    int mvp_l0_x, int mvp_l0_y, unsigned *qp_y,
    int chroma_qp_index_offset, int second_chroma_qp_index_offset,
    const uint8_t left_luma_nz[4], const uint8_t top_luma_nz[4],
    const uint8_t left_cb_nz[2], const uint8_t top_cb_nz[2],
    const uint8_t left_cr_nz[2], const uint8_t top_cr_nz[2],
    unsigned have_left, unsigned have_top,
    crtube_h264_p_mb_header_t *header)
{
    crtube_h264_p_mb_header_t parsed;
    crtube_h264_p_mb_residual_t residual;
    size_t offset;
    unsigned next_qp;
    int rc;

    if (!rbsp || !rbsp_size || !bit_offset || !current || !dpb ||
        !qp_y || !header || *bit_offset > rbsp_size * 8u || *qp_y > 51u)
        return -1;

    offset = *bit_offset;
    rc = crtube_h264_parse_p_mb_header(
        rbsp, rbsp_size, &offset, mb_address, num_ref_idx_l0_active,
        mvp_l0_x, mvp_l0_y, &parsed);
    if (rc < 0) return rc;

    if (parsed.coded_block_pattern != 0u) {
        rc = crtube_h264_parse_p_mb_residual(
            rbsp, rbsp_size, &offset, parsed.coded_block_pattern,
            left_luma_nz, top_luma_nz, left_cb_nz, top_cb_nz,
            left_cr_nz, top_cr_nz, have_left, have_top, &residual);
        if (rc < 0) return rc;
    } else {
        memset(&residual, 0, sizeof(residual));
    }

    next_qp = *qp_y;
    if (parsed.coded_block_pattern != 0u) {
        int value = (int)next_qp + parsed.mb_qp_delta;
        value %= 52;
        if (value < 0) value += 52;
        next_qp = (unsigned)value;
    }

    rc = crtube_h264_reconstruct_p16x16(
        current, dpb, &parsed, &residual, next_qp,
        chroma_qp_index_offset, second_chroma_qp_index_offset);
    if (rc < 0) return rc;

    *bit_offset = offset;
    *qp_y = next_qp;
    *header = parsed;
    return 0;
}


/* Export CAVLC nC contexts for the macroblocks to the right and below.
 * The block indices follow the H.264 4x4 luma scan used by the residual
 * parser: right={5,7,13,15}, bottom={10,11,14,15}. Chroma uses raster
 * order in each 2x2 plane: right={1,3}, bottom={2,3}. */
int crtube_h264_p_mb_nz_edges(
    const crtube_h264_p_mb_residual_t *residual,
    uint8_t right_luma_nz[4], uint8_t bottom_luma_nz[4],
    uint8_t right_cb_nz[2], uint8_t bottom_cb_nz[2],
    uint8_t right_cr_nz[2], uint8_t bottom_cr_nz[2])
{
    static const uint8_t right_luma[4] = {5, 7, 13, 15};
    static const uint8_t bottom_luma[4] = {10, 11, 14, 15};
    static const uint8_t right_chroma[2] = {1, 3};
    static const uint8_t bottom_chroma[2] = {2, 3};
    unsigned i;

    if (!residual || !right_luma_nz || !bottom_luma_nz ||
        !right_cb_nz || !bottom_cb_nz || !right_cr_nz || !bottom_cr_nz)
        return -1;

    for (i = 0; i < 4; ++i) {
        if (residual->luma_total_coeff[right_luma[i]] > 16u ||
            residual->luma_total_coeff[bottom_luma[i]] > 16u)
            return -1;
    }
    for (i = 0; i < 2; ++i) {
        if (residual->chroma_ac_total[0][right_chroma[i]] > 15u ||
            residual->chroma_ac_total[0][bottom_chroma[i]] > 15u ||
            residual->chroma_ac_total[1][right_chroma[i]] > 15u ||
            residual->chroma_ac_total[1][bottom_chroma[i]] > 15u)
            return -1;
    }

    for (i = 0; i < 4; ++i) {
        right_luma_nz[i] = residual->luma_total_coeff[right_luma[i]];
        bottom_luma_nz[i] = residual->luma_total_coeff[bottom_luma[i]];
    }
    for (i = 0; i < 2; ++i) {
        right_cb_nz[i] = residual->chroma_ac_total[0][right_chroma[i]];
        bottom_cb_nz[i] = residual->chroma_ac_total[0][bottom_chroma[i]];
        right_cr_nz[i] = residual->chroma_ac_total[1][right_chroma[i]];
        bottom_cr_nz[i] = residual->chroma_ac_total[1][bottom_chroma[i]];
    }
    return 0;
}
