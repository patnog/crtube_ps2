#include "h264_decoder.h"

int crtube_h264_decode_i4x4_slice_to_frame(
    const uint8_t *rbsp, size_t rbsp_size, size_t bit_offset,
    crtube_h264_frame_t *frame, unsigned first_mb_in_slice,
    unsigned current_qp, unsigned transform_8x8_mode,
    int chroma_qp_index_offset,
    crtube_h264_i4x4_mb_data_t *macroblocks, unsigned max_macroblocks)
{
    unsigned mb_width, mb_height, i;
    int count;

    if (!rbsp || !rbsp_size || !frame || !frame->storage ||
        !frame->width || !frame->height ||
        (frame->width & 15u) || (frame->height & 15u) ||
        !macroblocks || !max_macroblocks || current_qp > 51u ||
        chroma_qp_index_offset < -12 || chroma_qp_index_offset > 12)
        return -1;

    mb_width = frame->width / 16u;
    mb_height = frame->height / 16u;
    if (mb_width > 30u || mb_height > 17u ||
        (size_t)mb_width * mb_height > max_macroblocks)
        return -2;

    count = crtube_h264_parse_i4x4_slice_data(
        rbsp, rbsp_size, bit_offset, first_mb_in_slice,
        mb_width, mb_height, current_qp, transform_8x8_mode,
        macroblocks, max_macroblocks);
    if (count < 0) return count;

    for (i = 0; i < (unsigned)count; ++i) {
        if (crtube_h264_reconstruct_i4x4_mb_to_frame(
                frame, &macroblocks[i], chroma_qp_index_offset) < 0)
            return -2;
    }
    return count;
}
