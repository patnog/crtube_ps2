#include "edge264_r5900_mmi.h"
#include <string.h>
#include <limits.h>
#ifdef CRTUBE_R5900_MMI_TEST_ONLY
#include "debug_log.h"
#endif

#if defined(CRTUBE_H264_MMI_MATRIX_TEST)
static volatile unsigned edge264_r5900_runtime_mode;
static volatile unsigned edge264_r5900_runtime_counters[7];

void edge264_r5900_mmi_reset_counters(void)
{
    unsigned i;
    for (i = 0; i < 7; ++i)
        edge264_r5900_runtime_counters[i] = 0;
}

void edge264_r5900_mmi_note(unsigned operation)
{
    if (operation == EDGE264_R5900_MMI_OP_CHROMA)
        ++edge264_r5900_runtime_counters[0];
    else if (operation == EDGE264_R5900_MMI_OP_MINMAX)
        ++edge264_r5900_runtime_counters[1];
    else if (operation == EDGE264_R5900_MMI_OP_RESIDUAL)
        ++edge264_r5900_runtime_counters[2];
    else if (operation == EDGE264_R5900_MMI_OP_WEIGHTED)
        ++edge264_r5900_runtime_counters[3];
    else if (operation == EDGE264_R5900_MMI_OP_DEBLOCK_ADD)
        ++edge264_r5900_runtime_counters[4];
    else if (operation == EDGE264_R5900_MMI_OP_DEBLOCK_PACKABD)
        ++edge264_r5900_runtime_counters[5];
    else if (operation == EDGE264_R5900_MMI_OP_DEBLOCK_PACKABD_ALT)
        ++edge264_r5900_runtime_counters[6];
}

unsigned edge264_r5900_mmi_get_counter(unsigned operation)
{
    if (operation == EDGE264_R5900_MMI_OP_CHROMA)
        return edge264_r5900_runtime_counters[0];
    if (operation == EDGE264_R5900_MMI_OP_MINMAX)
        return edge264_r5900_runtime_counters[1];
    if (operation == EDGE264_R5900_MMI_OP_RESIDUAL)
        return edge264_r5900_runtime_counters[2];
    if (operation == EDGE264_R5900_MMI_OP_WEIGHTED)
        return edge264_r5900_runtime_counters[3];
    if (operation == EDGE264_R5900_MMI_OP_DEBLOCK_ADD)
        return edge264_r5900_runtime_counters[4];
    if (operation == EDGE264_R5900_MMI_OP_DEBLOCK_PACKABD)
        return edge264_r5900_runtime_counters[5];
    if (operation == EDGE264_R5900_MMI_OP_DEBLOCK_PACKABD_ALT)
        return edge264_r5900_runtime_counters[6];
    return 0;
}

void edge264_r5900_mmi_set_mode(unsigned mask)
{
    edge264_r5900_runtime_mode = mask;
}

unsigned edge264_r5900_mmi_get_mode(void)
{
    return edge264_r5900_runtime_mode;
}

int edge264_r5900_mmi_enabled(unsigned mask)
{
    return (edge264_r5900_runtime_mode & mask) == mask;
}

#endif

#if defined(__mips__) && defined(__GNUC__)

/*
 * R5900 GPRs are 128-bit for MMI.  A C "r" operand containing uint64_t
 * only describes the low 64 bits, so it is NOT a valid MMI vector operand.
 * Always transfer complete aligned vectors with LQ/SQ and operate on one
 * physical 128-bit GPR.  $8-$13 are explicitly clobbered so GCC cannot use
 * them for address operands.
 */
#define EDGE264_MMI_BINARY(name, instruction)                              \
Edge264R5900Vec128 name(Edge264R5900Vec128 a, Edge264R5900Vec128 b)         \
{                                                                           \
    Edge264R5900Vec128 aa __attribute__((aligned(16))) = a;                \
    Edge264R5900Vec128 bb __attribute__((aligned(16))) = b;                \
    Edge264R5900Vec128 rr __attribute__((aligned(16)));                    \
    __asm__ volatile(                                                       \
        "lq $8, %1\n\t"                                                  \
        "lq $9, %2\n\t"                                                  \
        instruction " $8, $8, $9\n\t"                                      \
        "sq $8, %0\n\t"                                                 \
        : "=m"(rr)                                                        \
        : "m"(aa), "m"(bb)                                              \
        : "$8", "$9", "memory");                                          \
    return rr;                                                              \
}

#define EDGE264_MMI_UNARY(name, instruction)                               \
Edge264R5900Vec128 name(Edge264R5900Vec128 a, Edge264R5900Vec128 b)         \
{                                                                           \
    Edge264R5900Vec128 aa __attribute__((aligned(16))) = a;                \
    Edge264R5900Vec128 bb __attribute__((aligned(16))) = b;                \
    Edge264R5900Vec128 rr __attribute__((aligned(16)));                    \
    __asm__ volatile(                                                       \
        "lq $8, %1\n\t"                                                  \
        "lq $9, %2\n\t"                                                  \
        instruction " $8, $8, $9\n\t"                                      \
        "sq $8, %0\n\t"                                                 \
        : "=m"(rr)                                                        \
        : "m"(aa), "m"(bb)                                              \
        : "$8", "$9", "memory");                                          \
    return rr;                                                              \
}

