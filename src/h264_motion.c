
#include "h264_decoder.h"

#include <string.h>

static unsigned clamp_coord(int value, unsigned limit)
{
    if (value < 0) return 0u;
    if ((unsigned)value >= limit) return limit - 1u;
    return (unsigned)value;
}


int crtube_h264_dpb_init(crtube_h264_dpb_t *dpb, uint8_t *storage,
                         size_t storage_size, unsigned width,
                         unsigned height)
{
    if (!dpb || !storage) return -1;
    memset(dpb, 0, sizeof(*dpb));
    if (crtube_h264_frame_init(&dpb->reference, storage, storage_size,
                               width, height) < 0)
        return -1;
    dpb->owned_storage = storage;
    dpb->owned_capacity = storage_size;
    crtube_h264_frame_clear(&dpb->reference, 0, 128, 128);
    return 0;
}

int crtube_h264_dpb_store(crtube_h264_dpb_t *dpb,
                          const crtube_h264_frame_t *frame,
                          unsigned frame_num, int poc)
{
    size_t bytes;
    if (!dpb || !dpb->owned_storage || !frame ||
        !frame->storage || !frame->y || !frame->cb || !frame->cr)
        return -1;
    if (frame->width != dpb->reference.width ||
        frame->height != dpb->reference.height)
        return -2;
    bytes = crtube_h264_frame_bytes(frame->width, frame->height);
    if (!bytes || bytes > dpb->owned_capacity ||
        frame->storage_size < bytes)
        return -3;
    memmove(dpb->owned_storage, frame->storage, bytes);
    if (crtube_h264_frame_init(&dpb->reference, dpb->owned_storage,
                               dpb->owned_capacity, frame->width,
                               frame->height) < 0)
        return -4;
    dpb->frame_num = frame_num;
    dpb->poc = poc;
    dpb->valid = 1u;
    return 0;
}

void crtube_h264_dpb_reset(crtube_h264_dpb_t *dpb)
{
    if (!dpb) return;
    dpb->valid = 0u;
    dpb->frame_num = 0u;
    dpb->poc = 0;
    if (dpb->reference.storage)
        crtube_h264_frame_clear(&dpb->reference, 0, 128, 128);
}


static int sample_clamped(const uint8_t *p, unsigned stride,
                          unsigned width, unsigned height, int x, int y)
{
    x = (int)clamp_coord(x, width);
    y = (int)clamp_coord(y, height);
    return p[(size_t)y * stride + (unsigned)x];
}

static int half_h(const uint8_t *p, unsigned stride,
                  unsigned width, unsigned height, int x, int y)
{
    static const int c[6] = {1, -5, 20, 20, -5, 1};
    int sum = 0, i;
    for (i = 0; i < 6; ++i)
        sum += c[i] * sample_clamped(p, stride, width, height,
                                     x + i - 2, y);
    return (sum + 16) >> 5;
}

static int half_v(const uint8_t *p, unsigned stride,
                  unsigned width, unsigned height, int x, int y)
{
    static const int c[6] = {1, -5, 20, 20, -5, 1};
    int sum = 0, i;
    for (i = 0; i < 6; ++i)
        sum += c[i] * sample_clamped(p, stride, width, height,
                                     x, y + i - 2);
    return (sum + 16) >> 5;
}

static int half_d(const uint8_t *p, unsigned stride,
                  unsigned width, unsigned height, int x, int y)
{
    static const int c[6] = {1, -5, 20, 20, -5, 1};
    int rows[6], i, j, sum = 0;
    for (i = 0; i < 6; ++i) {
        int h = 0;
        for (j = 0; j < 6; ++j)
            h += c[j] * sample_clamped(p, stride, width, height,
                                       x + j - 2, y + i - 2);
        rows[i] = (h + 16) >> 5;
    }
    for (i = 0; i < 6; ++i) sum += c[i] * rows[i];
    return (sum + 16) >> 5;
}

static unsigned clip_sample(int v)
{
    if (v < 0) return 0u;
    if (v > 255) return 255u;
    return (unsigned)v;
}

/* Quarter-pel luma interpolation. Integer and half-pel positions use the
   H.264 six-tap filter; quarter positions average adjacent integer/half
   samples. The diagonal half-pel path uses separable six-tap filtering. */
