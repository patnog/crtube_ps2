#include "h264_decoder.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    uint8_t reference_storage[32u * 16u * 3u / 2u];
    uint8_t current_storage[32u * 16u * 3u / 2u];
    uint8_t dpb_storage[32u * 16u * 3u / 2u];
    crtube_h264_frame_t reference_frame, current_frame;
    crtube_h264_dpb_t dpb;
    unsigned qp = 26u;
    /* ue(1) => mb_skip_run=1; then rbsp_stop_one_bit and alignment. */
    const uint8_t p_slice_rbsp[] = {0x50};
    int decoded;

    assert(crtube_h264_frame_init(&reference_frame, reference_storage,
                                  sizeof(reference_storage), 32u, 16u) == 0);
    assert(crtube_h264_frame_init(&current_frame, current_storage,
                                  sizeof(current_storage), 32u, 16u) == 0);
    memset(reference_storage, 0x80, sizeof(reference_storage));
    memset(current_storage, 0, sizeof(current_storage));
    assert(crtube_h264_dpb_init(&dpb, dpb_storage, sizeof(dpb_storage),
                                32u, 16u) == 0);
    assert(crtube_h264_dpb_store(&dpb, &reference_frame, 0u, 0) == 0);

    /* A slice beginning at address 1 is legal and must decode one P_Skip. */
    decoded = crtube_h264_decode_p_slice_data(
        p_slice_rbsp, sizeof(p_slice_rbsp), 0u, &current_frame, &dpb,
        1u, 1u, &qp, 0, 0);
    assert(decoded == 1);
    assert(qp == 26u);
    assert(memcmp(current_frame.y + 16u, dpb.reference.y + 16u, 16u) == 0);

    puts("h264 P-slice tests: ok");
    return 0;
}