EDGE264_MMI_BINARY(edge264_r5900_add_s32, "paddw")
EDGE264_MMI_BINARY(edge264_r5900_sub_s32, "psubw")
EDGE264_MMI_BINARY(edge264_r5900_add_s16, "paddh")
EDGE264_MMI_BINARY(edge264_r5900_sub_s16, "psubh")
EDGE264_MMI_BINARY(edge264_r5900_add_s16_sat, "paddsh")
EDGE264_MMI_BINARY(edge264_r5900_min_s16, "pminh")
EDGE264_MMI_BINARY(edge264_r5900_max_s16, "pmaxh")
EDGE264_MMI_BINARY(edge264_r5900_cmpgt_s16, "pcgth")
EDGE264_MMI_BINARY(edge264_r5900_cmpeq_s16, "pceqh")
EDGE264_MMI_BINARY(edge264_r5900_and, "pand")
EDGE264_MMI_BINARY(edge264_r5900_or, "por")
EDGE264_MMI_BINARY(edge264_r5900_xor, "pxor")
EDGE264_MMI_BINARY(edge264_r5900_add_u8_sat, "paddub")

#undef EDGE264_MMI_BINARY

/*
 * Keep these three operations scalar until their EE instruction behavior has
 * been independently characterized on hardware.  The initial EE self-test
 * exposed mismatches in PADDUB, PMADDH and PSUBUB; using them in the decoder
 * would corrupt pixels.  The remaining MMI operations stay enabled.
 */
Edge264R5900Vec128 edge264_r5900_add_u8(Edge264R5900Vec128 a,
                                        Edge264R5900Vec128 b)
{
    Edge264R5900Vec128 r;
    uint8_t av[16], bv[16], rv[16];
    unsigned i;
    memcpy(av, &a, 16);
    memcpy(bv, &b, 16);
    for (i = 0; i < 16; ++i)
        rv[i] = (uint8_t)(av[i] + bv[i]);
    memcpy(&r, rv, 16);
    return r;
}

Edge264R5900Vec128 edge264_r5900_madd_s16(Edge264R5900Vec128 a,
                                          Edge264R5900Vec128 b)
{
    Edge264R5900Vec128 aa __attribute__((aligned(16))) = a;
    Edge264R5900Vec128 bb __attribute__((aligned(16))) = b;
    Edge264R5900Vec128 r __attribute__((aligned(16)));
    /* PHMADH is the R5900 horizontal signed-halfword dot product.
     * PMADDH is not equivalent: it writes individual products to HI/LO.
     * Seed both accumulators because PHMADH also updates them. */
    __asm__ volatile(
        "por $10, $0, $0\n\t"
        "pmtlo $10\n\t"
        "pmthi $10\n\t"
        "lq $8, %1\n\t"
        "lq $9, %2\n\t"
        "phmadh $12, $8, $9\n\t"
        "sq $12, %0\n\t"
        : "=m"(r)
        : "m"(aa), "m"(bb)
        : "$8", "$9", "$10", "$12", "memory");
    return r;
}

Edge264R5900Vec128 edge264_r5900_sub_u8(Edge264R5900Vec128 a,
                                        Edge264R5900Vec128 b)
{
    Edge264R5900Vec128 r;
    uint8_t av[16], bv[16], rv[16];
    unsigned i;
    memcpy(av, &a, 16);
    memcpy(bv, &b, 16);
    for (i = 0; i < 16; ++i)
        rv[i] = (uint8_t)(av[i] - bv[i]);
    memcpy(&r, rv, 16);
    return r;
}

#undef EDGE264_MMI_UNARY

#define EDGE264_MMI_SHIFT(name, instruction, amount)                       \
Edge264R5900Vec128 name(Edge264R5900Vec128 a)                               \
{                                                                           \
    Edge264R5900Vec128 aa __attribute__((aligned(16))) = a;                \
    Edge264R5900Vec128 rr __attribute__((aligned(16)));                    \
    __asm__ volatile(                                                       \
        "lq $8, %1\n\t"                                               \
        instruction " $8, $8, " #amount "\n\t"                       \
        "sq $8, %0\n\t"                                              \
        : "=m"(rr) : "m"(aa) : "$8", "memory");                           \
    return rr;                                                              \
}

EDGE264_MMI_SHIFT(edge264_r5900_sar_s32_1, "psraw", 1)
EDGE264_MMI_SHIFT(edge264_r5900_sar_s16_1, "psrah", 1)
EDGE264_MMI_SHIFT(edge264_r5900_sar_s16_2, "psrah", 2)
EDGE264_MMI_SHIFT(edge264_r5900_sar_s16_6, "psrah", 6)
#undef EDGE264_MMI_SHIFT

static Edge264R5900Vec128 edge264_r5900_minmax_u8(
    Edge264R5900Vec128 a, Edge264R5900Vec128 b, int want_max)
{
    Edge264R5900Vec128 aa __attribute__((aligned(16))) = a;
    Edge264R5900Vec128 bb __attribute__((aligned(16))) = b;
    static const uint64_t sign_words[2] __attribute__((aligned(16))) = {
        UINT64_C(0x8080808080808080), UINT64_C(0x8080808080808080)
    };
    Edge264R5900Vec128 rr __attribute__((aligned(16)));
    __asm__ volatile(
        "lq $8, %1\n\t"
        "lq $9, %2\n\t"
        "lq $10, %3\n\t"
        "pxor $8, $8, $10\n\t"
        "pxor $9, $9, $10\n\t"
        "pcgtb $11, $8, $9\n\t"
        "beqz %4, 1f\n\t"
        "nop\n\t"
        /* max: choose a where a>b, otherwise b */
        "pand $12, $11, $8\n\t"
        "pnor $11, $11, $0\n\t"
        "pand $13, $11, $9\n\t"
        "b 2f\n\t"
        "nop\n"
        "1:\n\t"
        /* min: choose b where a>b, otherwise a */
        "pand $12, $11, $9\n\t"
        "pnor $11, $11, $0\n\t"
        "pand $13, $11, $8\n\t"
        "2:\n\t"
        "por $8, $12, $13\n\t"
        "pxor $8, $8, $10\n\t"
        "sq $8, %0\n\t"
        : "=m"(rr)
        : "m"(aa), "m"(bb), "m"(sign_words), "r"(want_max)
        : "$8", "$9", "$10", "$11", "$12", "$13", "memory");
    return rr;
}

