#include "h264_playback.h"

#include <string.h>

static int load_parameter_sets(const crtube_mp4_t *mp4,
                               crtube_h264_stream_info_t *sps,
                               crtube_h264_pps_info_t *pps)
{
    const uint8_t *nal;
    size_t nal_size;

    if (crtube_mp4_get_sps(mp4, 0u, &nal, &nal_size) < 0 ||
        crtube_h264_parse_sps(nal, nal_size, sps) < 0)
        return -1;
    if (crtube_mp4_get_pps(mp4, 0u, &nal, &nal_size) < 0 ||
        crtube_h264_parse_pps(nal, nal_size, pps) < 0)
        return -1;
    if (pps->sps_id != sps->sps_id || pps->entropy_coding_mode ||
        pps->num_slice_groups_minus1 != 0u ||
        sps->chroma_format_idc != 1u || sps->bit_depth_luma != 8u ||
        sps->bit_depth_chroma != 8u || !sps->frame_mbs_only ||
        !sps->width || !sps->height || (sps->width & 15u) ||
        (sps->height & 15u))
        return -2;
    return 0;
}

int crtube_h264_decode_mp4_sample(
    const crtube_mp4_t *mp4, unsigned sample_index,
    crtube_h264_frame_t *current, crtube_h264_dpb_t *dpb,
    crtube_h264_playback_workspace_t *workspace,
    crtube_h264_playback_info_t *info)
{
    crtube_h264_stream_info_t sps;
    crtube_h264_pps_info_t pps;
    crtube_mp4_sample_t sample;
    crtube_h264_nal_t nal;
    crtube_h264_slice_info_t slice;
    size_t annexb_size = 0, offset = 0, rbsp_size = 0;
    unsigned mb_count, qp_u, decoded_mbs = 0u;
    uint8_t *covered;
    unsigned picture_started = 0u, picture_nal_type = 0u;
    unsigned picture_slice_type = 0u, picture_frame_num = 0u;
    unsigned picture_poc = 0u;
    int qp, result, decoded = 0;

    if (!mp4 || !current || !dpb || !workspace || !workspace->annexb ||
        !workspace->rbsp || !workspace->macroblocks || !info ||
        !workspace->annexb_capacity || !workspace->rbsp_capacity ||
        !workspace->macroblock_capacity || !current->storage)
        return -1;

    result = load_parameter_sets(mp4, &sps, &pps);
    if (result < 0) return result;
    if (current->width != sps.width || current->height != sps.height ||
        dpb->reference.width != sps.width || dpb->reference.height != sps.height)
        return -2;
    mb_count = (sps.width / 16u) * (sps.height / 16u);
    if (workspace->macroblock_capacity < mb_count)
        return -2;
    /* Reuse the caller-owned macroblock scratch as a compact coverage map.
       This avoids an 8 KiB picture-sized array on the PS2 EE stack. */
    covered = (uint8_t *)(void *)workspace->macroblocks;
    memset(covered, 0, mb_count);
    if (crtube_mp4_get_sample(mp4, sample_index, &sample) < 0 ||
        crtube_mp4_sample_to_annexb(mp4, &sample, workspace->annexb,
                                    workspace->annexb_capacity,
                                    &annexb_size) < 0)
        return -1;

    memset(info, 0, sizeof(*info));
    info->width = sps.width;
    info->height = sps.height;
    info->sample_index = sample_index;

    while ((result = crtube_h264_annexb_next(workspace->annexb, annexb_size,
                                              &offset, &nal)) > 0) {
        if (nal.type != 1u && nal.type != 5u)
            continue;
        if (crtube_h264_parse_slice_header(nal.data, nal.size, &sps, &pps,
                                           &slice) < 0)
            return -1;
        if (slice.first_mb_in_slice >= mb_count ||
            crtube_h264_nal_to_rbsp(nal.data, nal.size, workspace->rbsp,
                                    workspace->rbsp_capacity, &rbsp_size) < 0)
            return -1;
        qp = 26 + pps.pic_init_qp_minus26 + slice.slice_qp_delta;
        if (qp < 0 || qp > 51) return -2;

        /* All VCL NALs in this MP4 sample must describe the same picture.
           Each slice decoder starts with unavailable cross-slice neighbors. */
        if (!picture_started) {
            picture_started = 1u;
            picture_nal_type = nal.type;
            picture_slice_type = slice.slice_type;
            picture_frame_num = slice.frame_num;
            picture_poc = slice.pic_order_cnt_lsb;
            if (nal.type == 5u) {
                if (slice.slice_type != 2u) return -2;
                crtube_h264_dpb_reset(dpb);
                crtube_h264_frame_clear(current, 0u, 128u, 128u);
            } else if (slice.slice_type != 0u || !dpb->valid ||
                       slice.num_ref_idx_l0_active != 1u) {
                return -2;
            }
            info->first_mb_in_slice = slice.first_mb_in_slice;
        } else if (picture_nal_type != nal.type ||
                   picture_slice_type != slice.slice_type ||
                   picture_frame_num != slice.frame_num ||
                   picture_poc != slice.pic_order_cnt_lsb) {
            return -2;
        }

        if (nal.type == 5u) {
            result = crtube_h264_decode_i_slice_to_frame(
                workspace->rbsp, rbsp_size, slice.slice_data_bit_offset,
                current, slice.first_mb_in_slice, (unsigned)qp,
                pps.transform_8x8_mode, pps.chroma_qp_index_offset);
        } else {
            result = crtube_h264_decode_p_slice_data(
                workspace->rbsp, rbsp_size, slice.slice_data_bit_offset,
                current, dpb, slice.first_mb_in_slice,
                slice.num_ref_idx_l0_active, &qp_u,
                pps.chroma_qp_index_offset,
                pps.second_chroma_qp_index_offset);
            qp = (int)qp_u;
        }
        if (result <= 0) return result < 0 ? result : -1;
        if ((unsigned)result > mb_count - slice.first_mb_in_slice)
            return -1;
        {
            unsigned i;
            for (i = 0; i < (unsigned)result; ++i) {
                unsigned address = slice.first_mb_in_slice + i;
                if (covered[address]) return -1;
                covered[address] = 1u;
            }
        }
        decoded_mbs += (unsigned)result;
        info->frame_num = slice.frame_num;
        info->slice_type = slice.slice_type;
        info->nal_type = slice.nal_type;
        info->macroblocks_decoded = decoded_mbs;
        info->poc = (int)slice.pic_order_cnt_lsb;
        decoded = 1;
    }
    if (result < 0) return -1;
    if (!decoded) return 0;
    if (decoded_mbs != mb_count) return -2;
    {
        unsigned i;
        for (i = 0; i < mb_count; ++i)
            if (!covered[i]) return -2;
    }
    if (crtube_h264_dpb_store(dpb, current, picture_frame_num,
                              (int)picture_poc) < 0)
        return -1;
    return 1;
}
