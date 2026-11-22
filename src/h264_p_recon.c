#include "h264_decoder.h"

#include <string.h>

int crtube_h264_reconstruct_p16x16_prediction(
    crtube_h264_frame_t *current, const crtube_h264_dpb_t *dpb,
    const crtube_h264_p_mb_header_t *mb)
{
    uint8_t y[256], cb[64], cr[64];
    unsigned mb_width, mb_x, mb_y;

    if (!current || !current->storage || !dpb || !dpb->valid ||
        !mb || mb->ref_idx_l0 != 0u ||
        current->width != dpb->reference.width ||
        current->height != dpb->reference.height)
        return -1;

    /* This stage intentionally handles only prediction-only macroblocks.
       Residual-bearing P macroblocks must wait for the P residual parser. */
    if (!mb->skipped && mb->coded_block_pattern != 0u)
        return -2;

    mb_width = current->width / 16u;
    if (!mb_width || mb->mb_address >=
        mb_width * (current->height / 16u))
        return -1;

    mb_x = mb->mb_address % mb_width;
    mb_y = mb->mb_address / mb_width;

    if (crtube_h264_motion_compensate_16x16(
            &dpb->reference, mb_x, mb_y,
            mb->mv_l0_x, mb->mv_l0_y, y, cb, cr) < 0)
        return -1;

    return crtube_h264_frame_put_macroblock(current, mb_x, mb_y, y, cb, cr);
}


static int add_p_residual4x4(uint8_t *plane, unsigned stride,
                             unsigned x0, unsigned y0,
                             const int16_t scan_levels[16], unsigned qp)
{
    int16_t raster[16], dequant[16], residual[16];
    uint8_t prediction[16], output[16];
    unsigned x, y;
    crtube_h264_scan4x4_to_raster(scan_levels, raster);
    if (crtube_h264_inverse_quant4x4(raster, qp, NULL, dequant) < 0 ||
        crtube_h264_inverse_transform4x4(dequant, residual) < 0)
        return -1;
    for (y = 0; y < 4; ++y)
        for (x = 0; x < 4; ++x)
            prediction[y * 4u + x] =
                plane[(y0 + y) * stride + x0 + x];
    crtube_h264_add_residual4x4(prediction, residual, output);
    for (y = 0; y < 4; ++y)
        for (x = 0; x < 4; ++x)
            plane[(y0 + y) * stride + x0 + x] = output[y * 4u + x];
    return 0;
}

static int add_p_chroma_residual(uint8_t *plane, unsigned stride,
                                 const int16_t dc_levels[4],
                                 const int16_t ac_levels[4][15],
                                 unsigned qp_c)
{
    int16_t dc_scaled[4];
    unsigned block;
    if (crtube_h264_inverse_chroma_dc4x4(dc_levels, qp_c, dc_scaled) < 0)
        return -1;
    for (block = 0; block < 4; ++block) {
        int16_t scan[16] = {0}, raster[16], dequant[16], residual[16];
        uint8_t prediction[16], output[16];
        unsigned x0 = (block & 1u) * 4u;
        unsigned y0 = (block >> 1u) * 4u;
        unsigned x, y;
        for (x = 0; x < 15; ++x)
            scan[x + 1u] = ac_levels[block][x];
        crtube_h264_scan4x4_to_raster(scan, raster);
        if (crtube_h264_inverse_quant4x4(raster, qp_c, NULL, dequant) < 0)
            return -1;
        dequant[0] = dc_scaled[block];
        if (crtube_h264_inverse_transform4x4(dequant, residual) < 0)
            return -1;
        for (y = 0; y < 4; ++y)
            for (x = 0; x < 4; ++x)
                prediction[y * 4u + x] =
                    plane[(y0 + y) * stride + x0 + x];
        crtube_h264_add_residual4x4(prediction, residual, output);
        for (y = 0; y < 4; ++y)
            for (x = 0; x < 4; ++x)
                plane[(y0 + y) * stride + x0 + x] =
                    output[y * 4u + x];
    }
    return 0;
}