Edge264R5900Vec128 edge264_r5900_min_u8(Edge264R5900Vec128 a,
                                        Edge264R5900Vec128 b)
{
    return edge264_r5900_minmax_u8(a, b, 0);
}

Edge264R5900Vec128 edge264_r5900_max_u8(Edge264R5900Vec128 a,
                                        Edge264R5900Vec128 b)
{
    return edge264_r5900_minmax_u8(a, b, 1);
}


/* Direct probes bypass all scalar wrappers: LQ -> raw MMI -> SQ.
 *
 * PMADDH and PHMADH both update the 128-bit HI/LO accumulators.  The test
 * explicitly seeds those registers and captures rd, LO and HI so the oracle
 * covers the complete architectural result rather than only rd.
 */
typedef struct {
    Edge264R5900Vec128 rd;
    Edge264R5900Vec128 lo;
    Edge264R5900Vec128 hi;
} Edge264MmiMulAccResult;

#define EDGE264_DIRECT_MULACC(name, instruction)                            \
static void name(Edge264R5900Vec128 a,                                    \
                 Edge264R5900Vec128 b,                                    \
                 Edge264R5900Vec128 lo_seed,                              \
                 Edge264R5900Vec128 hi_seed,                              \
                 Edge264MmiMulAccResult *out)                             \
{                                                                            \
    Edge264R5900Vec128 aa __attribute__((aligned(16))) = a;                 \
    Edge264R5900Vec128 bb __attribute__((aligned(16))) = b;                 \
    Edge264R5900Vec128 ll __attribute__((aligned(16))) = lo_seed;           \
    Edge264R5900Vec128 hh __attribute__((aligned(16))) = hi_seed;           \
    __asm__ volatile(                                                        \
        "lq $8, %5\n\t"                                                   \
        "lq $9, %6\n\t"                                                   \
        "pmtlo $8\n\t"                                                    \
        "pmthi $9\n\t"                                                    \
        "lq $10, %3\n\t"                                                  \
        "lq $11, %4\n\t"                                                  \
        instruction " $12, $10, $11\n\t"                                 \
        "pmflo $13\n\t"                                                   \
        "pmfhi $14\n\t"                                                   \
        "sq $12, %0\n\t"                                                  \
        "sq $13, %1\n\t"                                                  \
        "sq $14, %2\n\t"                                                  \
        : "=m"(out->rd), "=m"(out->lo), "=m"(out->hi)                     \
        : "m"(aa), "m"(bb), "m"(ll), "m"(hh)                              \
        : "$8", "$9", "$10", "$11", "$12", "$13", "$14", "memory");       \
}

EDGE264_DIRECT_MULACC(edge264_direct_pmaddh, "pmaddh")
EDGE264_DIRECT_MULACC(edge264_direct_phmadh, "phmadh")
#undef EDGE264_DIRECT_MULACC

#define EDGE264_DIRECT_MMI(name, instruction)                               \
static Edge264R5900Vec128 name(Edge264R5900Vec128 a, Edge264R5900Vec128 b)  \
{                                                                            \
    Edge264R5900Vec128 aa __attribute__((aligned(16))) = a;                 \
    Edge264R5900Vec128 bb __attribute__((aligned(16))) = b;                 \
    Edge264R5900Vec128 rr __attribute__((aligned(16)));                    \
    __asm__ volatile(                                                        \
        "lq $8, %1\n\t"                                                   \
        "lq $9, %2\n\t"                                                   \
        instruction " $8, $8, $9\n\t"                                    \
        "sq $8, %0\n\t"                                                   \
        : "=m"(rr) : "m"(aa), "m"(bb) : "$8", "$9", "memory");             \
    return rr;                                                               \
}
EDGE264_DIRECT_MMI(edge264_direct_paddub, "paddub")
EDGE264_DIRECT_MMI(edge264_direct_psubub, "psubub")
#undef EDGE264_DIRECT_MMI

static uint32_t edge264_mmi_test_rng = UINT32_C(0x6d2b79f5);
static uint32_t edge264_mmi_rand(void)
{
    uint32_t x = edge264_mmi_test_rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    edge264_mmi_test_rng = x;
    return x;
}

#ifdef CRTUBE_R5900_MMI_TEST_ONLY
static void edge264_mmi_log_s16(const char *name, const int16_t values[8])
{
    unsigned i;
    crtube_log("  %s:", name);
    for (i = 0; i < 8; ++i)
        crtube_log(" %04x", (unsigned)(uint16_t)values[i]);
    crtube_log("\n");
}

static void edge264_mmi_log_words(const char *name, const uint32_t values[4])
{
    unsigned i;
    crtube_log("  %s:", name);
    for (i = 0; i < 4; ++i)
        crtube_log(" %08x", (unsigned)values[i]);
    crtube_log("\n");
}
#endif

static int edge264_mmi_direct_unsigned_test(int subtract)
{
    unsigned test, lane;
    for (test = 0; test < 20032u; ++test) {
        uint8_t av[16], bv[16], expected[16];
        Edge264R5900Vec128 a __attribute__((aligned(16)));
        Edge264R5900Vec128 b __attribute__((aligned(16)));
        Edge264R5900Vec128 got;
        for (lane = 0; lane < 16; ++lane) {
            if (test < 32) {
                static const uint8_t edge[8] = {0, 1, 2, 3, 0x7f, 0x80, 0xfe, 0xff};
                av[lane] = edge[(test + lane) & 7u];
                bv[lane] = edge[(test * 3u + lane * 5u) & 7u];
            } else {
                av[lane] = (uint8_t)edge264_mmi_rand();
                bv[lane] = (uint8_t)edge264_mmi_rand();
            }
            if (subtract) {
                expected[lane] = av[lane] < bv[lane]
                    ? 0 : (uint8_t)(av[lane] - bv[lane]);
            } else {
                unsigned sum = (unsigned)av[lane] + (unsigned)bv[lane];
                expected[lane] = sum > 255u ? 255u : (uint8_t)sum;
            }
        }
        memcpy(&a, av, 16); memcpy(&b, bv, 16);
        got = subtract ? edge264_direct_psubub(a, b) : edge264_direct_paddub(a, b);
        if (memcmp(&got, expected, 16)) return 0;
        if (!subtract) {
            got = edge264_r5900_add_u8_sat(a, b);
            if (memcmp(&got, expected, 16)) return 0;
        }
    }
    return 1;
}

