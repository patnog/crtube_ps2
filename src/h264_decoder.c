#include "h264_decoder.h"

#include <limits.h>
#include <string.h>

#define H264_NAL_SPS 7
#define H264_NAL_PPS 8
#define H264_NAL_IDR 5
#define H264_NAL_SLICE 1

typedef struct bit_reader {
    const uint8_t *data;
    size_t size;
    size_t bit;
} bit_reader_t;

static int find_start_code(const uint8_t *data, size_t size, size_t from,
                           size_t *start, size_t *prefix)
{
    size_t i;
    if (!data || !start || !prefix || from > size) return 0;
    for (i = from; i + 2 < size; ++i) {
        if (data[i] != 0 || data[i + 1] != 0) continue;
        if (data[i + 2] == 1) {
            *start = i; *prefix = 3; return 1;
        }
        if (i + 3 < size && data[i + 2] == 0 && data[i + 3] == 1) {
            *start = i; *prefix = 4; return 1;
        }
    }
    return 0;
}

int crtube_h264_annexb_next(const uint8_t *data, size_t size, size_t *offset,
                            crtube_h264_nal_t *nal)
{
    size_t start, prefix, next = 0, next_prefix = 0, end;
    int have_next;
    if (!data || !offset || !nal || *offset > size) return -1;
    if (!find_start_code(data, size, *offset, &start, &prefix)) {
        *offset = size;
        return 0;
    }
    have_next = find_start_code(data, size, start + prefix, &next, &next_prefix);
    if (!have_next) end = size;
    else end = next;
    while (end > start + prefix && data[end - 1] == 0) --end;
    if (end <= start + prefix) {
        *offset = have_next ? next : size;
        return -1;
    }
    nal->data = data + start + prefix;
    nal->size = end - (start + prefix);
    nal->type = (uint8_t)(nal->data[0] & 0x1f);
    nal->ref_idc = (uint8_t)((nal->data[0] >> 5) & 3);
    *offset = have_next ? next : size;
    return 1;
}

static int read_bit(bit_reader_t *br, unsigned *value)
{
    if (!br || !value || br->size > SIZE_MAX / 8u || br->bit >= br->size * 8u) return -1;
    *value = (br->data[br->bit >> 3] >> (7 - (br->bit & 7))) & 1u;
    ++br->bit;
    return 0;
}

static int read_bits(bit_reader_t *br, unsigned count, unsigned *value)
{
    unsigned i, bit, result = 0;
    if (!value || count > 32) return -1;
    for (i = 0; i < count; ++i) {
        if (read_bit(br, &bit) < 0) return -1;
        result = (result << 1) | bit;
    }
    *value = result;
    return 0;
}

static int read_ue(bit_reader_t *br, unsigned *value)
{
    unsigned zeros = 0, bit, suffix = 0;
    while (1) {
        if (read_bit(br, &bit) < 0) return -1;
        if (bit) break;
        if (++zeros > 31) return -1;
    }
    if (zeros && read_bits(br, zeros, &suffix) < 0) return -1;
    *value = ((1u << zeros) - 1u) + suffix;
    return 0;
}

static int read_se(bit_reader_t *br, int *value)
{
    unsigned code;
    int64_t mapped;
    if (!value || read_ue(br, &code) < 0) return -1;
    mapped = (code & 1u) ? (int64_t)(((uint64_t)code + 1u) >> 1)
                         : -(int64_t)(code >> 1);
    if (mapped < INT_MIN || mapped > INT_MAX) return -1;
    *value = (int)mapped;
    return 0;
}

static int more_rbsp_data(const bit_reader_t *br)
{
    size_t bit;
    if (!br || br->size > SIZE_MAX / 8u || br->bit >= br->size * 8u) return 0;
    /* rbsp_trailing_bits is a single one bit followed only by zero bits. */
    if (((br->data[br->bit >> 3] >> (7u - (br->bit & 7u))) & 1u) == 0)
        return 1;
    for (bit = br->bit + 1; bit < br->size * 8u; ++bit) {
        if ((br->data[bit >> 3] >> (7u - (bit & 7u))) & 1u)
            return 1;
    }
    return 0;
}

