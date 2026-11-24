#ifndef EDGE264_R5900_MMI_H
#define EDGE264_R5900_MMI_H

#include <stdint.h>
#include <string.h>

/* Two 64-bit MMI lanes form one 128-bit edge264 vector. */
typedef struct {
    uint64_t lo;
    uint64_t hi;
} Edge264R5900Vec128;

/* Unsigned byte addition modulo 256, eight lanes per MMI instruction. */
Edge264R5900Vec128 edge264_r5900_add_u8(Edge264R5900Vec128 a,
                                        Edge264R5900Vec128 b);
Edge264R5900Vec128 edge264_r5900_add_u8_sat(Edge264R5900Vec128 a,
                                            Edge264R5900Vec128 b);

/* Signed halfword addition with saturation, four lanes per MMI instruction. */
Edge264R5900Vec128 edge264_r5900_add_s16_sat(Edge264R5900Vec128 a,
                                             Edge264R5900Vec128 b);

/* Horizontal sum of adjacent signed halfword products (PHMADH semantics).
 * The current implementation is a scalar reference; it is not PMADDH. */
Edge264R5900Vec128 edge264_r5900_madd_s16(Edge264R5900Vec128 a,
                                          Edge264R5900Vec128 b);

/* Unsigned byte lane-wise minimum and maximum. */
Edge264R5900Vec128 edge264_r5900_min_u8(Edge264R5900Vec128 a,
                                        Edge264R5900Vec128 b);
Edge264R5900Vec128 edge264_r5900_max_u8(Edge264R5900Vec128 a,
                                        Edge264R5900Vec128 b);

/* Unsigned byte subtraction modulo 256. */
Edge264R5900Vec128 edge264_r5900_sub_u8(Edge264R5900Vec128 a,
                                        Edge264R5900Vec128 b);
#if defined(CRTUBE_H264_MMI_MATRIX_TEST)
int edge264_r5900_mmi_enabled(unsigned mask);
void edge264_r5900_mmi_note(unsigned operation);
#endif

/* Pack abs(signed-halfword difference) low bytes from two vector pairs.
 * max(a,b)-min(a,b) has the same low byte as abs(a-b), including differences
 * above INT16_MAX. PMAXH/PMINH/PSUBH replace the longer compare/select path.
 * Keep inline: this is called repeatedly from deblock_mb. */
static __inline__ __attribute__((__always_inline__))
Edge264R5900Vec128 edge264_r5900_pack_absdiff_s16_u8(
    Edge264R5900Vec128 a, Edge264R5900Vec128 b,
    Edge264R5900Vec128 c, Edge264R5900Vec128 d)
{
#if defined(__mips__) && defined(__GNUC__)
    Edge264R5900Vec128 aa __attribute__((aligned(16))) = a;
    Edge264R5900Vec128 bb __attribute__((aligned(16))) = b;
    Edge264R5900Vec128 cc __attribute__((aligned(16))) = c;
    Edge264R5900Vec128 dd __attribute__((aligned(16))) = d;
    Edge264R5900Vec128 rr __attribute__((aligned(16)));
#if defined(CRTUBE_H264_MMI_MATRIX_TEST)
    if (edge264_r5900_mmi_enabled(1u << 6)) {
        edge264_r5900_mmi_note(1u << 6);
        __asm__ volatile(
            "lq $8, %1\n\t"
            "lq $9, %2\n\t"
            "pcgth $12, $9, $8\n\t"
            "psubh $8, $8, $9\n\t"
            "pxor $8, $8, $12\n\t"
            "psubh $8, $8, $12\n\t"
            "lq $10, %3\n\t"
            "lq $11, %4\n\t"
            "pcgth $13, $11, $10\n\t"
            "psubh $10, $10, $11\n\t"
            "pxor $10, $10, $13\n\t"
            "psubh $10, $10, $13\n\t"
            "ppacb $15, $10, $8\n\t"
            "sq $15, %0\n\t"
            : "=m"(rr)
            : "m"(aa), "m"(bb), "m"(cc), "m"(dd)
            : "$8", "$9", "$10", "$11", "$12", "$13", "$15", "memory");
        return rr;
    }
#endif
    __asm__ volatile(
        "lq $8, %1\n\t"
        "lq $9, %2\n\t"
        "pmaxh $12, $8, $9\n\t"
        "pminh $13, $8, $9\n\t"
        "psubh $8, $12, $13\n\t"
        "lq $10, %3\n\t"
        "lq $11, %4\n\t"
        "pmaxh $12, $10, $11\n\t"
        "pminh $13, $10, $11\n\t"
        "psubh $10, $12, $13\n\t"
        "ppacb $15, $10, $8\n\t"
        "sq $15, %0\n\t"
        : "=m"(rr)
        : "m"(aa), "m"(bb), "m"(cc), "m"(dd)
        : "$8", "$9", "$10", "$11", "$12", "$13", "$15", "memory");
    return rr;
#else
    Edge264R5900Vec128 r;
    int16_t av[8], bv[8], cv[8], dv[8];
    uint8_t rv[16];
    unsigned i;
    memcpy(av, &a, 16); memcpy(bv, &b, 16);
    memcpy(cv, &c, 16); memcpy(dv, &d, 16);
    for (i = 0; i < 8; ++i) {
        int x = (int)av[i] - (int)bv[i];
        int y = (int)cv[i] - (int)dv[i];
        rv[i] = (uint8_t)(x < 0 ? -x : x);
        rv[i + 8] = (uint8_t)(y < 0 ? -y : y);
    }
    memcpy(&r, rv, 16);
    return r;
#endif
}

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

