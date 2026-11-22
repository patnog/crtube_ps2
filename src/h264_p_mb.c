#include "h264_decoder.h"

#include <limits.h>
#include <string.h>

typedef struct {
    const uint8_t *data;
    size_t size;
    size_t bit;
} p_br_t;

static int p_bit(p_br_t *br, unsigned *v)
{
    if (!br || !v || br->size > SIZE_MAX / 8u || br->bit >= br->size * 8u) return -1;
    *v = (br->data[br->bit >> 3] >> (7u - (br->bit & 7u))) & 1u;
    ++br->bit;
    return 0;
}

static int p_bits(p_br_t *br, unsigned n, unsigned *v)
{
    unsigned i, x = 0, b;
    if (!br || !v || n > 32u || br->bit > br->size * 8u ||
        n > br->size * 8u - br->bit) return -1;
    for (i = 0; i < n; ++i) {
        if (p_bit(br, &b) < 0) return -1;
        x = (x << 1) | b;
    }
    *v = x;
    return 0;
}

static int p_ue(p_br_t *br, unsigned *v)
{
    unsigned zeros = 0, b, suffix = 0;
    while (1) {
        if (p_bit(br, &b) < 0) return -1;
        if (b) break;
        if (++zeros > 31u) return -1;
    }
    if (zeros && p_bits(br, zeros, &suffix) < 0) return -1;
    if (zeros == 31u && suffix == UINT_MAX) return -1;
    *v = ((1u << zeros) - 1u) + suffix;
    return 0;
}

static int p_se(p_br_t *br, int *v)
{
    unsigned code;
    int64_t value;
    if (p_ue(br, &code) < 0) return -1;
    value = (code & 1u) ? (int64_t)(code + 1u) / 2 :
                         -(int64_t)(code / 2u);
    if (value < INT_MIN || value > INT_MAX) return -1;
    *v = (int)value;
    return 0;
}

static int p_trailing(const uint8_t *data, size_t size, size_t bit)
{
    size_t i;
    unsigned b;
    if (!data || bit >= size * 8u) return 0;
    b = (data[bit >> 3] >> (7u - (bit & 7u))) & 1u;
    if (b != 1u) return 0;
    for (i = bit + 1u; i < size * 8u; ++i)
        if ((data[i >> 3] >> (7u - (i & 7u))) & 1u) return 0;
    return 1;
}

int crtube_h264_parse_p_mb_header(
    const uint8_t *rbsp, size_t rbsp_size, size_t *bit_offset,
    unsigned mb_address, unsigned num_ref_idx_l0_active,
    int mvp_l0_x, int mvp_l0_y, crtube_h264_p_mb_header_t *header)
{
    p_br_t br;
    crtube_h264_p_mb_header_t out;
    static const uint8_t inter_cbp[48] = {
        0,16,1,2,4,8,32,3,5,10,12,15,47,7,11,13,
        14,6,9,31,35,37,42,44,33,34,36,40,39,43,45,46,
        17,18,20,24,19,21,26,28,23,27,29,30,22,25,38,41
    };
    unsigned mb_type, ref = 0, cbp_code = 0, cbp;
    int dx, dy, qp_delta = 0;
    if (!rbsp || !rbsp_size || !bit_offset ||
        rbsp_size > SIZE_MAX / 8u || *bit_offset > rbsp_size * 8u || !num_ref_idx_l0_active ||
        num_ref_idx_l0_active > 32u || !header) return -1;
    memset(&out, 0, sizeof(out));
    br.data = rbsp; br.size = rbsp_size; br.bit = *bit_offset;
    if (p_ue(&br, &mb_type) < 0) return -1;
    if (mb_type != 0u) return -2; /* P_L0_16x16 only */
    if (num_ref_idx_l0_active > 1u && p_ue(&br, &ref) < 0) return -1;
    if (ref >= num_ref_idx_l0_active) return -1;
    if (p_se(&br, &dx) < 0 || p_se(&br, &dy) < 0) return -1;
    /* Table 9-4: inter prediction coded_block_pattern mapping. */
    if (p_ue(&br, &cbp_code) < 0 || cbp_code >= 48u) return -1;
    cbp = inter_cbp[cbp_code];
    if (cbp != 0u) {
        if (p_se(&br, &qp_delta) < 0 || qp_delta < -26 || qp_delta > 25)
            return -1;
    }
    if ((int64_t)mvp_l0_x + dx < INT_MIN ||
        (int64_t)mvp_l0_x + dx > INT_MAX ||
        (int64_t)mvp_l0_y + dy < INT_MIN ||
        (int64_t)mvp_l0_y + dy > INT_MAX) return -1;
    out.mb_address = mb_address;
    out.mb_type = mb_type;
    out.ref_idx_l0 = ref;
    out.mvd_l0_x = dx; out.mvd_l0_y = dy;
    out.mv_l0_x = mvp_l0_x + dx;
    out.mv_l0_y = mvp_l0_y + dy;
    out.coded_block_pattern = cbp;
    out.mb_qp_delta = qp_delta;
    out.data_bit_offset = br.bit;
    *header = out;
    *bit_offset = br.bit;
    return 0;
}