static int skip_scaling_list(bit_reader_t *br, unsigned count)
{
    unsigned i;
    int last = 8, next = 8, delta;
    for (i = 0; i < count; ++i) {
        if (next) {
            if (read_se(br, &delta) < 0) return -1;
            next = (last + delta + 256) & 255;
        }
        last = next ? next : last;
    }
    return 0;
}


static int make_rbsp(const uint8_t *nal, size_t size, uint8_t *rbsp,
                     size_t capacity, size_t *out_size)
{
    size_t i, out = 0;
    if (!nal || !rbsp || !out_size || size < 2 || (nal[0] & 0x80)) return -1;
    for (i = 1; i < size; ++i) {
        if (i + 2 < size && nal[i] == 0 && nal[i + 1] == 0 && nal[i + 2] == 3) {
            if (out + 2 > capacity) return -1;
            rbsp[out++] = 0; rbsp[out++] = 0; i += 2;
            continue;
        }
        if (out >= capacity) return -1;
        rbsp[out++] = nal[i];
    }
    *out_size = out;
    return 0;
}

int crtube_h264_nal_to_rbsp(const uint8_t *nal, size_t nal_size,
                            uint8_t *rbsp, size_t capacity, size_t *rbsp_size)
{
    return make_rbsp(nal, nal_size, rbsp, capacity, rbsp_size);
}


int crtube_h264_parse_sps(const uint8_t *nal, size_t size,
                          crtube_h264_stream_info_t *info)
{
    uint8_t rbsp[512];
    size_t out = 0;
    unsigned profile, constraints, level, id, chroma = 1, separate = 0;
    unsigned bit_luma = 0, bit_chroma = 0, frame_num, poc, refs, gaps;
    unsigned width_mbs, height_map, frame_only, direct, crop, left = 0, right = 0, top = 0, bottom = 0;
    unsigned ignored, count;
    bit_reader_t br;
    if (!nal || !info || size < 4 || (nal[0] & 0x1f) != H264_NAL_SPS ||
        make_rbsp(nal, size, rbsp, sizeof(rbsp), &out) < 0) return -1;
    memset(info, 0, sizeof(*info));
    br.data = rbsp; br.size = out; br.bit = 0;
    if (read_bits(&br, 8, &profile) < 0 || read_bits(&br, 8, &constraints) < 0 ||
        read_bits(&br, 8, &level) < 0 || read_ue(&br, &id) < 0) return -1;
    if (id > 31 || (constraints & 3u) != 0) return -1;
    info->profile_idc = profile; info->level_idc = level; info->sps_id = id;
    if (profile == 100 || profile == 110 || profile == 122 || profile == 244 ||
        profile == 44 || profile == 83 || profile == 86 || profile == 118 ||
        profile == 128 || profile == 138 || profile == 139 || profile == 134 || profile == 135) {
        if (read_ue(&br, &chroma) < 0 || chroma > 3) return -1;
        if (chroma == 3 && read_bit(&br, &separate) < 0) return -1;
        if (read_ue(&br, &bit_luma) < 0 || read_ue(&br, &bit_chroma) < 0 ||
            read_bit(&br, &ignored) < 0 || read_bit(&br, &ignored) < 0) return -1;
        if (ignored) {
            unsigned j;
            for (j = 0; j < (chroma == 3 ? 12u : 8u); ++j) {
                unsigned present;
                if (read_bit(&br, &present) < 0) return -1;
                if (present && skip_scaling_list(&br, j < 6 ? 16 : 64) < 0) return -1;
            }
        }
    }
    if (read_ue(&br, &frame_num) < 0 || frame_num > 12 || read_ue(&br, &poc) < 0 || poc > 2) return -1;
    info->log2_max_frame_num = frame_num + 4;
    info->pic_order_cnt_type = poc;
    if (poc == 0) {
        if (read_ue(&br, &ignored) < 0 || ignored > 12) return -1;
        info->log2_max_pic_order_cnt_lsb = ignored + 4;
    }
    else if (poc == 1) {
        int signed_value;
        if (read_bit(&br, &ignored) < 0 ||
            read_se(&br, &signed_value) < 0) return -1;
        info->delta_pic_order_always_zero = ignored;
        info->offset_for_non_ref_pic = signed_value;
        if (read_se(&br, &signed_value) < 0 ||
            read_ue(&br, &count) < 0 || count > 255) return -1;
        info->offset_for_top_to_bottom_field = signed_value;
        info->num_ref_frames_in_pic_order_cnt_cycle = count;
        {
            unsigned j;
            for (j = 0; j < count; ++j) {
                if (read_se(&br, &signed_value) < 0) return -1;
                info->offset_for_ref_frame[j] = signed_value;
            }
        }
    }
    if (read_ue(&br, &refs) < 0 || refs > 16 || read_bit(&br, &gaps) < 0 ||
        read_ue(&br, &width_mbs) < 0 || read_ue(&br, &height_map) < 0 ||
        read_bit(&br, &frame_only) < 0) return -1;
    if (!frame_only && read_bit(&br, &direct) < 0) return -1;
    if (read_bit(&br, &direct) < 0) return -1;
    if (read_bit(&br, &crop) < 0) return -1;
    if (crop) {
        if (read_ue(&br, &left) < 0) return -1;
        if (read_ue(&br, &right) < 0) return -1;
        if (read_ue(&br, &top) < 0) return -1;
        if (read_ue(&br, &bottom) < 0) return -1;
    }
    {
        unsigned coded_w = (width_mbs + 1) * 16;
        unsigned coded_h = (height_map + 1) * 16 * (2 - frame_only);
        unsigned crop_x = (chroma == 0 || separate) ? 1 : 2;
        unsigned crop_y = (chroma == 0 || separate) ? (2 - frame_only) : 2 * (2 - frame_only);
        unsigned crop_w = (left + right) * crop_x;
        unsigned crop_h = (top + bottom) * crop_y;
        if (crop_w >= coded_w || crop_h >= coded_h) return -1;
        info->coded_width = coded_w; info->coded_height = coded_h;
        info->width = coded_w - crop_w; info->height = coded_h - crop_h;
    }
    info->chroma_format_idc = chroma;
    info->bit_depth_luma = bit_luma + 8; info->bit_depth_chroma = bit_chroma + 8;
    info->frame_mbs_only = frame_only; info->num_ref_frames = refs;
    info->gaps_in_frame_num_value_allowed = gaps;
    if (read_bit(&br, &info->vui_parameters_present) < 0) return -1;
    (void)constraints; (void)frame_num; (void)direct;
    return 0;
}

