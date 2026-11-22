/*
 * Compile-only capability probe for the PS2 EE toolchain.
 * This file is not part of the application and must never be linked into it.
 */
#include <stddef.h>
#include <stdint.h>

#if !defined(__GNUC__)
#error "The R5900 port currently requires a GCC-compatible compiler"
#endif

#if !defined(__mips__)
#error "This probe must be compiled for the MIPS/R5900 target, not the host"
#endif

#if defined(__mips64)
#error "The PS2 EE target must use the 32-bit MIPS ABI"
#endif

#if defined(__SIZEOF_POINTER__) && __SIZEOF_POINTER__ != 4
#error "Unexpected pointer width: expected 32-bit EE ABI"
#endif

#if defined(__SIZEOF_LONG_LONG__) && __SIZEOF_LONG_LONG__ != 8
#error "Unexpected long long width"
#endif

typedef uint8_t v16u8 __attribute__((vector_size(16), aligned(16)));
typedef int16_t v8i16 __attribute__((vector_size(16), aligned(16)));
typedef uint32_t v4u32 __attribute__((vector_size(16), aligned(16)));
typedef uint32_t v4u32_mask __attribute__((vector_size(16), aligned(16)));

_Static_assert(sizeof(v16u8) == 16, "16-byte vector type unavailable");
_Static_assert(sizeof(v8i16) == 16, "16-byte signed vector type unavailable");
_Static_assert(sizeof(v4u32) == 16, "four-lane 32-bit vector unavailable");

/* GCC vector arithmetic and comparisons used by the proposed scalar/MMI layer. */
__attribute__((noinline)) v16u8 probe_vector_add(v16u8 a, v16u8 b) {
    return a + b;
}

__attribute__((noinline)) v8i16 probe_vector_saturate_candidate(v8i16 a, v8i16 b) {
    v8i16 sum = a + b;
    v8i16 hi = {32767,32767,32767,32767,32767,32767,32767,32767};
    v8i16 lo = {-32768,-32768,-32768,-32768,-32768,-32768,-32768,-32768};
    sum = ((sum > hi) & hi) | (~(sum > hi) & sum);
    return ((sum < lo) & lo) | (~(sum < lo) & sum);
}

/* GCC's vector permutation builtin is a candidate for replacing Clang shuffles. */
__attribute__((noinline)) v4u32 probe_vector_shuffle(v4u32 a, v4u32 b) {
    const v4u32_mask mask = {0, 5, 2, 7};
    return __builtin_shuffle(a, b, mask);
}

/* Keep 64-bit arithmetic visible to the optimizer and assembler. */
__attribute__((noinline)) uint64_t probe_u64(uint64_t a, uint64_t b) {
    return (a << 17) ^ (b >> 11) ^ (a * UINT64_C(0x9e3779b1));
}

#if defined(__mips__)
/* These functions verify assembler support for the MMI instruction family. */
__attribute__((noinline)) uint64_t probe_mmi_paddub(uint64_t a, uint64_t b) {
    __asm__ volatile("paddub %0, %0, %1" : "+r"(a) : "r"(b));
    return a;
}

__attribute__((noinline)) uint64_t probe_mmi_qfsrv(uint64_t a, uint64_t b) {
    __asm__ volatile("qfsrv %0, %0, %1" : "+r"(a) : "r"(b));
    return a;
}

__attribute__((noinline)) uint64_t probe_mmi_paddsh(uint64_t a, uint64_t b) {
    __asm__ volatile("paddsh %0, %0, %1" : "+r"(a) : "r"(b));
    return a;
}

__attribute__((noinline)) uint64_t probe_mmi_pmaddh(uint64_t a, uint64_t b) {
    __asm__ volatile("pmaddh %0, %0, %1" : "+r"(a) : "r"(b));
    return a;
}

__attribute__((noinline)) uint64_t probe_mmi_psubub(uint64_t a, uint64_t b) {
    __asm__ volatile("psubub %0, %0, %1" : "+r"(a) : "r"(b));
    return a;
}
__attribute__((noinline)) uint64_t probe_mmi_pminh(uint64_t a, uint64_t b) {
    __asm__ volatile("pminh %0, %0, %1" : "+r"(a) : "r"(b));
    return a;
}
__attribute__((noinline)) uint64_t probe_mmi_pmaxh(uint64_t a, uint64_t b) {
    __asm__ volatile("pmaxh %0, %0, %1" : "+r"(a) : "r"(b));
    return a;
}
__attribute__((noinline)) uint64_t probe_mmi_pcgth(uint64_t a, uint64_t b) {
    __asm__ volatile("pcgth %0, %0, %1" : "+r"(a) : "r"(b));
    return a;
}
__attribute__((noinline)) uint64_t probe_mmi_pceqh(uint64_t a, uint64_t b) {
    __asm__ volatile("pceqh %0, %0, %1" : "+r"(a) : "r"(b));
    return a;
}
__attribute__((noinline)) uint64_t probe_mmi_pand(uint64_t a, uint64_t b) {
    __asm__ volatile("pand %0, %0, %1" : "+r"(a) : "r"(b));
    return a;
}
__attribute__((noinline)) uint64_t probe_mmi_por(uint64_t a, uint64_t b) {
    __asm__ volatile("por %0, %0, %1" : "+r"(a) : "r"(b));
    return a;
}
__attribute__((noinline)) uint64_t probe_mmi_pxor(uint64_t a, uint64_t b) {
    __asm__ volatile("pxor %0, %0, %1" : "+r"(a) : "r"(b));
    return a;
}
#else
#error "R5900 MMI is not enabled; compile with the PS2 EE -march/-mabi flags"
#endif

/* Force the compiler to retain references to every probe operation. */
void r5900_probe_link_anchor(void) {
    volatile v16u8 a8 = {0};
    volatile v8i16 a16 = {0};
    volatile v4u32 a32 = {0};
    volatile uint64_t a64 = 1;
    (void)probe_vector_add(a8, a8);
    (void)probe_vector_saturate_candidate(a16, a16);
    (void)probe_vector_shuffle(a32, a32);
    (void)probe_u64(a64, a64);
    (void)probe_mmi_paddub(a64, a64);
    (void)probe_mmi_qfsrv(a64, a64);
    (void)probe_mmi_paddsh(a64, a64);
    (void)probe_mmi_pmaddh(a64, a64);
    (void)probe_mmi_psubub(a64, a64);
    (void)probe_mmi_pminh(a64, a64);
    (void)probe_mmi_pmaxh(a64, a64);
    (void)probe_mmi_pcgth(a64, a64);
    (void)probe_mmi_pceqh(a64, a64);
    (void)probe_mmi_pand(a64, a64);
    (void)probe_mmi_por(a64, a64);
    (void)probe_mmi_pxor(a64, a64);
}