int crtube_h264_parse_p_slice_mb(
    const uint8_t *rbsp, size_t rbsp_size, size_t *bit_offset,
    unsigned *next_mb_address, unsigned num_ref_idx_l0_active,
    int mvp_l0_x, int mvp_l0_y, crtube_h264_p_mb_header_t *header)
{
    p_br_t br;
    unsigned skip_run, address;
    if (!rbsp || !rbsp_size || !bit_offset || !next_mb_address ||
        !header || rbsp_size > SIZE_MAX / 8u || *bit_offset > rbsp_size * 8u) return -1;
    br.data = rbsp; br.size = rbsp_size; br.bit = *bit_offset;
    address = *next_mb_address;
    if (p_trailing(rbsp, rbsp_size, br.bit)) return 0;
    if (p_ue(&br, &skip_run) < 0 || skip_run > 65535u) return -1;
    if (skip_run) {
        memset(header, 0, sizeof(*header));
        header->mb_address = address;
        header->skipped = 1u;
        header->skip_run = skip_run;
        header->mb_type = UINT_MAX;
        header->mv_l0_x = mvp_l0_x;
        header->mv_l0_y = mvp_l0_y;
        header->data_bit_offset = br.bit;
        if (skip_run > UINT_MAX - address) return -1;
        *bit_offset = br.bit;
        *next_mb_address = address + skip_run;
        return 1;
    }
    if (crtube_h264_parse_p_mb_header(rbsp, rbsp_size, &br.bit, address,
            num_ref_idx_l0_active, mvp_l0_x, mvp_l0_y, header) < 0)
        return -2;
    if (address == UINT_MAX) return -1;
    *bit_offset = br.bit;
    *next_mb_address = address + 1u;
    return 1;
}

