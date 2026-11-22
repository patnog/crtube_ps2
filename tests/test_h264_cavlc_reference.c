#include "h264_decoder.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    /* H.264 CAVLC coeff_token (nC=0), trailing-one sign, total_zeros=0.
       01 | 0 | 1 -> one positive coefficient at scan position zero. */
    const uint8_t positive[] = {0x50};
    /* 01 | 1 | 1 -> same reference codeword with negative sign. */
    const uint8_t negative[] = {0x70};
    int16_t coeff[16];
    unsigned total;
    size_t bit;

    assert(crtube_h264_cavlc_validate_tables() == 0);
    bit = 0;
    assert(crtube_h264_cavlc_decode_block(positive, sizeof(positive),
        &bit, 0, 16u, coeff, &total) == 0);
    assert(total == 1u && bit == 4u && coeff[0] == 1);
    bit = 0;
    assert(crtube_h264_cavlc_decode_block(negative, sizeof(negative),
        &bit, 0, 16u, coeff, &total) == 0);
    assert(total == 1u && bit == 4u && coeff[0] == -1);

    puts("h264 CAVLC reference vectors: ok");
    return 0;
}
