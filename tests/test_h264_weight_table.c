#include "h264_decoder.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    /* P slice with weighted_pred_flag=1. The table contains L0[0]:
       luma weight=2, offset=-1; Cb weight=2, offset=1;
       Cr weight=1, offset=0. */
    const uint8_t nal[] = {0x61, 0xe0, 0x07, 0x23, 0x91, 0x2b};
    crtube_h264_stream_info_t sps = {0};
    crtube_h264_pps_info_t pps = {0};
    crtube_h264_slice_info_t slice;
    sps.chroma_format_idc = 1u;
    sps.log2_max_frame_num = 4u;
    sps.pic_order_cnt_type = 0u;
    sps.log2_max_pic_order_cnt_lsb = 4u;
    sps.frame_mbs_only = 1u;
    sps.sps_id = 0u;
    pps.pps_id = 0u;
    pps.sps_id = 0u;
    pps.num_ref_idx_l0_default = 1u;
    pps.weighted_pred = 1u;
    assert(crtube_h264_parse_slice_header(nal, sizeof(nal), &sps,
                                           &pps, &slice) == 0);
    assert(slice.pred_weight_l0_count == 1u);
    assert(slice.pred_weight_l0[0].enabled == 1u);
    assert(slice.pred_weight_l0[0].luma_denom == 0u);
    assert(slice.pred_weight_l0[0].luma_weight == 2);
    assert(slice.pred_weight_l0[0].luma_offset == -1);
    assert(slice.pred_weight_l0[0].chroma_weight_cb == 2);
    assert(slice.pred_weight_l0[0].chroma_offset_cb == 1);
    assert(slice.pred_weight_l0[0].chroma_weight_cr == 1);
    assert(slice.pred_weight_l0[0].chroma_offset_cr == 0);
    puts("h264 weighted prediction table: ok");
    return 0;
}
