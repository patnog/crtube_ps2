#define SIMD 6
#include "../third_party/edge264/src/edge264_internal.h"
#include <stdio.h>

int main(void)
{
    i8x16 mask = {1, 2, 4, 8, 16, 32, 64, 127,
                  -1, -2, -64, 0, 0x55, (int8_t)0xaa, 0x7f, (int8_t)0x80};
    i8x16 yes = {0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,
                 0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55};
    i8x16 no  = {(int8_t)0xaa,(int8_t)0xaa,(int8_t)0xaa,(int8_t)0xaa,
                 (int8_t)0xaa,(int8_t)0xaa,(int8_t)0xaa,(int8_t)0xaa,
                 (int8_t)0xaa,(int8_t)0xaa,(int8_t)0xaa,(int8_t)0xaa,
                 (int8_t)0xaa,(int8_t)0xaa,(int8_t)0xaa,(int8_t)0xaa};
    i8x16 got = ifelse_mask(mask, yes, no);
    i8x16 msb = ifelse_msb(mask, yes, no);
    for (int i = 0; i < 16; ++i) {
        int8_t expected = (int8_t)(((uint8_t)yes[i] & (uint8_t)mask[i]) |
                                  ((uint8_t)no[i] & (uint8_t)~mask[i]));
        int8_t expected_msb = mask[i] < 0 ? yes[i] : no[i];
        if (got[i] != expected || msb[i] != expected_msb) {
            fprintf(stderr, "lane %d mask=%02x got=%02x expected=%02x msb=%02x expected_msb=%02x\n",
                    i, (uint8_t)mask[i], (uint8_t)got[i], (uint8_t)expected,
                    (uint8_t)msb[i], (uint8_t)expected_msb);
            return 1;
        }
    }
    puts("R5900 vector mask semantics: PASS");
    return 0;
}
