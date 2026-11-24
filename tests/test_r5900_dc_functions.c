#include "../third_party/edge264/src/edge264_residual.c"
#include <stdio.h>
#ifdef CRTUBE_R5900_SMB_LOG
#include "../include/debug_log.h"
#define TEST_LOG(...) crtube_log(__VA_ARGS__)
#else
#define TEST_LOG(...) printf(__VA_ARGS__)
#endif
#include <stdlib.h>
#include <string.h>

static uint32_t rng_state = 0x6d2b79f5u;
static uint32_t rnd(void) { rng_state ^= rng_state << 13; rng_state ^= rng_state >> 17; rng_state ^= rng_state << 5; return rng_state; }
static uint32_t crc32_update(uint32_t crc, const void *data, size_t n) {
    const uint8_t *p = (const uint8_t *)data;
    while (n--) { crc ^= *p++; for (int k=0;k<8;k++) crc = (crc >> 1) ^ (0xedb88320u & (uint32_t)-(int32_t)(crc & 1)); }
    return crc;
}
static uint32_t digest;
static uint32_t finish_digest(void) { return digest ^ 0xffffffffu; }
static void add_digest(const void *p, size_t n) { digest = crc32_update(digest, p, n); }

static void init_weights(Edge264Context *ctx) {
    for (int p=0;p<6;p++) for (int i=0;i<16;i++) ctx->t.pps.weightScale4x4[p][i] = (uint8_t)(1 + (i * 7 + p * 11) % 32);
}

static int test_add_dc4x4(void) {
    for (int n=0;n<5000;n++) {
        Edge264Context ctx; Edge264Macroblock mbx; uint8_t pixels[16*16] __attribute__((aligned(16)));
        memset(&ctx,0,sizeof(ctx)); memset(&mbx,0,sizeof(mbx));
        ctx._mb=&mbx; ctx.t.stride[0]=16; ctx.t.samples_clip[0][0]=255;
        for (size_t i=0;i<sizeof(pixels);i++) pixels[i]=(uint8_t)rnd();
        ctx.c[16]=(int32_t)(rnd()%20001)-10000;
        init_weights(&ctx);
        add_dc4x4(&ctx,0,0,pixels);
        add_digest(pixels,sizeof(pixels));
    }
    return 0;
}

static int test_transform_dc4x4(void) {
    for (int n=0;n<5000;n++) {
        Edge264Context ctx; Edge264Macroblock mbx; uint8_t pixels[16*16] __attribute__((aligned(16)));
        memset(&ctx,0,sizeof(ctx)); memset(&mbx,0,sizeof(mbx));
        ctx._mb=&mbx; ctx.t.stride[0]=16; ctx.t.samples_clip[0][0]=255;
        ctx.t.QP[0]=(int8_t)(rnd()%31); init_weights(&ctx);
        for (size_t i=0;i<sizeof(pixels);i++) pixels[i]=(uint8_t)rnd();
        ctx.samples_mb[0]=pixels;
        for (int v=0;v<4;v++) for (int i=0;i<4;i++) ctx.c_v[v][i]=(int32_t)(rnd()%2001)-1000;
        if (n & 1) mbx.bits[0] |= 1u << 5; /* retain transformed DC coefficients */
        transform_dc4x4(&ctx,0);
        add_digest(pixels,sizeof(pixels));
        add_digest(ctx.c_v,sizeof(ctx.c_v));
    }
    return 0;
}

static int test_transform_dc2x2(void) {
    for (int n=0;n<5000;n++) {
        Edge264Context ctx; Edge264Macroblock mbx; uint8_t pixels[16*16] __attribute__((aligned(16)));
        memset(&ctx,0,sizeof(ctx)); memset(&mbx,0,sizeof(mbx));
        ctx._mb=&mbx; ctx.t.stride[1]=16; ctx.t.samples_clip[1][0]=255;
        ctx.t.QP[1]=(int8_t)(rnd()%31); ctx.t.QP[2]=(int8_t)(rnd()%31); init_weights(&ctx);
        for (size_t i=0;i<sizeof(pixels);i++) pixels[i]=(uint8_t)rnd();
        ctx.samples_mb[1]=pixels;
        for (int v=0;v<2;v++) for (int i=0;i<4;i++) ctx.c_v[v][i]=(int32_t)(rnd()%2001)-1000;
        if (n & 1) mbx.f.CodedBlockPatternChromaAC = 1; /* retain transformed DC coefficients */
        transform_dc2x2(&ctx);
        add_digest(pixels,sizeof(pixels));
        add_digest(ctx.c_v,sizeof(ctx.c_v));
    }
    return 0;
}

int main(void) {
    uint32_t d_add, d_4x4, d_2x2;
    digest=0xffffffffu; test_add_dc4x4(); d_add=finish_digest();
    digest=0xffffffffu; test_transform_dc4x4(); d_4x4=finish_digest();
    digest=0xffffffffu; test_transform_dc2x2(); d_2x2=finish_digest();
    TEST_LOG("add_dc4x4=%08x transform_dc4x4=%08x transform_dc2x2=%08x\n",
           (unsigned)d_add, (unsigned)d_4x4, (unsigned)d_2x2);
    if (d_add != 0x374b68e5u || d_4x4 != 0x045237a9u ||
        d_2x2 != 0x6bd3a3c7u) {
        TEST_LOG("DC reconstruction reference CRC mismatch\n");
        return 1;
    }
    return 0;
}
