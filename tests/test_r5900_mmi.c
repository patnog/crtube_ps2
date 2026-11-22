#include "../third_party/edge264/ps2/edge264_r5900_mmi.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int test_add_u8(void) {
    uint8_t a[16], b[16], expected[16];
    Edge264R5900Vec128 av, bv, got;
    unsigned i;
    for (i = 0; i < 16; ++i) {
        a[i] = (uint8_t)(250u - i * 7u);
        b[i] = (uint8_t)(i * 19u + 11u);
        expected[i] = (uint8_t)(a[i] + b[i]);
    }
    memcpy(&av, a, 16); memcpy(&bv, b, 16);
    got = edge264_r5900_add_u8(av, bv);
    return memcmp(&got, expected, 16) == 0;
}

static int test_add_s16_sat(void) {
    int16_t a[8] = {32760, -32760, 1200, -1200, 10, -10, 30000, -30000};
    int16_t b[8] = {100, -100, 2300, -2300, -20, 20, 10000, -10000};
    int16_t expected[8] = {32767, -32768, 3500, -3500, -10, 10, 32767, -32768};
    Edge264R5900Vec128 av, bv, got;
    memcpy(&av, a, 16); memcpy(&bv, b, 16);
    got = edge264_r5900_add_s16_sat(av, bv);
    return memcmp(&got, expected, 16) == 0;
}

static int test_madd_s16(void) {
    int16_t a[8] = {1, 2, -3, 4, 300, -200, -7, -8};
    int16_t b[8] = {5, 6, 7, -8, 2, 3, 9, 10};
    int32_t expected[4] = {17, -53, 0, -143};
    Edge264R5900Vec128 av, bv, got;
    memcpy(&av, a, 16); memcpy(&bv, b, 16);
    got = edge264_r5900_madd_s16(av, bv);
    return memcmp(&got, expected, 16) == 0;
}


static int test_sub_u8(void) {
    uint8_t a[16], b[16], expected[16];
    Edge264R5900Vec128 av, bv, got;
    unsigned i;
    for (i = 0; i < 16; ++i) {
        a[i] = (uint8_t)(i * 13u + 3u);
        b[i] = (uint8_t)(250u - i * 9u);
        expected[i] = (uint8_t)(a[i] - b[i]);
    }
    memcpy(&av, a, 16); memcpy(&bv, b, 16);
    got = edge264_r5900_sub_u8(av, bv);
    return memcmp(&got, expected, 16) == 0;
}

static int test_minmax_compare(void) {
    int16_t a[8] = {-32768, -100, 0, 100, 32767, 12, -12, 42};
    int16_t b[8] = {32767, -100, 1, -100, -32768, 13, -13, 42};
    int16_t min_expected[8], max_expected[8];
    uint16_t gt_expected[8], eq_expected[8];
    Edge264R5900Vec128 av, bv, got;
    unsigned i;
    for (i = 0; i < 8; ++i) {
        min_expected[i] = a[i] < b[i] ? a[i] : b[i];
        max_expected[i] = a[i] > b[i] ? a[i] : b[i];
        gt_expected[i] = a[i] > b[i] ? UINT16_MAX : 0;
        eq_expected[i] = a[i] == b[i] ? UINT16_MAX : 0;
    }
    memcpy(&av, a, 16); memcpy(&bv, b, 16);
    got = edge264_r5900_min_s16(av, bv);
    if (memcmp(&got, min_expected, 16)) return 0;
    got = edge264_r5900_max_s16(av, bv);
    if (memcmp(&got, max_expected, 16)) return 0;
    got = edge264_r5900_cmpgt_s16(av, bv);
    if (memcmp(&got, gt_expected, 16)) return 0;
    got = edge264_r5900_cmpeq_s16(av, bv);
    return memcmp(&got, eq_expected, 16) == 0;
}

static int test_bitwise(void) {
    Edge264R5900Vec128 a = {UINT64_C(0xf0f00f0faa55aa55), UINT64_C(0x123456789abcdef0)};
    Edge264R5900Vec128 b = {UINT64_C(0x0ff03333ffff0000), UINT64_C(0xffff0000aaaa5555)};
    Edge264R5900Vec128 r;
    r = edge264_r5900_and(a, b);
    if (r.lo != (a.lo & b.lo) || r.hi != (a.hi & b.hi)) return 0;
    r = edge264_r5900_or(a, b);
    if (r.lo != (a.lo | b.lo) || r.hi != (a.hi | b.hi)) return 0;
    r = edge264_r5900_xor(a, b);
    return r.lo == (a.lo ^ b.lo) && r.hi == (a.hi ^ b.hi);
}

int main(void) {
    if (!test_add_u8()) { puts("MMI add_u8: FAIL"); return 1; }
    if (!test_add_s16_sat()) { puts("MMI add_s16_sat: FAIL"); return 1; }
    if (!test_madd_s16()) { puts("MMI madd_s16: FAIL"); return 1; }
    if (!test_sub_u8()) { puts("MMI sub_u8: FAIL"); return 1; }
    if (!test_minmax_compare()) { puts("MMI min/max/compare: FAIL"); return 1; }
    if (!test_bitwise()) { puts("MMI bitwise: FAIL"); return 1; }
    puts("R5900 MMI primitive reference tests: PASS");
    return 0;
}
