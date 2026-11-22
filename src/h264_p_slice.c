#include "h264_decoder.h"

#include <string.h>

#define P_MAX_MB_WIDTH 30u
#define P_MAX_MB_HEIGHT 17u
#define P_MAX_MBS (P_MAX_MB_WIDTH * P_MAX_MB_HEIGHT)

typedef struct {
    int x;
    int y;
    unsigned available;
} p_mv_t;

static int p_median(int a, int b, int c)
{
    if (a > b) { int t = a; a = b; b = t; }
    if (b > c) { int t = b; b = c; c = t; }
    if (a > b) b = a;
    return b;
}

static p_mv_t p_neighbor(const int *mv_x, const int *mv_y,
                         const uint8_t *decoded, unsigned address, unsigned total)
{
    p_mv_t out;
    out.x = 0;
    out.y = 0;
    out.available = address < total && decoded[address] != 0u;
    if (out.available) {
        out.x = mv_x[address];
        out.y = mv_y[address];
    }
    return out;
}

static void p_predict_mv(const int *mv_x, const int *mv_y,
                         const uint8_t *decoded, unsigned address, unsigned mb_width,
                         unsigned total, int *pred_x, int *pred_y,
                         p_mv_t *left_out, p_mv_t *top_out)
{
    unsigned x = address % mb_width;
    unsigned y = address / mb_width;
    p_mv_t a = p_neighbor(mv_x, mv_y, decoded,
        x ? address - 1u : total, total);
    p_mv_t b = p_neighbor(mv_x, mv_y, decoded,
        y ? address - mb_width : total, total);
    p_mv_t c;

    if (x + 1u < mb_width && y)
        c = p_neighbor(mv_x, mv_y, decoded, address - mb_width + 1u, total);
    else {
        c.x = c.y = 0;
        c.available = 0;
    }
    /* H.264 uses the above-left block D when C (above-right) is
       unavailable, including when C belongs to a different slice. */
    if (!c.available && x && y)
        c = p_neighbor(mv_x, mv_y, decoded,
                       address - mb_width - 1u, total);

    if (left_out) *left_out = a;
    if (top_out) *top_out = b;

    if ((unsigned)a.available + (unsigned)b.available +
        (unsigned)c.available == 1u) {
        p_mv_t only = a.available ? a : (b.available ? b : c);
        *pred_x = only.x;
        *pred_y = only.y;
    } else {
        *pred_x = p_median(a.x, b.x, c.x);
        *pred_y = p_median(a.y, b.y, c.y);
    }
}

static int p_is_trailing(const uint8_t *rbsp, size_t size, size_t bit)
{
    size_t i;
    if (!rbsp || bit >= size * 8u ||
        ((rbsp[bit >> 3] >> (7u - (bit & 7u))) & 1u) == 0u)
        return 0;
    for (i = bit + 1u; i < size * 8u; ++i)
        if ((rbsp[i >> 3] >> (7u - (i & 7u))) & 1u) return 0;
    return 1;
}

static int p_reconstruct_prediction_weighted(
    crtube_h264_frame_t *current, const crtube_h264_dpb_t *dpb,
    const crtube_h264_p_mb_header_t *header,
    const crtube_h264_pred_weight_t *weights)
{
    crtube_h264_p_partition_t partition;
    uint8_t y[256], cb[64], cr[64];
    unsigned mb_width, mb_x, mb_y;
    if (!current || !dpb || !header || !dpb->valid ||
        header->ref_idx_l0 != 0u) return -1;
    mb_width = current->width / 16u;
    if (!mb_width || header->mb_address >= mb_width * (current->height / 16u))
        return -1;
    memset(&partition, 0, sizeof(partition));
    partition.width4 = partition.height4 = 4u;
    partition.mv_x_qpel = header->mv_l0_x;
    partition.mv_y_qpel = header->mv_l0_y;
    mb_x = header->mb_address % mb_width;
    mb_y = header->mb_address / mb_width;
    if (crtube_h264_motion_compensate_partitions(&dpb->reference, mb_x, mb_y,
            &partition, 1u, weights, y, cb, cr) < 0)
        return -1;
    return crtube_h264_frame_put_macroblock(current, mb_x, mb_y, y, cb, cr);
}

