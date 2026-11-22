#ifndef EDGE264_R5900_MMI_H
#define EDGE264_R5900_MMI_H

#include <stdint.h>

/* Two 64-bit MMI lanes form one 128-bit edge264 vector. */
typedef struct {
    uint64_t lo;
    uint64_t hi;
} Edge264R5900Vec128;

/* Unsigned byte addition modulo 256, eight lanes per MMI instruction. */
Edge264R5900Vec128 edge264_r5900_add_u8(Edge264R5900Vec128 a,
                                        Edge264R5900Vec128 b);

/* Signed halfword addition with saturation, four lanes per MMI instruction. */
Edge264R5900Vec128 edge264_r5900_add_s16_sat(Edge264R5900Vec128 a,
                                             Edge264R5900Vec128 b);

/* Four signed halfword products accumulated pairwise into two words. */
Edge264R5900Vec128 edge264_r5900_madd_s16(Edge264R5900Vec128 a,
                                          Edge264R5900Vec128 b);

/* Unsigned byte subtraction modulo 256. */
Edge264R5900Vec128 edge264_r5900_sub_u8(Edge264R5900Vec128 a,
                                        Edge264R5900Vec128 b);

/* Signed halfword lane-wise minimum and maximum. */
Edge264R5900Vec128 edge264_r5900_min_s16(Edge264R5900Vec128 a,
                                         Edge264R5900Vec128 b);
Edge264R5900Vec128 edge264_r5900_max_s16(Edge264R5900Vec128 a,
                                         Edge264R5900Vec128 b);

/* Signed halfword compare: each result lane is 0xffff or zero. */
Edge264R5900Vec128 edge264_r5900_cmpgt_s16(Edge264R5900Vec128 a,
                                           Edge264R5900Vec128 b);
Edge264R5900Vec128 edge264_r5900_cmpeq_s16(Edge264R5900Vec128 a,
                                           Edge264R5900Vec128 b);

/* Bitwise operations over all 128 bits. */
Edge264R5900Vec128 edge264_r5900_and(Edge264R5900Vec128 a,
                                     Edge264R5900Vec128 b);
Edge264R5900Vec128 edge264_r5900_or(Edge264R5900Vec128 a,
                                    Edge264R5900Vec128 b);
Edge264R5900Vec128 edge264_r5900_xor(Edge264R5900Vec128 a,
                                     Edge264R5900Vec128 b);

#endif