int crtube_h264_reconstruct_p16x16(
    crtube_h264_frame_t *current, const crtube_h264_dpb_t *dpb,
    const crtube_h264_p_mb_header_t *mb,
    const crtube_h264_p_mb_residual_t *residual,
    unsigned qp_y, int chroma_qp_index_offset,
    int second_chroma_qp_index_offset)
{
    uint8_t y[256], cb[64], cr[64];
    unsigned mb_width, mb_x, mb_y, block;
    unsigned qp_cb, qp_cr;
    if (!current || !current->storage || !dpb || !dpb->valid ||
        !mb || !residual || qp_y > 51u || mb->ref_idx_l0 != 0u ||
        chroma_qp_index_offset < -12 || chroma_qp_index_offset > 12 ||
        second_chroma_qp_index_offset < -12 ||
        second_chroma_qp_index_offset > 12 ||
        current->width != dpb->reference.width ||
        current->height != dpb->reference.height ||
        (current->width & 15u) || (current->height & 15u))
        return -1;
    if (mb->coded_block_pattern > 47u) return -1;
    mb_width = current->width / 16u;
    if (mb->mb_address >= mb_width * (current->height / 16u))
        return -1;
    mb_x = mb->mb_address % mb_width;
    mb_y = mb->mb_address / mb_width;
    if (crtube_h264_motion_compensate_16x16(
            &dpb->reference, mb_x, mb_y, mb->mv_l0_x, mb->mv_l0_y,
            y, cb, cr) < 0)
        return -1;

    if (mb->coded_block_pattern & 15u) {
        static const uint8_t bx[16] = {
            0,1,0,1,2,3,2,3,0,1,0,1,2,3,2,3
        };
        static const uint8_t by[16] = {
            0,0,1,1,0,0,1,1,2,2,3,3,2,2,3,3
        };
        for (block = 0; block < 16u; ++block) {
            unsigned group = ((by[block] >> 1u) * 2u) +
                             (bx[block] >> 1u);
            if (mb->coded_block_pattern & (1u << group)) {
                if (add_p_residual4x4(y, 16u, bx[block] * 4u,
                        by[block] * 4u, residual->luma_levels[block],
                        qp_y) < 0)
                    return -2;
            }
        }
    }

    qp_cb = crtube_h264_chroma_qp(qp_y, chroma_qp_index_offset);
    qp_cr = crtube_h264_chroma_qp(qp_y, second_chroma_qp_index_offset);
    if ((mb->coded_block_pattern >> 4u) != 0u) {
        if (add_p_chroma_residual(cb, 8u, residual->chroma_dc[0],
                residual->chroma_ac[0], qp_cb) < 0 ||
            add_p_chroma_residual(cr, 8u, residual->chroma_dc[1],
                residual->chroma_ac[1], qp_cr) < 0)
            return -2;
    }
    return crtube_h264_frame_put_macroblock(current, mb_x, mb_y, y, cb, cr);
}


int crtube_h264_reconstruct_p_partitioned(
    crtube_h264_frame_t *current, const crtube_h264_dpb_t *dpb,
    const crtube_h264_p_mb_header_t *mb,
    const crtube_h264_p_partition_t *partitions, unsigned partition_count,
    const crtube_h264_pred_weight_t *weights,
    const crtube_h264_p_mb_residual_t *residual,
    unsigned qp_y, int chroma_qp_index_offset,
    int second_chroma_qp_index_offset)
{
    uint8_t y[256], cb[64], cr[64];
    unsigned mb_width, mb_x, mb_y, block;
    unsigned qp_cb, qp_cr;
    if (!current || !current->storage || !dpb || !dpb->valid ||
        !mb || !partitions || !partition_count || !residual ||
        qp_y > 51u || mb->ref_idx_l0 != 0u ||
        chroma_qp_index_offset < -12 || chroma_qp_index_offset > 12 ||
        second_chroma_qp_index_offset < -12 ||
        second_chroma_qp_index_offset > 12 ||
        current->width != dpb->reference.width ||
        current->height != dpb->reference.height ||
        (current->width & 15u) || (current->height & 15u))
        return -1;
    if (mb->coded_block_pattern > 47u) return -1;
    mb_width = current->width / 16u;
    if (mb->mb_address >= mb_width * (current->height / 16u))
        return -1;
    mb_x = mb->mb_address % mb_width;
    mb_y = mb->mb_address / mb_width;
    if (crtube_h264_motion_compensate_partitions(
            &dpb->reference, mb_x, mb_y, partitions, partition_count,
            weights, y, cb, cr) < 0)
        return -1;

    if (mb->coded_block_pattern & 15u) {
        static const uint8_t bx[16] = {
            0,1,0,1,2,3,2,3,0,1,0,1,2,3,2,3
        };
        static const uint8_t by[16] = {
            0,0,1,1,0,0,1,1,2,2,3,3,2,2,3,3
        };
        for (block = 0; block < 16u; ++block) {
            unsigned group = ((by[block] >> 1u) * 2u) +
                             (bx[block] >> 1u);
            if (mb->coded_block_pattern & (1u << group)) {
                if (add_p_residual4x4(y, 16u, bx[block] * 4u,
                        by[block] * 4u, residual->luma_levels[block],
                        qp_y) < 0)
                    return -2;
            }
        }
    }

    qp_cb = crtube_h264_chroma_qp(qp_y, chroma_qp_index_offset);
    qp_cr = crtube_h264_chroma_qp(qp_y, second_chroma_qp_index_offset);
    if ((mb->coded_block_pattern >> 4u) != 0u) {
        if (add_p_chroma_residual(cb, 8u, residual->chroma_dc[0],
                residual->chroma_ac[0], qp_cb) < 0 ||
            add_p_chroma_residual(cr, 8u, residual->chroma_dc[1],
                residual->chroma_ac[1], qp_cr) < 0)
            return -2;
    }
    return crtube_h264_frame_put_macroblock(current, mb_x, mb_y, y, cb, cr);
}