static int make_rbsp_prefix(const uint8_t *nal, size_t size, uint8_t *rbsp,
                            size_t capacity, size_t *out_size)
{
    size_t i, out = 0;
    if (!nal || !rbsp || !out_size || size < 2 || capacity == 0 || (nal[0] & 0x80)) return -1;
    for (i = 1; i < size && out < capacity; ++i) {
        if (i + 2 < size && nal[i] == 0 && nal[i + 1] == 0 && nal[i + 2] == 3) {
            if (out + 2 > capacity) break;
            rbsp[out++] = 0; rbsp[out++] = 0; i += 2;
        } else {
            rbsp[out++] = nal[i];
        }
    }
    *out_size = out;
    return out ? 0 : -1;
}

int crtube_h264_parse_pps(const uint8_t *nal, size_t size,
                          crtube_h264_pps_info_t *pps)
{
    uint8_t rbsp[512];
    size_t rbsp_size;
    bit_reader_t br;
    unsigned value, groups;
    if (!nal || !pps || size < 2 || (nal[0] & 0x1f) != H264_NAL_PPS ||
        make_rbsp(nal, size, rbsp, sizeof(rbsp), &rbsp_size) < 0) return -1;
    memset(pps, 0, sizeof(*pps));
    br.data = rbsp; br.size = rbsp_size; br.bit = 0;
    if (read_ue(&br, &pps->pps_id) < 0 || pps->pps_id > 255 ||
        read_ue(&br, &pps->sps_id) < 0 || pps->sps_id > 31 ||
        read_bit(&br, &pps->entropy_coding_mode) < 0 ||
        read_bit(&br, &pps->bottom_field_pic_order_in_frame_present) < 0 ||
        read_ue(&br, &groups) < 0 || groups > 7) return -1;
    pps->num_slice_groups_minus1 = groups;
    if (groups != 0) return -2; /* FMO is outside the first implementation. */
    if (read_ue(&br, &value) < 0 || value > 31) return -1;
    pps->num_ref_idx_l0_default = value + 1;
    if (read_ue(&br, &value) < 0 || value > 31) return -1;
    pps->num_ref_idx_l1_default = value + 1;
    if (read_bit(&br, &pps->weighted_pred) < 0 ||
        read_bits(&br, 2, &pps->weighted_bipred_idc) < 0 ||
        read_se(&br, &pps->pic_init_qp_minus26) < 0 ||
        read_se(&br, &pps->pic_init_qs_minus26) < 0 ||
        read_se(&br, &pps->chroma_qp_index_offset) < 0 ||
        read_bit(&br, &pps->deblocking_filter_control_present) < 0 ||
        read_bit(&br, &pps->constrained_intra_pred) < 0 ||
        read_bit(&br, &pps->redundant_pic_cnt_present) < 0) return -1;
    if (pps->pic_init_qp_minus26 < -26 || pps->pic_init_qp_minus26 > 25 ||
        pps->chroma_qp_index_offset < -12 || pps->chroma_qp_index_offset > 12) return -1;
    pps->second_chroma_qp_index_offset = pps->chroma_qp_index_offset;
    if (more_rbsp_data(&br)) {
        unsigned scaling_present;
        if (read_bit(&br, &pps->transform_8x8_mode) < 0 ||
            read_bit(&br, &scaling_present) < 0) return -1;
        pps->pic_scaling_matrix_present = scaling_present;
        /* Scaling matrices require high-profile SPS/PPS semantics. The first
           PS2 target is 8-bit 4:2:0 Baseline with flat scaling lists. */
        if (scaling_present) return -2;
        if (read_se(&br, &pps->second_chroma_qp_index_offset) < 0 ||
            pps->second_chroma_qp_index_offset < -12 ||
            pps->second_chroma_qp_index_offset > 12) return -1;
    }
    return 0;
}