/* PMADDH: eight independent signed halfword products are added to eight
 * independent 32-bit accumulators distributed across LO and HI.
 *
 * LO words map products 0,1,4,5; HI words map products 2,3,6,7.
 * rd receives the even product lanes: LO0, HI0, LO2, HI2.
 */
static int edge264_mmi_direct_pmaddh_test(void)
{
    unsigned test, lane;
    for (test = 0; test < 20032u; ++test) {
        int16_t av[8], bv[8];
        uint32_t lo_seed[4], hi_seed[4];
        uint32_t expected_rd[4], expected_lo[4], expected_hi[4];
        uint32_t got_rd[4], got_lo[4], got_hi[4];
        Edge264R5900Vec128 a __attribute__((aligned(16)));
        Edge264R5900Vec128 b __attribute__((aligned(16)));
        Edge264R5900Vec128 lo __attribute__((aligned(16)));
        Edge264R5900Vec128 hi __attribute__((aligned(16)));
        Edge264MmiMulAccResult got;

        for (lane = 0; lane < 8; ++lane) {
            if (test < 32) {
                static const int16_t edge[8] = {
                    INT16_MIN, INT16_MIN + 1, -1, 0, 1,
                    INT16_MAX - 1, INT16_MAX, 0x4000
                };
                av[lane] = edge[(test + lane) & 7u];
                bv[lane] = edge[(test * 3u + lane * 5u) & 7u];
            } else {
                av[lane] = (int16_t)edge264_mmi_rand();
                bv[lane] = (int16_t)edge264_mmi_rand();
            }
        }
        for (lane = 0; lane < 4; ++lane) {
            if (test < 32) {
                static const uint32_t seeds[8] = {
                    0u, 1u, UINT32_MAX, UINT32_C(0x7fffffff),
                    UINT32_C(0x80000000), UINT32_C(0x55555555),
                    UINT32_C(0xaaaaaaaa), UINT32_C(0x12345678)
                };
                lo_seed[lane] = seeds[(test + lane) & 7u];
                hi_seed[lane] = seeds[(test * 3u + lane * 3u) & 7u];
            } else {
                lo_seed[lane] = edge264_mmi_rand();
                hi_seed[lane] = edge264_mmi_rand();
            }
        }

        /* Exact architectural mapping, written explicitly to avoid treating
           PMADDH as a horizontal dot-product instruction. */
        expected_lo[0] = lo_seed[0] +
            (uint32_t)((int32_t)av[0] * (int32_t)bv[0]);
        expected_lo[1] = lo_seed[1] +
            (uint32_t)((int32_t)av[1] * (int32_t)bv[1]);
        expected_lo[2] = lo_seed[2] +
            (uint32_t)((int32_t)av[4] * (int32_t)bv[4]);
        expected_lo[3] = lo_seed[3] +
            (uint32_t)((int32_t)av[5] * (int32_t)bv[5]);
        expected_hi[0] = hi_seed[0] +
            (uint32_t)((int32_t)av[2] * (int32_t)bv[2]);
        expected_hi[1] = hi_seed[1] +
            (uint32_t)((int32_t)av[3] * (int32_t)bv[3]);
        expected_hi[2] = hi_seed[2] +
            (uint32_t)((int32_t)av[6] * (int32_t)bv[6]);
        expected_hi[3] = hi_seed[3] +
            (uint32_t)((int32_t)av[7] * (int32_t)bv[7]);
        expected_rd[0] = expected_lo[0];
        expected_rd[1] = expected_hi[0];
        expected_rd[2] = expected_lo[2];
        expected_rd[3] = expected_hi[2];

        memcpy(&a, av, 16); memcpy(&b, bv, 16);
        memcpy(&lo, lo_seed, 16); memcpy(&hi, hi_seed, 16);
        edge264_direct_pmaddh(a, b, lo, hi, &got);
        memcpy(got_rd, &got.rd, 16);
        memcpy(got_lo, &got.lo, 16);
        memcpy(got_hi, &got.hi, 16);
        if (memcmp(got_rd, expected_rd, 16) ||
            memcmp(got_lo, expected_lo, 16) ||
            memcmp(got_hi, expected_hi, 16)) {
#ifdef CRTUBE_R5900_MMI_TEST_ONLY
            crtube_log("PMADDH first mismatch: vector=%u\n", test);
            edge264_mmi_log_s16("A halfwords", av);
            edge264_mmi_log_s16("B halfwords", bv);
            edge264_mmi_log_words("LO seed", lo_seed);
            edge264_mmi_log_words("HI seed", hi_seed);
            edge264_mmi_log_words("rd expected", expected_rd);
            edge264_mmi_log_words("rd actual", got_rd);
            edge264_mmi_log_words("LO expected", expected_lo);
            edge264_mmi_log_words("LO actual", got_lo);
            edge264_mmi_log_words("HI expected", expected_hi);
            edge264_mmi_log_words("HI actual", got_hi);
#endif
            return 0;
        }
    }
    return 1;
}

/* PHMADH is the horizontal dot-product operation: each output word is
 * A[2n]*B[2n] + A[2n+1]*B[2n+1].  Keep it as a separate probe so the test
 * makes the PMADDH/PHMADH distinction explicit on real hardware.
 */