/* Full 128-bit arithmetic used by the residual/transform kernels. */
Edge264R5900Vec128 edge264_r5900_add_s32(Edge264R5900Vec128 a, Edge264R5900Vec128 b);
Edge264R5900Vec128 edge264_r5900_sub_s32(Edge264R5900Vec128 a, Edge264R5900Vec128 b);
Edge264R5900Vec128 edge264_r5900_sar_s32_1(Edge264R5900Vec128 a);
Edge264R5900Vec128 edge264_r5900_add_s16(Edge264R5900Vec128 a, Edge264R5900Vec128 b);
Edge264R5900Vec128 edge264_r5900_sub_s16(Edge264R5900Vec128 a, Edge264R5900Vec128 b);
Edge264R5900Vec128 edge264_r5900_sar_s16_1(Edge264R5900Vec128 a);
Edge264R5900Vec128 edge264_r5900_sar_s16_2(Edge264R5900Vec128 a);
Edge264R5900Vec128 edge264_r5900_sar_s16_6(Edge264R5900Vec128 a);


/* Runtime decoder matrix switches. A zero mask is the scalar reference;
 * individual bits enable one MMI family at a time. */
enum {
    EDGE264_R5900_MMI_OP_CHROMA   = 1u << 0,
    EDGE264_R5900_MMI_OP_MINMAX   = 1u << 1,
    EDGE264_R5900_MMI_OP_RESIDUAL = 1u << 2,
    EDGE264_R5900_MMI_OP_WEIGHTED = 1u << 3,
    EDGE264_R5900_MMI_OP_DEBLOCK_ADD = 1u << 4,
    EDGE264_R5900_MMI_OP_DEBLOCK_PACKABD = 1u << 5,
    EDGE264_R5900_MMI_OP_DEBLOCK_PACKABD_ALT = 1u << 6
};
#if defined(CRTUBE_H264_MMI_MATRIX_TEST)
void edge264_r5900_mmi_set_mode(unsigned mask);
unsigned edge264_r5900_mmi_get_mode(void);
int edge264_r5900_mmi_enabled(unsigned mask);
void edge264_r5900_mmi_reset_counters(void);
void edge264_r5900_mmi_note(unsigned operation);
unsigned edge264_r5900_mmi_get_counter(unsigned operation);
#else
/* Production builds keep the selected MMI paths unconditional: no runtime
 * branch or diagnostic counter is added to the decoder hot path. */
#define edge264_r5900_mmi_enabled(mask) (1)
#define edge264_r5900_mmi_note(operation) ((void)(operation))
#endif

/* Native EE test: zero means every MMI primitive matches its scalar oracle. */
int edge264_r5900_mmi_selftest(void);

#endif
