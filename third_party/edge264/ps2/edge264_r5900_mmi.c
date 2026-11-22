#include "edge264_r5900_mmi.h"

#if defined(__mips__) && defined(__GNUC__)

Edge264R5900Vec128 edge264_r5900_add_u8(Edge264R5900Vec128 a,
                                        Edge264R5900Vec128 b) {
    Edge264R5900Vec128 r;
    __asm__ volatile(
        "paddub %0, %2, %4\n\t"
        "paddub %1, %3, %5"
        : "=&r"(r.lo), "=&r"(r.hi)
        : "r"(a.lo), "r"(a.hi), "r"(b.lo), "r"(b.hi));
    return r;
}

Edge264R5900Vec128 edge264_r5900_add_s16_sat(Edge264R5900Vec128 a,
                                             Edge264R5900Vec128 b) {
    Edge264R5900Vec128 r;
    __asm__ volatile(
        "paddsh %0, %2, %4\n\t"
        "paddsh %1, %3, %5"
        : "=&r"(r.lo), "=&r"(r.hi)
        : "r"(a.lo), "r"(a.hi), "r"(b.lo), "r"(b.hi));
    return r;
}

Edge264R5900Vec128 edge264_r5900_madd_s16(Edge264R5900Vec128 a,
                                          Edge264R5900Vec128 b) {
    Edge264R5900Vec128 r;
    __asm__ volatile(
        "pmaddh %0, %2, %4\n\t"
        "pmaddh %1, %3, %5"
        : "=&r"(r.lo), "=&r"(r.hi)
        : "r"(a.lo), "r"(a.hi), "r"(b.lo), "r"(b.hi));
    return r;
}


Edge264R5900Vec128 edge264_r5900_sub_u8(Edge264R5900Vec128 a,
                                        Edge264R5900Vec128 b) {
    Edge264R5900Vec128 r;
    __asm__ volatile(
        "psubub %0, %2, %4\n\t"
        "psubub %1, %3, %5"
        : "=&r"(r.lo), "=&r"(r.hi)
        : "r"(a.lo), "r"(a.hi), "r"(b.lo), "r"(b.hi));
    return r;
}

Edge264R5900Vec128 edge264_r5900_min_s16(Edge264R5900Vec128 a,
                                         Edge264R5900Vec128 b) {
    Edge264R5900Vec128 r;
    __asm__ volatile(
        "pminh %0, %2, %4\n\t"
        "pminh %1, %3, %5"
        : "=&r"(r.lo), "=&r"(r.hi)
        : "r"(a.lo), "r"(a.hi), "r"(b.lo), "r"(b.hi));
    return r;
}

Edge264R5900Vec128 edge264_r5900_max_s16(Edge264R5900Vec128 a,
                                         Edge264R5900Vec128 b) {
    Edge264R5900Vec128 r;
    __asm__ volatile(
        "pmaxh %0, %2, %4\n\t"
        "pmaxh %1, %3, %5"
        : "=&r"(r.lo), "=&r"(r.hi)
        : "r"(a.lo), "r"(a.hi), "r"(b.lo), "r"(b.hi));
    return r;
}

Edge264R5900Vec128 edge264_r5900_cmpgt_s16(Edge264R5900Vec128 a,
                                           Edge264R5900Vec128 b) {
    Edge264R5900Vec128 r;
    __asm__ volatile(
        "pcgth %0, %2, %4\n\t"
        "pcgth %1, %3, %5"
        : "=&r"(r.lo), "=&r"(r.hi)
        : "r"(a.lo), "r"(a.hi), "r"(b.lo), "r"(b.hi));
    return r;
}

Edge264R5900Vec128 edge264_r5900_cmpeq_s16(Edge264R5900Vec128 a,
                                           Edge264R5900Vec128 b) {
    Edge264R5900Vec128 r;
    __asm__ volatile(
        "pceqh %0, %2, %4\n\t"
        "pceqh %1, %3, %5"
        : "=&r"(r.lo), "=&r"(r.hi)
        : "r"(a.lo), "r"(a.hi), "r"(b.lo), "r"(b.hi));
    return r;
}

Edge264R5900Vec128 edge264_r5900_and(Edge264R5900Vec128 a,
                                     Edge264R5900Vec128 b) {
    Edge264R5900Vec128 r;
    __asm__ volatile(
        "pand %0, %2, %4\n\t"
        "pand %1, %3, %5"
        : "=&r"(r.lo), "=&r"(r.hi)
        : "r"(a.lo), "r"(a.hi), "r"(b.lo), "r"(b.hi));
    return r;
}

Edge264R5900Vec128 edge264_r5900_or(Edge264R5900Vec128 a,
                                    Edge264R5900Vec128 b) {
    Edge264R5900Vec128 r;
    __asm__ volatile(
        "por %0, %2, %4\n\t"
        "por %1, %3, %5"
        : "=&r"(r.lo), "=&r"(r.hi)
        : "r"(a.lo), "r"(a.hi), "r"(b.lo), "r"(b.hi));
    return r;
}

Edge264R5900Vec128 edge264_r5900_xor(Edge264R5900Vec128 a,
                                     Edge264R5900Vec128 b) {
    Edge264R5900Vec128 r;
    __asm__ volatile(
        "pxor %0, %2, %4\n\t"
        "pxor %1, %3, %5"
        : "=&r"(r.lo), "=&r"(r.hi)
        : "r"(a.lo), "r"(a.hi), "r"(b.lo), "r"(b.hi));
    return r;
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

#endif