static int skip_ref_pic_list_reordering(bit_reader_t *br, unsigned *present)
{
    unsigned flag, idc, value, count = 0;
    if (read_bit(br, &flag) < 0) return -1;
    *present = flag;
    if (!flag) return 0;
    for (;;) {
        if (read_ue(br, &idc) < 0 || idc > 3) return -1;
        if (idc == 3) break;
        if (++count > 64) return -1;
        if (idc == 0 || idc == 1) {
            if (read_ue(br, &value) < 0) return -1;
        } else if (read_ue(br, &value) < 0) return -1;
    }
    return 0;
}


static int parse_pred_weight_table(bit_reader_t *br,
                                    const crtube_h264_stream_info_t *sps,
                                    unsigned num_ref_l0,
                                    crtube_h264_slice_info_t *slice)
{
    unsigned luma_denom, chroma_denom = 0, i, flag;
    if (!br || !sps || !slice || !num_ref_l0 || num_ref_l0 > 32u ||
        read_ue(br, &luma_denom) < 0 || luma_denom > 7u)
        return -1;
    if (sps->chroma_format_idc != 0u &&
        (read_ue(br, &chroma_denom) < 0 || chroma_denom > 7u))
        return -1;
    slice->pred_weight_l0_count = num_ref_l0;
    for (i = 0; i < num_ref_l0; ++i) {
        crtube_h264_pred_weight_t *w = &slice->pred_weight_l0[i];
        int value;
        memset(w, 0, sizeof(*w));
        w->enabled = 1u;
        w->luma_denom = luma_denom;
        w->chroma_denom = chroma_denom;
        w->luma_weight = 1 << luma_denom;
        w->chroma_weight_cb = 1 << chroma_denom;
        w->chroma_weight_cr = 1 << chroma_denom;
        if (read_bit(br, &flag) < 0) return -1;
        if (flag) {
            if (read_se(br, &value) < 0 || value < -128 || value > 127)
                return -1;
            w->luma_weight = value;
            if (read_se(br, &value) < 0 || value < -128 || value > 127)
                return -1;
            w->luma_offset = value;
        }
        if (sps->chroma_format_idc != 0u) {
            if (read_bit(br, &flag) < 0) return -1;
            if (flag) {
                if (read_se(br, &value) < 0 || value < -128 || value > 127)
                    return -1;
                w->chroma_weight_cb = value;
                if (read_se(br, &value) < 0 || value < -128 || value > 127)
                    return -1;
                w->chroma_offset_cb = value;
                if (read_se(br, &value) < 0 || value < -128 || value > 127)
                    return -1;
                w->chroma_weight_cr = value;
                if (read_se(br, &value) < 0 || value < -128 || value > 127)
                    return -1;
                w->chroma_offset_cr = value;
            }
        }
    }
    return 0;
}

