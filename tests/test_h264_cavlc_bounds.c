#include "h264_decoder.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    const uint8_t zero_token[] = {0x80};
    const uint8_t truncated[] = {0x00};
    int16_t coeffs[16];
    unsigned total = 99u;
    size_t bit = 0u;

    assert(crtube_h264_cavlc_validate_tables() == 0);
    memset(coeffs, 0x5a, sizeof(coeffs));
    assert(crtube_h264_cavlc_decode_block(zero_token, sizeof(zero_token),
        &bit, 0, 16u, coeffs, &total) == 0);
    assert(bit == 1u && total == 0u);

    bit = 0u;
    total = 99u;
    assert(crtube_h264_cavlc_decode_block(truncated, sizeof(truncated),
        &bit, 0, 16u, coeffs, &total) < 0);
    /* Failed parses must not commit the caller's bit cursor. */
    assert(bit == 0u);

    puts("h264 CAVLC bounds tests: ok");
    return 0;
}