int crtube_h264_decode_p_slice_data_weighted(
    const uint8_t *rbsp, size_t rbsp_size, size_t bit_offset,
    crtube_h264_frame_t *current, const crtube_h264_dpb_t *dpb,
    unsigned first_mb_in_slice, unsigned num_ref_idx_l0_active,
    const crtube_h264_pred_weight_t *weights, unsigned weight_count,
    unsigned *qp_y, int chroma_qp_index_offset,
    int second_chroma_qp_index_offset)
{
    int mv_x[P_MAX_MBS], mv_y[P_MAX_MBS];
    uint8_t decoded_map[P_MAX_MBS];
    uint8_t top_luma[P_MAX_MB_WIDTH][4];
    uint8_t top_cb[P_MAX_MB_WIDTH][2], top_cr[P_MAX_MB_WIDTH][2];
    uint8_t left_luma[4], left_cb[2], left_cr[2];
    unsigned mb_width, mb_height, total, address, decoded = 0;
    unsigned qp;

    if (!rbsp || !rbsp_size || !current || !current->storage || !dpb ||
        !dpb->valid || !qp_y || rbsp_size > SIZE_MAX / 8u || bit_offset > rbsp_size * 8u ||
        !current->width || !current->height ||
        (current->width & 15u) || (current->height & 15u) ||
        current->width != dpb->reference.width ||
        current->height != dpb->reference.height ||
        !num_ref_idx_l0_active || num_ref_idx_l0_active > 1u ||
        (weights && weight_count != num_ref_idx_l0_active) ||
        (!weights && weight_count != 0u) || *qp_y > 51u || chroma_qp_index_offset < -12 ||
        chroma_qp_index_offset > 12 || second_chroma_qp_index_offset < -12 ||
        second_chroma_qp_index_offset > 12)
        return -1;

    mb_width = current->width / 16u;
    mb_height = current->height / 16u;
    if (!mb_width || mb_width > P_MAX_MB_WIDTH ||
        !mb_height || mb_height > P_MAX_MB_HEIGHT)
        return -2;
    total = mb_width * mb_height;
    if (first_mb_in_slice >= total)
        return -2;

    memset(mv_x, 0, sizeof(mv_x));
    memset(mv_y, 0, sizeof(mv_y));
    memset(decoded_map, 0, sizeof(decoded_map));
    memset(top_luma, 0, sizeof(top_luma));
    memset(top_cb, 0, sizeof(top_cb));
    memset(top_cr, 0, sizeof(top_cr));
    memset(left_luma, 0, sizeof(left_luma));
    memset(left_cb, 0, sizeof(left_cb));
    memset(left_cr, 0, sizeof(left_cr));
    address = first_mb_in_slice;
    qp = *qp_y;

    while (address < total && !p_is_trailing(rbsp, rbsp_size, bit_offset)) {
        unsigned before_address = address;
        crtube_h264_p_mb_header_t header;
        int pred_x, pred_y;
        p_mv_t left_mv, top_mv;
        int parsed;

        p_predict_mv(mv_x, mv_y, decoded_map, address, mb_width, total,
                     &pred_x, &pred_y, &left_mv, &top_mv);
        parsed = crtube_h264_parse_p_slice_mb(
            rbsp, rbsp_size, &bit_offset, &address,
            num_ref_idx_l0_active, pred_x, pred_y, &header);
        if (parsed <= 0) return parsed == 0 ? (int)decoded : -2;
        if (header.skipped) {
            unsigned run = header.skip_run;
            unsigned i;
            if (!run || run > total - before_address) return -1;
            for (i = 0; i < run; ++i) {
                unsigned mb = before_address + i;
                unsigned x = mb % mb_width;
                unsigned y = mb / mb_width;
                int skip_x = 0, skip_y = 0;
                p_mv_t a, b;
                p_predict_mv(mv_x, mv_y, decoded_map, mb, mb_width, total,
                             &skip_x, &skip_y, &a, &b);
                /* P_Skip has a zero vector only when both A and B
                   independently satisfy the zero/unavailable condition.
                   A single zero neighbor must not suppress the median MVP. */
                if ((!a.available || (a.x == 0 && a.y == 0)) &&
                    (!b.available || (b.x == 0 && b.y == 0)))
                    skip_x = skip_y = 0;
                memset(&header, 0, sizeof(header));
                header.mb_address = mb;
                header.skipped = 1u;
                header.ref_idx_l0 = 0u;
                header.mv_l0_x = skip_x;
                header.mv_l0_y = skip_y;
                if (p_reconstruct_prediction_weighted(current, dpb, &header,
                        weights ? &weights[0] : NULL) < 0) return -2;
                mv_x[mb] = skip_x;
                mv_y[mb] = skip_y;
                decoded_map[mb] = 1u;
                if (x == 0u) {
                    memset(left_luma, 0, sizeof(left_luma));
                    memset(left_cb, 0, sizeof(left_cb));
                    memset(left_cr, 0, sizeof(left_cr));
                }
                memset(left_luma, 0, sizeof(left_luma));
                memset(left_cb, 0, sizeof(left_cb));
                memset(left_cr, 0, sizeof(left_cr));
                memset(top_luma[x], 0, sizeof(top_luma[x]));
                memset(top_cb[x], 0, sizeof(top_cb[x]));
                memset(top_cr[x], 0, sizeof(top_cr[x]));
                ++decoded;
                (void)y;
            }
            continue;
        }

        if (header.mb_address != before_address || header.mb_type != 0u ||
            header.ref_idx_l0 != 0u)
            return -2;
        {
            crtube_h264_p_mb_residual_t residual;
            uint8_t *left_y = (before_address % mb_width) ? left_luma : NULL;
            uint8_t *top_y = (before_address / mb_width) ?
                top_luma[before_address % mb_width] : NULL;
            uint8_t *left_u = (before_address % mb_width) ? left_cb : NULL;
            uint8_t *top_u = (before_address / mb_width) ?
                top_cb[before_address % mb_width] : NULL;
            uint8_t *left_v = (before_address % mb_width) ? left_cr : NULL;
            uint8_t *top_v = (before_address / mb_width) ?
                top_cr[before_address % mb_width] : NULL;
            unsigned next_qp = qp;
            unsigned x = before_address % mb_width;

            if (header.coded_block_pattern) {
                int q = (int)qp + header.mb_qp_delta;
                q %= 52;
                if (q < 0) q += 52;
                next_qp = (unsigned)q;
                if (crtube_h264_parse_p_mb_residual(
                        rbsp, rbsp_size, &bit_offset,
                        header.coded_block_pattern,
                        left_y, top_y, left_u, top_u, left_v, top_v,
                        left_y != NULL, top_y != NULL, &residual) < 0)
                    return -1;
            } else {
                memset(&residual, 0, sizeof(residual));
            }
            {
                crtube_h264_p_partition_t partition;
                memset(&partition, 0, sizeof(partition));
                partition.width4 = partition.height4 = 4u;
                partition.mv_x_qpel = header.mv_l0_x;
                partition.mv_y_qpel = header.mv_l0_y;
                if (crtube_h264_reconstruct_p_partitioned(
                        current, dpb, &header, &partition, 1u,
                        weights ? &weights[0] : NULL, &residual, next_qp,
                        chroma_qp_index_offset,
                        second_chroma_qp_index_offset) < 0)
                    return -2;
            }
            qp = next_qp;
            mv_x[before_address] = header.mv_l0_x;
            mv_y[before_address] = header.mv_l0_y;
            decoded_map[before_address] = 1u;
            if (crtube_h264_p_mb_nz_edges(
                    &residual, left_luma, top_luma[x], left_cb, top_cb[x],
                    left_cr, top_cr[x]) < 0)
                return -1;
            ++decoded;
        }
    }

    if (!p_is_trailing(rbsp, rbsp_size, bit_offset))
        return -1;
    *qp_y = qp;
    return (int)decoded;
}


int crtube_h264_decode_p_slice_data(
    const uint8_t *rbsp, size_t rbsp_size, size_t bit_offset,
    crtube_h264_frame_t *current, const crtube_h264_dpb_t *dpb,
    unsigned first_mb_in_slice, unsigned num_ref_idx_l0_active,
    unsigned *qp_y, int chroma_qp_index_offset,
    int second_chroma_qp_index_offset)
{
    return crtube_h264_decode_p_slice_data_weighted(
        rbsp, rbsp_size, bit_offset, current, dpb, first_mb_in_slice,
        num_ref_idx_l0_active, NULL, 0u, qp_y, chroma_qp_index_offset,
        second_chroma_qp_index_offset);
}