int crtube_h264_parse_slice_header(const uint8_t *nal, size_t size,
                                   const crtube_h264_stream_info_t *sps,
                                   const crtube_h264_pps_info_t *pps,
                                   crtube_h264_slice_info_t *slice)
{
    uint8_t rbsp[4096];
    size_t rbsp_size;
    bit_reader_t br;
    unsigned value, flag, is_p, is_b;
    int signed_value;
    if (!nal || !sps || !pps || !slice || size < 2 ||
        (nal[0] & 0x80) || ((nal[0] & 0x1f) != H264_NAL_SLICE &&
        (nal[0] & 0x1f) != H264_NAL_IDR) ||
        make_rbsp_prefix(nal, size, rbsp, sizeof(rbsp), &rbsp_size) < 0) return -1;
    memset(slice, 0, sizeof(*slice));
    slice->nal_type = nal[0] & 0x1f;
    slice->nal_ref_idc = (nal[0] >> 5) & 3;
    br.data = rbsp; br.size = rbsp_size; br.bit = 0;
    if (read_ue(&br, &slice->first_mb_in_slice) < 0 ||
        read_ue(&br, &slice->slice_type) < 0 || slice->slice_type > 9 ||
        read_ue(&br, &slice->pps_id) < 0) return -1;
    slice->slice_type %= 5;
    if (slice->pps_id != pps->pps_id || pps->sps_id != sps->sps_id) return -2;
    if (pps->entropy_coding_mode) return -2;
    if (sps->log2_max_frame_num < 4 || sps->log2_max_frame_num > 16 ||
        read_bits(&br, sps->log2_max_frame_num, &slice->frame_num) < 0) return -1;
    if (!sps->frame_mbs_only) return -2;
    if (slice->nal_type == H264_NAL_IDR) {
        if (read_ue(&br, &slice->idr_pic_id) < 0 || slice->idr_pic_id > 65535) return -1;
    }
    if (sps->pic_order_cnt_type == 0) {
        if (sps->log2_max_pic_order_cnt_lsb < 4 ||
            sps->log2_max_pic_order_cnt_lsb > 16 ||
            read_bits(&br, sps->log2_max_pic_order_cnt_lsb,
                      &slice->pic_order_cnt_lsb) < 0) return -1;
        if (pps->bottom_field_pic_order_in_frame_present &&
            read_se(&br, &signed_value) < 0) return -1;
    } else if (sps->pic_order_cnt_type == 1) {
        if (!sps->delta_pic_order_always_zero) {
            if (read_se(&br, &signed_value) < 0) return -1;
            if (pps->bottom_field_pic_order_in_frame_present &&
                read_se(&br, &signed_value) < 0) return -1;
        }
    } else if (sps->pic_order_cnt_type != 2) return -1;

    if (pps->redundant_pic_cnt_present) {
        if (read_ue(&br, &value) < 0 || value > 127) return -1;
    }
    is_p = slice->slice_type == 0 || slice->slice_type == 3;
    is_b = slice->slice_type == 1;
    if (is_b) {
        if (read_bit(&br, &slice->direct_spatial_mv_pred) < 0) return -1;
        return -2;
    }
    if (slice->slice_type == 2 || slice->slice_type == 4) {
        /* I and SI slices have no inter reference syntax. SI is not supported. */
        if (slice->slice_type == 4) return -2;
        slice->num_ref_idx_l0_active = 0;
    } else if (is_p) {
        slice->num_ref_idx_l0_active = pps->num_ref_idx_l0_default;
        if (read_bit(&br, &flag) < 0) return -1;
        if (flag) {
            if (read_ue(&br, &value) < 0 || value > 31) return -1;
            slice->num_ref_idx_l0_active = value + 1;
        }
    } else return -2;

    if (is_p && skip_ref_pic_list_reordering(&br,
            &slice->ref_pic_list_reordering_l0) < 0) return -1;
    if (pps->weighted_pred && is_p &&
        parse_pred_weight_table(&br, sps, slice->num_ref_idx_l0_active,
                                slice) < 0)
        return -1;
    if (pps->weighted_bipred_idc == 1u && is_b) return -2;

    if (slice->nal_ref_idc) {
        if (slice->nal_type == H264_NAL_IDR) {
            if (read_bit(&br, &slice->no_output_of_prior_pics) < 0 ||
                read_bit(&br, &slice->long_term_reference) < 0) return -1;
        } else {
            if (read_bit(&br, &slice->adaptive_ref_pic_marking_mode) < 0) return -1;
            if (slice->adaptive_ref_pic_marking_mode) {
                unsigned op, count = 0;
                do {
                    if (read_ue(&br, &op) < 0 || op > 6) return -1;
                    if (op == 0) break;
                    if (++count > 64) return -1;
                    if ((op == 1 || op == 3) && read_ue(&br, &value) < 0) return -1;
                    if (op == 2 && read_ue(&br, &value) < 0) return -1;
                    if ((op == 3 || op == 6) && read_ue(&br, &value) < 0) return -1;
                    if (op == 4 && read_ue(&br, &value) < 0) return -1;
                } while (1);
            }
        }
    }
    if (read_se(&br, &slice->slice_qp_delta) < 0 ||
        slice->slice_qp_delta < -51 || slice->slice_qp_delta > 51) return -1;
    if (pps->deblocking_filter_control_present) {
        if (read_ue(&br, &slice->disable_deblocking_filter_idc) < 0 ||
            slice->disable_deblocking_filter_idc > 2) return -1;
        if (slice->disable_deblocking_filter_idc != 1) {
            if (read_se(&br, &slice->slice_alpha_c0_offset_div2) < 0 ||
                read_se(&br, &slice->slice_beta_offset_div2) < 0 ||
                slice->slice_alpha_c0_offset_div2 < -6 ||
                slice->slice_alpha_c0_offset_div2 > 6 ||
                slice->slice_beta_offset_div2 < -6 ||
                slice->slice_beta_offset_div2 > 6) return -1;
        }
    }
    slice->slice_data_bit_offset = br.bit;
    return 0;
}

