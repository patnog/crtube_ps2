#include "h264_decoder.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    /* mb_type=3 (Intra16 DC, no CBP), chroma mode 0, qp_delta 0,
       zero luma-DC coeff_token, then rbsp_trailing_bits. */
    const uint8_t rbsp[] = {0x27, 0x80};
    uint8_t storage[16u * 16u * 3u / 2u];
    crtube_h264_frame_t frame;
    int decoded;

    assert(crtube_h264_frame_init(&frame, storage, sizeof(storage), 16u, 16u) == 0);
    crtube_h264_frame_clear(&frame, 0u, 128u, 128u);
    decoded = crtube_h264_decode_i16x16_slice_to_frame(
        rbsp, sizeof(rbsp), 0u, &frame, 0u, 26u, 0);
    assert(decoded == 1);
    for (unsigned i = 0; i < 256u; ++i) assert(frame.y[i] == 128u);
    for (unsigned i = 0; i < 64u; ++i) {
        assert(frame.cb[i] == 128u);
        assert(frame.cr[i] == 128u);
    }
    puts("h264 I16x16 tests: ok");
    return 0;
}
