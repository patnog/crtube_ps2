#include "h264_decoder.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    const uint8_t nal[] = {0x65, 0x00, 0x00, 0x03, 0x01,
                           0x00, 0x00, 0x03, 0x02, 0x80};
    const uint8_t expected[] = {0x00, 0x00, 0x01, 0x00,
                                0x00, 0x02, 0x80};
    uint8_t rbsp[sizeof(nal)];
    size_t rbsp_size = 0;

    assert(crtube_h264_nal_to_rbsp(nal, sizeof(nal), rbsp,
                                   sizeof(rbsp), &rbsp_size) == 0);
    assert(rbsp_size == sizeof(expected));
    assert(memcmp(rbsp, expected, sizeof(expected)) == 0);
    assert(crtube_h264_nal_to_rbsp(nal, sizeof(nal), rbsp, 2u,
                                   &rbsp_size) < 0);
    puts("h264 RBSP tests: ok");
    return 0;
}