int crtube_h264_probe_annexb(const uint8_t *data, size_t size,
                             crtube_h264_stream_info_t *info)
{
    size_t offset = 0;
    crtube_h264_nal_t nal;
    int result, have_sps = 0;
    crtube_h264_stream_info_t parsed;
    if (!data || !size || !info) return -1;
    memset(info, 0, sizeof(*info));
    while ((result = crtube_h264_annexb_next(data, size, &offset, &nal)) > 0) {
        if (nal.type == H264_NAL_SPS && !have_sps) {
            if (crtube_h264_parse_sps(nal.data, nal.size, &parsed) < 0) return -1;
            *info = parsed; have_sps = 1;
        } else if (nal.type == H264_NAL_PPS) ++info->pps_count;
        else if (nal.type == H264_NAL_IDR) ++info->idr_count;
        else if (nal.type == H264_NAL_SLICE) ++info->slice_count;
    }
    if (result < 0 || !have_sps) return -1;
    if (info->profile_idc != 66 || info->chroma_format_idc != 1 ||
        info->bit_depth_luma != 8 || info->bit_depth_chroma != 8 ||
        !info->frame_mbs_only || info->coded_width > CRTUBE_H264_MAX_WIDTH ||
        info->coded_height > CRTUBE_H264_MAX_HEIGHT) return -2;
    return 0;
}