static int edge264_mmi_direct_phmadh_test(void)
{
    unsigned test, lane;
    uint32_t zero_words[4] = {0, 0, 0, 0};
    for (test = 0; test < 20032u; ++test) {
        int16_t av[8], bv[8];
        uint32_t expected_rd[4], expected_lo[4], expected_hi[4];
        uint32_t got_rd[4], got_lo[4], got_hi[4];
        Edge264R5900Vec128 a __attribute__((aligned(16)));
        Edge264R5900Vec128 b __attribute__((aligned(16)));
        Edge264R5900Vec128 zero __attribute__((aligned(16)));
        Edge264MmiMulAccResult got;

        for (lane = 0; lane < 8; ++lane) {
            if (test < 32) {
                static const int16_t edge[8] = {
                    INT16_MIN, INT16_MIN + 1, -1, 0, 1,
                    INT16_MAX - 1, INT16_MAX, 0x4000
                };
                av[lane] = edge[(test + lane) & 7u];
                bv[lane] = edge[(test * 3u + lane * 5u) & 7u];
            } else {
                av[lane] = (int16_t)edge264_mmi_rand();
                bv[lane] = (int16_t)edge264_mmi_rand();
            }
        }
        expected_rd[0] = (uint32_t)((int64_t)av[0] * bv[0] +
                                    (int64_t)av[1] * bv[1]);
        expected_rd[1] = (uint32_t)((int64_t)av[2] * bv[2] +
                                    (int64_t)av[3] * bv[3]);
        expected_rd[2] = (uint32_t)((int64_t)av[4] * bv[4] +
                                    (int64_t)av[5] * bv[5]);
        expected_rd[3] = (uint32_t)((int64_t)av[6] * bv[6] +
                                    (int64_t)av[7] * bv[7]);
        expected_lo[0] = expected_rd[0];
        expected_lo[1] = 0;
        expected_lo[2] = expected_rd[2];
        expected_lo[3] = 0;
        expected_hi[0] = expected_rd[1];
        expected_hi[1] = 0;
        expected_hi[2] = expected_rd[3];
        expected_hi[3] = 0;

        memcpy(&a, av, 16); memcpy(&b, bv, 16);
        memcpy(&zero, zero_words, 16);
        edge264_direct_phmadh(a, b, zero, zero, &got);
        memcpy(got_rd, &got.rd, 16);
        memcpy(got_lo, &got.lo, 16);
        memcpy(got_hi, &got.hi, 16);
        /* PHMADH defines only LO words 0/2 and HI words 0/2.
           Words 1/3 in each accumulator are architecturally undefined. */
        if (memcmp(got_rd, expected_rd, 16) ||
            got_lo[0] != expected_lo[0] ||
            got_lo[2] != expected_lo[2] ||
            got_hi[0] != expected_hi[0] ||
            got_hi[2] != expected_hi[2]) {
#ifdef CRTUBE_R5900_MMI_TEST_ONLY
            crtube_log("PHMADH first mismatch: vector=%u\n", test);
            crtube_log("PHMADH accumulator words 1 and 3 are undefined; ignored\n");
            edge264_mmi_log_s16("A halfwords", av);
            edge264_mmi_log_s16("B halfwords", bv);
            edge264_mmi_log_words("rd expected", expected_rd);
            edge264_mmi_log_words("rd actual", got_rd);
            edge264_mmi_log_words("LO expected", expected_lo);
            edge264_mmi_log_words("LO actual", got_lo);
            edge264_mmi_log_words("HI expected", expected_hi);
            edge264_mmi_log_words("HI actual", got_hi);
#endif
            return 0;
        }
    }
    return 1;
}


static int edge264_mmi_packabd_test(void)
{
    unsigned test, lane;
    for (test = 0; test < 20032u; ++test) {
        int16_t av[8], bv[8], cv[8], dv[8];
        uint8_t expected[16];
        Edge264R5900Vec128 a __attribute__((aligned(16)));
        Edge264R5900Vec128 b __attribute__((aligned(16)));
        Edge264R5900Vec128 c __attribute__((aligned(16)));
        Edge264R5900Vec128 d __attribute__((aligned(16)));
        Edge264R5900Vec128 got;
        for (lane = 0; lane < 8; ++lane) {
            if (test < 32u) {
                static const int16_t edge[8] = {INT16_MIN, INT16_MIN + 1,
                    -1, 0, 1, INT16_MAX - 1, INT16_MAX, 0x4000};
                av[lane] = edge[(test + lane) & 7u];
                bv[lane] = edge[(test * 3u + lane * 5u) & 7u];
                cv[lane] = edge[(test * 5u + lane * 3u) & 7u];
                dv[lane] = edge[(test * 7u + lane * 5u) & 7u];
            } else {
                av[lane] = (int16_t)edge264_mmi_rand();
                bv[lane] = (int16_t)edge264_mmi_rand();
                cv[lane] = (int16_t)edge264_mmi_rand();
                dv[lane] = (int16_t)edge264_mmi_rand();
            }
            { int x = (int)av[lane] - (int)bv[lane];
              int y = (int)cv[lane] - (int)dv[lane];
              expected[lane] = (uint8_t)(x < 0 ? -x : x);
              expected[lane + 8] = (uint8_t)(y < 0 ? -y : y); }
        }
        memcpy(&a, av, 16); memcpy(&b, bv, 16);
        memcpy(&c, cv, 16); memcpy(&d, dv, 16);
        got = edge264_r5900_pack_absdiff_s16_u8(a, b, c, d);
        if (memcmp(&got, expected, 16)) {
#ifdef CRTUBE_R5900_MMI_TEST_ONLY
            crtube_log("PACKABD first mismatch: vector=%u\n", test);
#endif
            return 0;
        }
    }
    return 1;
}

/* Execute every primitive on the EE and compare all 128 output bits against
 * scalar reference calculations.  Each bit identifies one failed primitive. */
