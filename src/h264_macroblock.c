#include "h264_decoder.h"

#include <string.h>

static uint8_t sample_or_mid(const uint8_t *samples, unsigned index,
                             unsigned available)
{
    return available && samples ? samples[index] : 128;
}

static int mb_block_index_at(unsigned x, unsigned y)
{
    static const uint8_t block_x[16] = {
        0,1,0,1,2,3,2,3,0,1,0,1,2,3,2,3
    };
    static const uint8_t block_y[16] = {
        0,0,1,1,0,0,1,1,2,2,3,3,2,2,3,3
    };
    unsigned i;
    for (i = 0; i < 16; ++i)
        if (block_x[i] == x && block_y[i] == y) return (int)i;
    return -1;
}

int crtube_h264_reconstruct_intra4x4_mb(const uint8_t top[20],
                                         const uint8_t left[16],
                                         uint8_t top_left,
                                         unsigned have_top,
                                         unsigned have_top_right,
                                         unsigned have_left,
                                         unsigned have_top_left,
                                         const uint8_t modes[16],
                                         const int16_t levels[16][16],
                                         unsigned qp,
                                         const uint8_t scaling[16],
                                         uint8_t output[256])
{
    static const uint8_t block_x[16] = {
        0,1,0,1,2,3,2,3,0,1,0,1,2,3,2,3
    };
    static const uint8_t block_y[16] = {
        0,0,1,1,0,0,1,1,2,2,3,3,2,2,3,3
    };
    unsigned block;
    if (!modes || !levels || !output || qp > 51 ||
        (have_top && !top) || (have_left && !left) ||
        (have_top_right && !have_top)) return -1;

    memset(output, 128, 256);
    for (block = 0; block < 16; ++block) {
        unsigned bx = (unsigned)block_x[block] * 4u;
        unsigned by = (unsigned)block_y[block] * 4u;
        unsigned x, y;
        unsigned top_available = (by != 0) || have_top;
        unsigned left_available = (bx != 0) || have_left;
        unsigned top_left_available;
        uint8_t top_ref[8], left_ref[4], prediction[16], reconstructed[16];
        int16_t raster[16], quantized[16], residual[16];

        if (bx && by) top_left_available = 1;
        else if (bx) top_left_available = have_top;
        else if (by) top_left_available = have_left;
        else top_left_available = have_top_left;

        for (x = 0; x < 8; ++x) {
            unsigned gx = bx + x;
            if (by) {
                if (gx < bx + 4u) {
                    top_ref[x] = output[(by - 1) * 16 + gx];
                } else {
                    int top_right_block = gx < 16
                        ? mb_block_index_at(gx / 4u, (by / 4u) - 1u) : -1;
                    if (top_right_block >= 0 &&
                        (unsigned)top_right_block < block)
                        top_ref[x] = output[(by - 1) * 16 + gx];
                    else
                        top_ref[x] = output[(by - 1) * 16 + bx + 3u];
                }
            } else if (gx < 16) {
                top_ref[x] = sample_or_mid(top, gx, have_top);
            } else if (have_top && have_top_right) {
                top_ref[x] = top[gx];
            } else {
                top_ref[x] = sample_or_mid(top, 15, have_top);
            }
        }

        for (y = 0; y < 4; ++y) {
            if (bx)
                left_ref[y] = output[(by + y) * 16 + bx - 1];
            else
                left_ref[y] = sample_or_mid(left, by + y, have_left);
        }

        if (bx && by)
            top_left = output[(by - 1) * 16 + bx - 1];
        else if (bx)
            top_left = sample_or_mid(top, bx - 1, have_top);
        else if (by)
            top_left = sample_or_mid(left, by - 1, have_left);
        else if (!have_top_left)
            top_left = 128;

        if (crtube_h264_intra_predict4x4(modes[block], top_ref, left_ref,
                                         top_left, top_available,
                                         left_available, top_left_available,
                                         prediction) < 0)
            return -2;

        crtube_h264_scan4x4_to_raster(levels[block], raster);
        if (crtube_h264_inverse_quant4x4(raster, qp, scaling, quantized) < 0 ||
            crtube_h264_inverse_transform4x4(quantized, residual) < 0)
            return -1;
        crtube_h264_add_residual4x4(prediction, residual, reconstructed);

        for (y = 0; y < 4; ++y)
            for (x = 0; x < 4; ++x)
                output[(by + y) * 16 + bx + x] = reconstructed[y * 4 + x];
    }
    return 0;
}