static unsigned luma_qpel(const uint8_t *p, unsigned stride,
                          unsigned width, unsigned height, int xq, int yq)
{
    int ix = xq >= 0 ? xq / 4 : -((3 - xq) / 4);
    int iy = yq >= 0 ? yq / 4 : -((3 - yq) / 4);
    int fx = xq - ix * 4, fy = yq - iy * 4;
    int a = sample_clamped(p, stride, width, height, ix, iy);
    int b = fx ? (fx == 2 ? half_h(p,stride,width,height,ix,iy) :
                  (fx == 1 ? (a + half_h(p,stride,width,height,ix,iy) + 1) >> 1 :
                   (half_h(p,stride,width,height,ix,iy) +
                    sample_clamped(p,stride,width,height,ix+1,iy) + 1) >> 1)) : a;
    int c, d;
    if (!fy) return clip_sample(b);
    c = fy == 2 ? half_v(p,stride,width,height,ix,iy) :
        (fy == 1 ? (a + half_v(p,stride,width,height,ix,iy) + 1) >> 1 :
         (half_v(p,stride,width,height,ix,iy) +
          sample_clamped(p,stride,width,height,ix,iy+1) + 1) >> 1);
    if (!fx) return clip_sample(c);
    d = fx == 2 ? half_d(p,stride,width,height,ix,iy) :
        (fx == 1 ? (c + half_v(p,stride,width,height,ix+1,iy) + 1) >> 1 :
         (half_v(p,stride,width,height,ix+1,iy) +
          sample_clamped(p,stride,width,height,ix+1,iy+1) + 1) >> 1);
    (void)b;
    return clip_sample(d);
}

static unsigned chroma_qpel(const uint8_t *p, unsigned stride,
                            unsigned width, unsigned height, int x8, int y8)
{
    int ix = x8 >= 0 ? x8 / 8 : -((7 - x8) / 8);
    int iy = y8 >= 0 ? y8 / 8 : -((7 - y8) / 8);
    int fx = x8 - ix * 8, fy = y8 - iy * 8;
    int a = sample_clamped(p,stride,width,height,ix,iy);
    int b = sample_clamped(p,stride,width,height,ix+1,iy);
    int c = sample_clamped(p,stride,width,height,ix,iy+1);
    int d = sample_clamped(p,stride,width,height,ix+1,iy+1);
    int top = a * (8-fx) + b * fx;
    int bot = c * (8-fx) + d * fx;
    return (unsigned)((top * (8-fy) + bot * fy + 32) >> 6);
}

static unsigned apply_uni_weight(unsigned sample, int weight, int offset,
                                  unsigned denom)
{
    int64_t value;
    if (!denom) value = (int64_t)weight * sample + offset;
    else value = (((int64_t)weight * sample +
                   ((int64_t)1 << (denom - 1u))) >> denom) + offset;
    if (value < 0) return 0u;
    if (value > 255) return 255u;
    return (unsigned)value;
}