int edge264_r5900_mmi_selftest(void)
{
    Edge264R5900Vec128 a __attribute__((aligned(16)));
    Edge264R5900Vec128 b __attribute__((aligned(16)));
    Edge264R5900Vec128 got, expected;
    uint8_t au[16], bu[16], eu[16];
    int16_t ah[8], bh[8], eh[8];
    int32_t ew[4];
    unsigned i;
    int failures = 0;

    for (i = 0; i < 16; ++i) {
        au[i] = (uint8_t)(i * 17u + 3u);
        bu[i] = (uint8_t)(255u - i * 11u);
    }
    for (i = 0; i < 8; ++i) {
        ah[i] = (int16_t)((int)i * 701 - 2300);
        bh[i] = (int16_t)(1900 - (int)i * 509);
    }
    memcpy(&a, au, 16); memcpy(&b, bu, 16);

    if (!edge264_mmi_direct_unsigned_test(0)) failures |= 1 << 0;

    memcpy(&a, ah, 16); memcpy(&b, bh, 16);
    for (i = 0; i < 8; ++i) {
        int v = (int)ah[i] + bh[i];
        if (v > INT16_MAX) v = INT16_MAX;
        if (v < INT16_MIN) v = INT16_MIN;
        eh[i] = (int16_t)v;
    }
    memcpy(&expected, eh, 16);
    got = edge264_r5900_add_s16_sat(a, b);
    if (memcmp(&got, &expected, 16)) failures |= 1 << 1;

    if (!edge264_mmi_direct_pmaddh_test()) failures |= 1 << 2;
    if (!edge264_mmi_direct_phmadh_test()) failures |= 1 << 21;
    if (!edge264_mmi_packabd_test()) failures |= 1 << 23;

    /* Verify the production wrapper, not only the raw instruction probe. */
    memcpy(&a, ah, 16); memcpy(&b, bh, 16);
    {
        int32_t dot[4];
        for (i = 0; i < 4; ++i)
            dot[i] = (int32_t)ah[2 * i] * bh[2 * i] +
                     (int32_t)ah[2 * i + 1] * bh[2 * i + 1];
        memcpy(&expected, dot, sizeof(dot));
        got = edge264_r5900_madd_s16(a, b);
        if (memcmp(&got, &expected, sizeof(dot))) failures |= 1 << 22;
    }

    memcpy(&a, au, 16); memcpy(&b, bu, 16);
    for (i = 0; i < 16; ++i) eu[i] = au[i] < bu[i] ? au[i] : bu[i];
    memcpy(&expected, eu, 16);
    got = edge264_r5900_min_u8(a, b);
    if (memcmp(&got, &expected, 16)) failures |= 1 << 3;
    for (i = 0; i < 16; ++i) eu[i] = au[i] > bu[i] ? au[i] : bu[i];
    memcpy(&expected, eu, 16);
    got = edge264_r5900_max_u8(a, b);
    if (memcmp(&got, &expected, 16)) failures |= 1 << 4;
    if (!edge264_mmi_direct_unsigned_test(1)) failures |= 1 << 5;

    memcpy(&a, ah, 16); memcpy(&b, bh, 16);
    for (i = 0; i < 8; ++i) eh[i] = ah[i] < bh[i] ? ah[i] : bh[i];
    memcpy(&expected, eh, 16);
    got = edge264_r5900_min_s16(a, b);
    if (memcmp(&got, &expected, 16)) failures |= 1 << 6;
    for (i = 0; i < 8; ++i) eh[i] = ah[i] > bh[i] ? ah[i] : bh[i];
    memcpy(&expected, eh, 16);
    got = edge264_r5900_max_s16(a, b);
    if (memcmp(&got, &expected, 16)) failures |= 1 << 7;
    for (i = 0; i < 8; ++i) eh[i] = ah[i] > bh[i] ? -1 : 0;
    memcpy(&expected, eh, 16);
    got = edge264_r5900_cmpgt_s16(a, b);
    if (memcmp(&got, &expected, 16)) failures |= 1 << 8;
    for (i = 0; i < 8; ++i) eh[i] = ah[i] == bh[i] ? -1 : 0;
    memcpy(&expected, eh, 16);
    got = edge264_r5900_cmpeq_s16(a, b);
    if (memcmp(&got, &expected, 16)) failures |= 1 << 9;

    got = edge264_r5900_and(a, b);
    expected.lo = a.lo & b.lo; expected.hi = a.hi & b.hi;
    if (memcmp(&got, &expected, 16)) failures |= 1 << 10;
    got = edge264_r5900_or(a, b);
    expected.lo = a.lo | b.lo; expected.hi = a.hi | b.hi;
    if (memcmp(&got, &expected, 16)) failures |= 1 << 11;
    got = edge264_r5900_xor(a, b);
    expected.lo = a.lo ^ b.lo; expected.hi = a.hi ^ b.hi;
    if (memcmp(&got, &expected, 16)) failures |= 1 << 12;

    got = edge264_r5900_add_s32(a, b);
    { int32_t x[4], y[4]; memcpy(x, &a, 16); memcpy(y, &b, 16);
      for (i=0;i<4;i++) x[i]+=y[i]; memcpy(&expected, x, 16); }
    if (memcmp(&got, &expected, 16)) failures |= 1 << 13;
    got = edge264_r5900_sub_s32(a, b);
    { int32_t x[4], y[4]; memcpy(x, &a, 16); memcpy(y, &b, 16);
      for (i=0;i<4;i++) x[i]-=y[i]; memcpy(&expected, x, 16); }
    if (memcmp(&got, &expected, 16)) failures |= 1 << 14;
    got = edge264_r5900_sar_s32_1(a);
    { int32_t x[4]; memcpy(x, &a, 16); for (i=0;i<4;i++) x[i]>>=1; memcpy(&expected, x, 16); }
    if (memcmp(&got, &expected, 16)) failures |= 1 << 15;
    got = edge264_r5900_add_s16(a, b);
    { int16_t x[8], y[8]; memcpy(x, &a, 16); memcpy(y, &b, 16);
      for (i=0;i<8;i++) x[i]=(int16_t)(x[i]+y[i]); memcpy(&expected, x, 16); }
    if (memcmp(&got, &expected, 16)) failures |= 1 << 16;
    got = edge264_r5900_sub_s16(a, b);
    { int16_t x[8], y[8]; memcpy(x, &a, 16); memcpy(y, &b, 16);
      for (i=0;i<8;i++) x[i]=(int16_t)(x[i]-y[i]); memcpy(&expected, x, 16); }
    if (memcmp(&got, &expected, 16)) failures |= 1 << 17;
    got = edge264_r5900_sar_s16_1(a);
    { int16_t x[8]; memcpy(x, &a, 16); for (i=0;i<8;i++) x[i]>>=1; memcpy(&expected, x, 16); }
    if (memcmp(&got, &expected, 16)) failures |= 1 << 18;
    got = edge264_r5900_sar_s16_2(a);
    { int16_t x[8]; memcpy(x, &a, 16); for (i=0;i<8;i++) x[i]>>=2; memcpy(&expected, x, 16); }
    if (memcmp(&got, &expected, 16)) failures |= 1 << 19;
    got = edge264_r5900_sar_s16_6(a);
    { int16_t x[8]; memcpy(x, &a, 16); for (i=0;i<8;i++) x[i]>>=6; memcpy(&expected, x, 16); }
    if (memcmp(&got, &expected, 16)) failures |= 1 << 20;

    return failures;
}