/* Reconstruct one parsed Intra_4x4 macroblock into a planar YUV420 frame.
   The frame contains already reconstructed raster-order neighbors. */
int crtube_h264_reconstruct_i4x4_mb_to_frame(
    crtube_h264_frame_t *frame,
    const crtube_h264_i4x4_mb_data_t *mb,
    int chroma_qp_index_offset)
{
    uint8_t top_y[20], left_y[16], top_cb[8], left_cb[8];
    uint8_t top_cr[8], left_cr[8];
    uint8_t luma[256], cb[64], cr[64];
    uint8_t top_left_y = 128, top_left_cb = 128, top_left_cr = 128;
    unsigned mb_x, mb_y, have_top, have_left, have_top_right;
    unsigned x, y, qp_c;
    size_t y_stride, c_stride;

    if (!frame || !frame->y || !frame->cb || !frame->cr || !mb ||
        !frame->width || !frame->height ||
        (frame->width & 15u) || (frame->height & 15u) ||
        mb->mb_address >= (frame->width / 16u) * (frame->height / 16u) ||
        mb->qp_y > 51u || mb->chroma_pred_mode > 3u)
        return -1;

    mb_x = mb->mb_address % (frame->width / 16u);
    mb_y = mb->mb_address / (frame->width / 16u);
    have_top = mb->slice_have_top != 0u;
    have_left = mb->slice_have_left != 0u;
    have_top_right = mb->slice_have_top_right != 0u;
    y_stride = frame->width;
    c_stride = frame->chroma_width;

    for (x = 0; x < 20u; ++x) {
        unsigned sx = mb_x * 16u + x;
        if (have_top && sx < frame->width)
            top_y[x] = frame->y[(size_t)(mb_y * 16u - 1u) * y_stride + sx];
        else if (have_top && x >= 16u && have_top_right)
            top_y[x] = frame->y[(size_t)(mb_y * 16u - 1u) * y_stride + sx];
        else
            top_y[x] = have_top ? frame->y[(size_t)(mb_y * 16u - 1u) *
                         y_stride + mb_x * 16u + 15u] : 128u;
    }
    for (y = 0; y < 16u; ++y)
        left_y[y] = have_left
            ? frame->y[(size_t)(mb_y * 16u + y) * y_stride + mb_x * 16u - 1u]
            : 128u;

    if (have_top && have_left)
        top_left_y = frame->y[(size_t)(mb_y * 16u - 1u) * y_stride +
                              mb_x * 16u - 1u];

    for (x = 0; x < 8u; ++x) {
        size_t pos = (size_t)(mb_y * 8u - (have_top ? 1u : 0u)) * c_stride +
                     mb_x * 8u + x;
        top_cb[x] = have_top ? frame->cb[pos] : 128u;
        top_cr[x] = have_top ? frame->cr[pos] : 128u;
    }
    for (y = 0; y < 8u; ++y) {
        size_t pos = (size_t)(mb_y * 8u + y) * c_stride + mb_x * 8u - 1u;
        left_cb[y] = have_left ? frame->cb[pos] : 128u;
        left_cr[y] = have_left ? frame->cr[pos] : 128u;
    }
    if (have_top && have_left) {
        size_t pos = (size_t)(mb_y * 8u - 1u) * c_stride + mb_x * 8u - 1u;
        top_left_cb = frame->cb[pos];
        top_left_cr = frame->cr[pos];
    }

    if (crtube_h264_reconstruct_intra4x4_mb(
            top_y, left_y, top_left_y, have_top, have_top_right,
            have_left, have_top && have_left, mb->modes, mb->luma_levels,
            mb->qp_y, NULL, luma) < 0)
        return -2;

    qp_c = crtube_h264_chroma_qp(mb->qp_y, chroma_qp_index_offset);
    if (crtube_h264_reconstruct_chroma8x8(
            mb->chroma_pred_mode, top_cb, left_cb, top_left_cb,
            have_top, have_left, have_top && have_left,
            mb->chroma_dc[0], mb->chroma_ac[0], qp_c, cb) < 0 ||
        crtube_h264_reconstruct_chroma8x8(
            mb->chroma_pred_mode, top_cr, left_cr, top_left_cr,
            have_top, have_left, have_top && have_left,
            mb->chroma_dc[1], mb->chroma_ac[1], qp_c, cr) < 0)
        return -3;

    return crtube_h264_frame_put_macroblock(frame, mb_x, mb_y,
                                             luma, cb, cr);
}