int crtube_h264_motion_compensate_partitions(
    const crtube_h264_frame_t *reference, unsigned mb_x, unsigned mb_y,
    const crtube_h264_p_partition_t *partitions, unsigned partition_count,
    const crtube_h264_pred_weight_t *weights,
    uint8_t out_y[256], uint8_t out_cb[64], uint8_t out_cr[64])
{
    int mv_x[16], mv_y[16];
    uint8_t coverage[16] = {0};
    unsigned p, x, y, row, col;
    unsigned lden = 0, cden = 0;
    int lw = 1, lo = 0, cbw = 1, cbo = 0, crw = 1, cro = 0;
    if (!reference || !reference->y || !reference->cb || !reference->cr ||
        !partitions || !partition_count || partition_count > 16u ||
        !out_y || !out_cb || !out_cr || !reference->width ||
        !reference->height || (reference->width & 15u) ||
        (reference->height & 15u) || mb_x >= reference->width / 16u ||
        mb_y >= reference->height / 16u)
        return -1;
    if (weights && weights->enabled) {
        if (weights->luma_denom > 7u || weights->chroma_denom > 7u ||
            weights->luma_weight < -128 || weights->luma_weight > 127 ||
            weights->luma_offset < -128 || weights->luma_offset > 127 ||
            weights->chroma_weight_cb < -128 || weights->chroma_weight_cb > 127 ||
            weights->chroma_offset_cb < -128 || weights->chroma_offset_cb > 127 ||
            weights->chroma_weight_cr < -128 || weights->chroma_weight_cr > 127 ||
            weights->chroma_offset_cr < -128 || weights->chroma_offset_cr > 127)
            return -1;
        lden = weights->luma_denom;
        cden = weights->chroma_denom;
        lw = weights->luma_weight; lo = weights->luma_offset;
        cbw = weights->chroma_weight_cb; cbo = weights->chroma_offset_cb;
        crw = weights->chroma_weight_cr; cro = weights->chroma_offset_cr;
    } else {
        /* Default uni-prediction is the identity transform. */
        lw = cbw = crw = 1;
    }
    for (p = 0; p < partition_count; ++p) {
        const crtube_h264_p_partition_t *part = &partitions[p];
        if (part->ref_idx_l0 != 0u || !part->width4 || !part->height4 ||
            part->x4 + part->width4 > 4u || part->y4 + part->height4 > 4u)
            return -1;
        for (y = part->y4; y < (unsigned)part->y4 + part->height4; ++y) {
            for (x = part->x4; x < (unsigned)part->x4 + part->width4; ++x) {
                unsigned cell = y * 4u + x;
                if (coverage[cell]++) return -1;
                mv_x[cell] = part->mv_x_qpel;
                mv_y[cell] = part->mv_y_qpel;
            }
        }
    }
    for (p = 0; p < 16u; ++p) if (coverage[p] != 1u) return -1;

    for (row = 0; row < 16u; ++row) {
        for (col = 0; col < 16u; ++col) {
            unsigned cell = (row >> 2u) * 4u + (col >> 2u);
            int xq = (int)(mb_x * 16u + col) * 4 + mv_x[cell];
            int yq = (int)(mb_y * 16u + row) * 4 + mv_y[cell];
            unsigned sample = luma_qpel(reference->y, reference->width,
                reference->width, reference->height, xq, yq);
            if (weights && weights->enabled)
                sample = apply_uni_weight(sample, lw, lo, lden);
            out_y[row * 16u + col] = (uint8_t)sample;
        }
    }
    for (row = 0; row < 8u; ++row) {
        for (col = 0; col < 8u; ++col) {
            unsigned cell = (row >> 1u) * 4u + (col >> 1u);
            int mvx = mv_x[cell], mvy = mv_y[cell];
            int x8 = (int)(mb_x * 8u + col) * 8 + mvx * 2;
            int y8 = (int)(mb_y * 8u + row) * 8 + mvy * 2;
            unsigned u = chroma_qpel(reference->cb, reference->chroma_width,
                reference->chroma_width, reference->chroma_height, x8, y8);
            unsigned v = chroma_qpel(reference->cr, reference->chroma_width,
                reference->chroma_width, reference->chroma_height, x8, y8);
            if (weights && weights->enabled) {
                u = apply_uni_weight(u, cbw, cbo, cden);
                v = apply_uni_weight(v, crw, cro, cden);
            }
            out_cb[row * 8u + col] = (uint8_t)u;
            out_cr[row * 8u + col] = (uint8_t)v;
        }
    }
    return 0;
}

int crtube_h264_motion_compensate_16x16(
    const crtube_h264_frame_t *reference,
    unsigned mb_x, unsigned mb_y,
    int mv_x_qpel, int mv_y_qpel,
    uint8_t out_y[256], uint8_t out_cb[64], uint8_t out_cr[64])
{
    crtube_h264_p_partition_t partition;
    memset(&partition, 0, sizeof(partition));
    partition.width4 = 4u;
    partition.height4 = 4u;
    partition.mv_x_qpel = mv_x_qpel;
    partition.mv_y_qpel = mv_y_qpel;
    return crtube_h264_motion_compensate_partitions(reference, mb_x, mb_y,
        &partition, 1u, NULL, out_y, out_cb, out_cr);
}