#else

#include <limits.h>
#include <string.h>

Edge264R5900Vec128 edge264_r5900_add_u8(Edge264R5900Vec128 a,
                                        Edge264R5900Vec128 b) {
    Edge264R5900Vec128 r;
    uint8_t av[16], bv[16], rv[16];
    unsigned i;
    memcpy(av, &a, sizeof(av));
    memcpy(bv, &b, sizeof(bv));
    for (i = 0; i < 16; ++i)
        rv[i] = (uint8_t)(av[i] + bv[i]);
    memcpy(&r, rv, sizeof(r));
    return r;
}

Edge264R5900Vec128 edge264_r5900_add_u8_sat(Edge264R5900Vec128 a,
                                            Edge264R5900Vec128 b) {
    Edge264R5900Vec128 r;
    uint8_t av[16], bv[16], rv[16];
    unsigned i;
    memcpy(av, &a, sizeof(av));
    memcpy(bv, &b, sizeof(bv));
    for (i = 0; i < 16; ++i) {
        unsigned sum = (unsigned)av[i] + (unsigned)bv[i];
        rv[i] = (uint8_t)(sum > 255u ? 255u : sum);
    }
    memcpy(&r, rv, sizeof(r));
    return r;
}

Edge264R5900Vec128 edge264_r5900_add_s16_sat(Edge264R5900Vec128 a,
                                             Edge264R5900Vec128 b) {
    Edge264R5900Vec128 r;
    int16_t av[8], bv[8], rv[8];
    unsigned i;
    memcpy(av, &a, sizeof(av));
    memcpy(bv, &b, sizeof(bv));
    for (i = 0; i < 8; ++i) {
        int32_t sum = (int32_t)av[i] + (int32_t)bv[i];
        if (sum > INT16_MAX) sum = INT16_MAX;
        if (sum < INT16_MIN) sum = INT16_MIN;
        rv[i] = (int16_t)sum;
    }
    memcpy(&r, rv, sizeof(r));
    return r;
}

Edge264R5900Vec128 edge264_r5900_madd_s16(Edge264R5900Vec128 a,
                                          Edge264R5900Vec128 b) {
    Edge264R5900Vec128 r;
    int16_t av[8], bv[8];
    int32_t rv[4];
    unsigned i;
    memcpy(av, &a, sizeof(av));
    memcpy(bv, &b, sizeof(bv));
    for (i = 0; i < 4; ++i)
        rv[i] = (int32_t)av[2 * i] * bv[2 * i] +
                (int32_t)av[2 * i + 1] * bv[2 * i + 1];
    memcpy(&r, rv, sizeof(rv));
    memset((uint8_t *)&r + sizeof(rv), 0, sizeof(r) - sizeof(rv));
    return r;
}


Edge264R5900Vec128 edge264_r5900_min_u8(Edge264R5900Vec128 a,
                                        Edge264R5900Vec128 b) {
    Edge264R5900Vec128 r;
    uint8_t av[16], bv[16], rv[16];
    unsigned i;
    memcpy(av, &a, sizeof(av)); memcpy(bv, &b, sizeof(bv));
    for (i = 0; i < 16; ++i) rv[i] = av[i] < bv[i] ? av[i] : bv[i];
    memcpy(&r, rv, sizeof(rv)); return r;
}

Edge264R5900Vec128 edge264_r5900_max_u8(Edge264R5900Vec128 a,
                                        Edge264R5900Vec128 b) {
    Edge264R5900Vec128 r;
    uint8_t av[16], bv[16], rv[16];
    unsigned i;
    memcpy(av, &a, sizeof(av)); memcpy(bv, &b, sizeof(bv));
    for (i = 0; i < 16; ++i) rv[i] = av[i] > bv[i] ? av[i] : bv[i];
    memcpy(&r, rv, sizeof(rv)); return r;
}

Edge264R5900Vec128 edge264_r5900_sub_u8(Edge264R5900Vec128 a,
                                        Edge264R5900Vec128 b) {
    Edge264R5900Vec128 r;
    uint8_t av[16], bv[16], rv[16];
    unsigned i;
    memcpy(av, &a, sizeof(av)); memcpy(bv, &b, sizeof(bv));
    for (i = 0; i < 16; ++i) rv[i] = (uint8_t)(av[i] - bv[i]);
    memcpy(&r, rv, sizeof(r)); return r;
}

