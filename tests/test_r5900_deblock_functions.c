#include "../third_party/edge264/src/edge264_deblock.c"
#include <stdio.h>
#include <string.h>

static uint32_t rng_state = 0x91e10da5u;
static uint32_t rnd(void) { rng_state ^= rng_state << 13; rng_state ^= rng_state >> 17; rng_state ^= rng_state << 5; return rng_state; }
static uint32_t crc32_update(uint32_t crc, const void *data, size_t n) {
    const uint8_t *p = (const uint8_t *)data;
    while (n--) { crc ^= *p++; for (int k=0;k<8;k++) crc = (crc >> 1) ^ (0xedb88320u & (uint32_t)-(int32_t)(crc & 1)); }
    return crc;
}
static uint32_t digest;
static void add_digest(const void *p, size_t n) { digest = crc32_update(digest, p, n); }
static void fill_vec(i8x16 *v) { for (int i=0;i<16;i++) (*v)[i]=(int8_t)(uint8_t)rnd(); }

static void run_luma(unsigned count) {
    for (unsigned n=0;n<count;n++) {
        i8x16 p2,p1,p0,q0,q1,q2;
        fill_vec(&p2); fill_vec(&p1); fill_vec(&p0);
        fill_vec(&q0); fill_vec(&q1); fill_vec(&q2);
        int alphaVal=(int)(rnd()%256), betaVal=(int)(rnd()%256);
        uint32_t tcBits = 0;
        for (int i=0;i<4;i++) tcBits |= (uint32_t)(uint8_t)((int)(rnd()%27)-1) << (i*8);
        int32_t tc = (int32_t)tcBits;
        DEBLOCK_LUMA_SOFT(p2,p1,p0,q0,q1,q2,alphaVal,betaVal,tc);
        add_digest(&p2,sizeof(p2)); add_digest(&p1,sizeof(p1)); add_digest(&p0,sizeof(p0));
        add_digest(&q0,sizeof(q0)); add_digest(&q1,sizeof(q1)); add_digest(&q2,sizeof(q2));
    }
}
static void run_chroma(unsigned count) {
    for (unsigned n=0;n<count;n++) {
        i8x16 p1,p0,q0,q1;
        fill_vec(&p1); fill_vec(&p0); fill_vec(&q0); fill_vec(&q1);
        int alphaVal=(int)(rnd()%256), betaVal=(int)(rnd()%256);
        uint64_t tcBits = 0;
        for (int i=0;i<8;i++) tcBits |= (uint64_t)(uint8_t)((int)(rnd()%27)-1) << (i*8);
        int64_t tc = (int64_t)tcBits;
        DEBLOCK_CHROMA_SOFT(p1,p0,q0,q1,alphaVal,betaVal,tc);
        add_digest(&p1,sizeof(p1)); add_digest(&p0,sizeof(p0));
        add_digest(&q0,sizeof(q0)); add_digest(&q1,sizeof(q1));
    }
}

static void run_luma_hard(unsigned count) {
    for (unsigned n=0;n<count;n++) {
        i8x16 p3,p2,p1,p0,q0,q1,q2,q3;
        fill_vec(&p3); fill_vec(&p2); fill_vec(&p1); fill_vec(&p0);
        fill_vec(&q0); fill_vec(&q1); fill_vec(&q2); fill_vec(&q3);
        int alphaVal=(int)(rnd()%256), betaVal=(int)(rnd()%256);
        DEBLOCK_LUMA_HARD(p3,p2,p1,p0,q0,q1,q2,q3,alphaVal,betaVal);
        add_digest(&p3,sizeof(p3)); add_digest(&p2,sizeof(p2));
        add_digest(&p1,sizeof(p1)); add_digest(&p0,sizeof(p0));
        add_digest(&q0,sizeof(q0)); add_digest(&q1,sizeof(q1));
        add_digest(&q2,sizeof(q2)); add_digest(&q3,sizeof(q3));
    }
}
static void run_chroma_hard(unsigned count) {
    for (unsigned n=0;n<count;n++) {
        i8x16 p1,p0,q0,q1;
        fill_vec(&p1); fill_vec(&p0); fill_vec(&q0); fill_vec(&q1);
        uint32_t alphaBits=0, betaBits=0;
        for (int i=0;i<4;i++) {
            alphaBits |= (uint32_t)(rnd()%256) << (i*8);
            betaBits |= (uint32_t)(rnd()%256) << (i*8);
        }
        DEBLOCK_CHROMA_HARD(p1,p0,q0,q1,(int32_t)alphaBits,(int32_t)betaBits);
        add_digest(&p1,sizeof(p1)); add_digest(&p0,sizeof(p0));
        add_digest(&q0,sizeof(q0)); add_digest(&q1,sizeof(q1));
    }
}

int main(void) {
    digest=0xffffffffu; run_luma(20000); uint32_t luma=digest^0xffffffffu;
    digest=0xffffffffu; run_chroma(20000); uint32_t chroma=digest^0xffffffffu;
    digest=0xffffffffu; run_luma_hard(20000); uint32_t luma_hard=digest^0xffffffffu;
    digest=0xffffffffu; run_chroma_hard(20000); uint32_t chroma_hard=digest^0xffffffffu;
    printf("deblock_luma=%08x deblock_chroma=%08x luma_hard=%08x chroma_hard=%08x\n",
           (unsigned)luma,(unsigned)chroma,(unsigned)luma_hard,(unsigned)chroma_hard);
    return 0;
}
