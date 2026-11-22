#include "h264_decoder.h"

#include <string.h>

int crtube_h264_parse_p_mb_residual(
    const uint8_t *rbsp, size_t rbsp_size, size_t *bit_offset,
    unsigned coded_block_pattern,
    const uint8_t left_luma_nz[4], const uint8_t top_luma_nz[4],
    const uint8_t left_cb_nz[2], const uint8_t top_cb_nz[2],
    const uint8_t left_cr_nz[2], const uint8_t top_cr_nz[2],
    unsigned have_left, unsigned have_top,
    crtube_h264_p_mb_residual_t *residual)
{
    crtube_h264_p_mb_residual_t out;
    size_t offset;
    if (!rbsp || !rbsp_size || !bit_offset ||
        rbsp_size > SIZE_MAX / 8u || *bit_offset > rbsp_size * 8u || coded_block_pattern > 47u ||
        !residual || have_left > 1u || have_top > 1u ||
        (have_left && (!left_luma_nz || !left_cb_nz || !left_cr_nz)) ||
        (have_top && (!top_luma_nz || !top_cb_nz || !top_cr_nz)))
        return -1;

    memset(&out, 0, sizeof(out));
    offset = *bit_offset;

    /* The residual syntax is shared by inter and intra macroblocks.
       These existing block parsers decode CAVLC coefficients; prediction
       and reconstruction remain the caller's responsibility. */
    if (crtube_h264_parse_intra4x4_luma_residual(
            rbsp, rbsp_size, &offset, coded_block_pattern,
            have_left ? left_luma_nz : NULL,
            have_top ? top_luma_nz : NULL,
            have_left, have_top, out.luma_levels,
            out.luma_total_coeff) < 0)
        return -1;

    if (crtube_h264_parse_intra4x4_chroma_residual(
            rbsp, rbsp_size, &offset, coded_block_pattern >> 4,
            have_left ? left_cb_nz : NULL,
            have_top ? top_cb_nz : NULL,
            have_left ? left_cr_nz : NULL,
            have_top ? top_cr_nz : NULL,
            have_left, have_top, out.chroma_dc, out.chroma_ac,
            out.chroma_dc_total, out.chroma_ac_total) < 0)
        return -1;

    *residual = out;
    *bit_offset = offset;
    return 0;
}