Edge264R5900Vec128 edge264_r5900_min_s16(Edge264R5900Vec128 a,
                                         Edge264R5900Vec128 b) {
    Edge264R5900Vec128 r; int16_t av[8], bv[8], rv[8]; unsigned i;
    memcpy(av, &a, 16); memcpy(bv, &b, 16);
    for (i=0;i<8;++i) rv[i] = av[i] < bv[i] ? av[i] : bv[i];
    memcpy(&r, rv, 16); return r;
}

Edge264R5900Vec128 edge264_r5900_max_s16(Edge264R5900Vec128 a,
                                         Edge264R5900Vec128 b) {
    Edge264R5900Vec128 r; int16_t av[8], bv[8], rv[8]; unsigned i;
    memcpy(av, &a, 16); memcpy(bv, &b, 16);
    for (i=0;i<8;++i) rv[i] = av[i] > bv[i] ? av[i] : bv[i];
    memcpy(&r, rv, 16); return r;
}

Edge264R5900Vec128 edge264_r5900_cmpgt_s16(Edge264R5900Vec128 a,
                                           Edge264R5900Vec128 b) {
    Edge264R5900Vec128 r; int16_t av[8], bv[8]; uint16_t rv[8]; unsigned i;
    memcpy(av, &a, 16); memcpy(bv, &b, 16);
    for (i=0;i<8;++i) rv[i] = av[i] > bv[i] ? UINT16_MAX : 0;
    memcpy(&r, rv, 16); return r;
}

Edge264R5900Vec128 edge264_r5900_cmpeq_s16(Edge264R5900Vec128 a,
                                           Edge264R5900Vec128 b) {
    Edge264R5900Vec128 r; int16_t av[8], bv[8]; uint16_t rv[8]; unsigned i;
    memcpy(av, &a, 16); memcpy(bv, &b, 16);
    for (i=0;i<8;++i) rv[i] = av[i] == bv[i] ? UINT16_MAX : 0;
    memcpy(&r, rv, 16); return r;
}

Edge264R5900Vec128 edge264_r5900_and(Edge264R5900Vec128 a,
                                     Edge264R5900Vec128 b) {
    Edge264R5900Vec128 r = {a.lo & b.lo, a.hi & b.hi}; return r;
}
Edge264R5900Vec128 edge264_r5900_or(Edge264R5900Vec128 a,
                                    Edge264R5900Vec128 b) {
    Edge264R5900Vec128 r = {a.lo | b.lo, a.hi | b.hi}; return r;
}
Edge264R5900Vec128 edge264_r5900_xor(Edge264R5900Vec128 a,
                                     Edge264R5900Vec128 b) {
    Edge264R5900Vec128 r = {a.lo ^ b.lo, a.hi ^ b.hi}; return r;
}

Edge264R5900Vec128 edge264_r5900_add_s32(Edge264R5900Vec128 a, Edge264R5900Vec128 b) { Edge264R5900Vec128 r; int32_t x[4], y[4]; unsigned i; memcpy(x,&a,16); memcpy(y,&b,16); for(i=0;i<4;i++) x[i]+=y[i]; memcpy(&r,x,16); return r; }
Edge264R5900Vec128 edge264_r5900_sub_s32(Edge264R5900Vec128 a, Edge264R5900Vec128 b) { Edge264R5900Vec128 r; int32_t x[4], y[4]; unsigned i; memcpy(x,&a,16); memcpy(y,&b,16); for(i=0;i<4;i++) x[i]-=y[i]; memcpy(&r,x,16); return r; }
Edge264R5900Vec128 edge264_r5900_sar_s32_1(Edge264R5900Vec128 a) { Edge264R5900Vec128 r; int32_t x[4]; unsigned i; memcpy(x,&a,16); for(i=0;i<4;i++) x[i]>>=1; memcpy(&r,x,16); return r; }
Edge264R5900Vec128 edge264_r5900_add_s16(Edge264R5900Vec128 a, Edge264R5900Vec128 b) { Edge264R5900Vec128 r; int16_t x[8], y[8]; unsigned i; memcpy(x,&a,16); memcpy(y,&b,16); for(i=0;i<8;i++) x[i]=(int16_t)(x[i]+y[i]); memcpy(&r,x,16); return r; }
Edge264R5900Vec128 edge264_r5900_sub_s16(Edge264R5900Vec128 a, Edge264R5900Vec128 b) { Edge264R5900Vec128 r; int16_t x[8], y[8]; unsigned i; memcpy(x,&a,16); memcpy(y,&b,16); for(i=0;i<8;i++) x[i]=(int16_t)(x[i]-y[i]); memcpy(&r,x,16); return r; }
Edge264R5900Vec128 edge264_r5900_sar_s16_1(Edge264R5900Vec128 a) { Edge264R5900Vec128 r; int16_t x[8]; unsigned i; memcpy(x,&a,16); for(i=0;i<8;i++) x[i]>>=1; memcpy(&r,x,16); return r; }
Edge264R5900Vec128 edge264_r5900_sar_s16_2(Edge264R5900Vec128 a) { Edge264R5900Vec128 r; int16_t x[8]; unsigned i; memcpy(x,&a,16); for(i=0;i<8;i++) x[i]>>=2; memcpy(&r,x,16); return r; }
Edge264R5900Vec128 edge264_r5900_sar_s16_6(Edge264R5900Vec128 a) { Edge264R5900Vec128 r; int16_t x[8]; unsigned i; memcpy(x,&a,16); for(i=0;i<8;i++) x[i]>>=6; memcpy(&r,x,16); return r; }

int edge264_r5900_mmi_selftest(void)
{
    return -1;
}

#endif
