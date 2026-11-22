#include "h264_decoder.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    uint8_t storage[32u * 16u * 3u / 2u];
    crtube_h264_frame_t reference;
    crtube_h264_p_partition_t parts[16];
    crtube_h264_pred_weight_t weights;
    uint8_t y[256], cb[64], cr[64], sub[4];
    unsigned count, x, row;
    crtube_h264_p_mb_partition_header_t parsed;
    size_t bit_offset;
    const uint8_t p16x8_bits[] = {0x5f};
    const uint8_t p8x16_bits[] = {0x7f};
    const uint8_t p8x8_bits[] = {0x27, 0xff, 0xc0};
    const uint8_t p8x8ref0_bits[] = {0x2f, 0xff, 0xc0};
    const uint8_t p16x8_refs_bits[] = {0x5d, 0x70};
    const uint8_t p8x8_refs_bits[] = {0x27, 0xd5, 0x7f, 0xc0};
    int mvp_x[16] = {0}, mvp_y[16] = {0};

    assert(crtube_h264_frame_init(&reference, storage, sizeof(storage),
                                  32u, 16u) == 0);
    for (row = 0; row < 16u; ++row)
        for (x = 0; x < 32u; ++x)
            reference.y[row * 32u + x] = (uint8_t)(x * 3u);
    memset(reference.cb, 80, 64u);
    memset(reference.cr, 160, 64u);

    assert(crtube_h264_p_partition_layout(1u, NULL, parts, &count) == 0);
    assert(count == 2u && parts[0].height4 == 2u && parts[1].y4 == 2u);
    parts[0].mv_x_qpel = 0; parts[0].mv_y_qpel = 0;
    parts[1].mv_x_qpel = 4; parts[1].mv_y_qpel = 0;
    memset(&weights, 0, sizeof(weights));
    weights.enabled = 1u;
    weights.luma_denom = weights.chroma_denom = 1u;
    weights.luma_weight = weights.chroma_weight_cb =
        weights.chroma_weight_cr = 2;
    assert(crtube_h264_motion_compensate_partitions(&reference, 0u, 0u,
        parts, count, &weights, y, cb, cr) == 0);
    assert(y[0] == reference.y[0]);
    assert(y[8u * 16u] == reference.y[8u * 32u + 1u]);
    assert(cb[0] == 80u && cr[0] == 160u);
    weights.luma_offset = 3;
    weights.chroma_offset_cb = -2;
    weights.chroma_offset_cr = 4;
    assert(crtube_h264_motion_compensate_partitions(&reference, 0u, 0u,
        parts, count, &weights, y, cb, cr) == 0);
    assert(y[0] == (uint8_t)(reference.y[0] + 3u));
    assert(cb[0] == 78u && cr[0] == 164u);

    for (x = 0; x < 4u; ++x) sub[x] = 0u;
    assert(crtube_h264_p_partition_layout(3u, sub, parts, &count) == 0);
    assert(count == 4u);
    for (x = 0; x < 4u; ++x) sub[x] = 1u;
    assert(crtube_h264_p_partition_layout(4u, sub, parts, &count) == 0);
    assert(count == 8u);
    for (x = 0; x < 4u; ++x) sub[x] = 2u;
    assert(crtube_h264_p_partition_layout(3u, sub, parts, &count) == 0);
    assert(count == 8u);
    for (x = 0; x < 4u; ++x) sub[x] = 3u;
    assert(crtube_h264_p_partition_layout(4u, sub, parts, &count) == 0);
    assert(count == 16u);

    bit_offset = 0;
    assert(crtube_h264_parse_p_mb_partitions(p16x8_bits,
        sizeof(p16x8_bits), &bit_offset, 0u, 1u, mvp_x, mvp_y,
        &parsed) == 0);
    assert(parsed.mb_type == 1u && parsed.partition_count == 2u);
    bit_offset = 0;
    assert(crtube_h264_parse_p_mb_partitions(p8x16_bits,
        sizeof(p8x16_bits), &bit_offset, 0u, 1u, mvp_x, mvp_y,
        &parsed) == 0);
    assert(parsed.mb_type == 2u && parsed.partition_count == 2u);
    bit_offset = 0;
    assert(crtube_h264_parse_p_mb_partitions(p8x8_bits,
        sizeof(p8x8_bits), &bit_offset, 0u, 1u, mvp_x, mvp_y,
        &parsed) == 0);
    assert(parsed.mb_type == 3u && parsed.partition_count == 4u);
    bit_offset = 0;
    assert(crtube_h264_parse_p_mb_partitions(p8x8ref0_bits,
        sizeof(p8x8ref0_bits), &bit_offset, 0u, 1u, mvp_x, mvp_y,
        &parsed) == 0);
    assert(parsed.mb_type == 4u && parsed.partition_count == 4u);
    bit_offset = 0;
    assert(crtube_h264_parse_p_mb_partitions(p16x8_refs_bits,
        sizeof(p16x8_refs_bits), &bit_offset, 0u, 2u, mvp_x, mvp_y,
        &parsed) == 0);
    assert(parsed.partition_count == 2u &&
           parsed.partitions[0].ref_idx_l0 == 0u &&
           parsed.partitions[1].ref_idx_l0 == 1u);
    bit_offset = 0;
    assert(crtube_h264_parse_p_mb_partitions(p8x8_refs_bits,
        sizeof(p8x8_refs_bits), &bit_offset, 0u, 2u, mvp_x, mvp_y,
        &parsed) == 0);
    assert(parsed.partition_count == 4u &&
           parsed.partitions[0].ref_idx_l0 == 0u &&
           parsed.partitions[1].ref_idx_l0 == 1u &&
           parsed.partitions[2].ref_idx_l0 == 0u &&
           parsed.partitions[3].ref_idx_l0 == 1u);

    puts("h264 partition/weighted prediction tests: ok");
    return 0;
}