int crtube_h264_parse_p_mb_partitions(
    const uint8_t *rbsp, size_t rbsp_size, size_t *bit_offset,
    unsigned mb_address, unsigned num_ref_idx_l0_active,
    const int mvp_x[16], const int mvp_y[16],
    crtube_h264_p_mb_partition_header_t *header)
{
    static const uint8_t inter_cbp[48] = {
        0,16,1,2,4,8,32,3,5,10,12,15,47,7,11,13,
        14,6,9,31,35,37,42,44,33,34,36,40,39,43,45,46,
        17,18,20,24,19,21,26,28,23,27,29,30,22,25,38,41
    };
    p_br_t br;
    crtube_h264_p_mb_partition_header_t out;
    uint8_t sub_type[4] = {0,0,0,0};
    unsigned mb_type, i, cbp_code;
    int qp_delta = 0;
    if (!rbsp || !rbsp_size || rbsp_size > SIZE_MAX / 8u ||
        !bit_offset || *bit_offset > rbsp_size * 8u ||
        !num_ref_idx_l0_active || num_ref_idx_l0_active > 32u || !header)
        return -1;
    memset(&out, 0, sizeof(out));
    br.data = rbsp; br.size = rbsp_size; br.bit = *bit_offset;
    if (p_ue(&br, &mb_type) < 0) return -1;
    if (mb_type > 4u) return -2;
    if (mb_type >= 3u) {
        for (i = 0; i < 4u; ++i) {
            unsigned value;
            if (p_ue(&br, &value) < 0 || value > 3u) return -1;
            sub_type[i] = (uint8_t)value;
        }
    }
    if (crtube_h264_p_partition_layout(mb_type,
            mb_type >= 3u ? sub_type : NULL,
            out.partitions, &out.partition_count) < 0)
        return -1;

    if (mb_type < 3u) {
        /* For 16x16/16x8/8x16, ref_idx and MVD are interleaved per part. */
        for (i = 0; i < out.partition_count; ++i) {
            crtube_h264_p_partition_t *part = &out.partitions[i];
            unsigned ref = 0;
            int dx, dy;
            if (num_ref_idx_l0_active > 1u && p_ue(&br, &ref) < 0)
                return -1;
            if (ref >= num_ref_idx_l0_active ||
                p_se(&br, &dx) < 0 || p_se(&br, &dy) < 0)
                return -1;
            if ((int64_t)(mvp_x ? mvp_x[i] : 0) + dx < INT_MIN ||
                (int64_t)(mvp_x ? mvp_x[i] : 0) + dx > INT_MAX ||
                (int64_t)(mvp_y ? mvp_y[i] : 0) + dy < INT_MIN ||
                (int64_t)(mvp_y ? mvp_y[i] : 0) + dy > INT_MAX)
                return -1;
            part->ref_idx_l0 = ref;
            part->mvd_x = dx; part->mvd_y = dy;
            part->mv_x_qpel = (mvp_x ? mvp_x[i] : 0) + dx;
            part->mv_y_qpel = (mvp_y ? mvp_y[i] : 0) + dy;
        }
    } else {
        /* P_8x8 syntax codes all four reference indices before any MVDs. */
        for (i = 0; i < 4u; ++i) {
            unsigned ref = 0, j;
            if (mb_type == 3u && num_ref_idx_l0_active > 1u &&
                p_ue(&br, &ref) < 0) return -1;
            if (ref >= num_ref_idx_l0_active) return -1;
            for (j = 0; j < out.partition_count; ++j) {
                crtube_h264_p_partition_t *part = &out.partitions[j];
                unsigned sub = ((unsigned)part->y4 >> 1u) * 2u +
                               ((unsigned)part->x4 >> 1u);
                if (sub == i) part->ref_idx_l0 = ref;
            }
        }
        for (i = 0; i < out.partition_count; ++i) {
            crtube_h264_p_partition_t *part = &out.partitions[i];
            int dx, dy;
            if (p_se(&br, &dx) < 0 || p_se(&br, &dy) < 0 ||
                (int64_t)(mvp_x ? mvp_x[i] : 0) + dx < INT_MIN ||
                (int64_t)(mvp_x ? mvp_x[i] : 0) + dx > INT_MAX ||
                (int64_t)(mvp_y ? mvp_y[i] : 0) + dy < INT_MIN ||
                (int64_t)(mvp_y ? mvp_y[i] : 0) + dy > INT_MAX)
                return -1;
            part->mvd_x = dx; part->mvd_y = dy;
            part->mv_x_qpel = (mvp_x ? mvp_x[i] : 0) + dx;
            part->mv_y_qpel = (mvp_y ? mvp_y[i] : 0) + dy;
        }
    }
    if (p_ue(&br, &cbp_code) < 0 || cbp_code >= 48u) return -1;
    out.coded_block_pattern = inter_cbp[cbp_code];
    if (out.coded_block_pattern &&
        (p_se(&br, &qp_delta) < 0 || qp_delta < -26 || qp_delta > 25))
        return -1;
    out.mb_address = mb_address;
    out.mb_type = mb_type;
    out.mb_qp_delta = qp_delta;
    out.data_bit_offset = br.bit;
    *bit_offset = br.bit;
    *header = out;
    return 0;
}
