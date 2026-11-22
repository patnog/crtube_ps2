#include "../third_party/edge264/src/edge264_residual.c"
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static uint32_t rng_state = 0x6d2b79f5u;
static uint32_t rnd(void) { rng_state ^= rng_state << 13; rng_state ^= rng_state >> 17; rng_state ^= rng_state << 5; return rng_state; }
static uint32_t crc32_update(uint32_t crc, const uint8_t *p, size_t n) {
    size_t i; for (i=0;i<n;i++) { unsigned b; crc ^= p[i]; for (b=0;b<8;b++) crc=(crc>>1)^(0xedb88320u & (0u-(crc&1u))); } return crc;
}
static uint32_t digest4, digest8;

static void setup_common(Edge264Context *ctx, Edge264Macroblock *mbs, uint8_t *buf) {
    memset(ctx,0,sizeof(*ctx)); memset(mbs,0,sizeof(*mbs));
    ctx->_mb=mbs; mbs->mbIsInterFlag=0;
    ctx->t.stride[0]=32; ctx->t.samples_clip[0][0]=255;
    ctx->samples_mb[0]=buf+128;
}
static int test4(void) {
    unsigned it;
    for(it=0;it<4000;it++) {
        Edge264Context ctx; Edge264Macroblock mbs; uint8_t buf[512]; unsigned i;
        setup_common(&ctx,&mbs,buf); memset(buf,0x5a,sizeof(buf));
        ctx.t.QP[0]=(int8_t)(rnd()%36);
        for(i=0;i<16;i++) ctx.t.pps.weightScale4x4[0][i]=(uint8_t)(1+(rnd()%64));
        for(i=0;i<16;i++) ctx.c[i]=(int32_t)((int)(rnd()%2049)-1024);
        add_idct4x4(&ctx,0,-1,ctx.samples_mb[0]);
        digest4=crc32_update(digest4,buf,sizeof(buf));
        for(i=0;i<16;i++) if(ctx.c[i]!=0) { fprintf(stderr,"4x4 coefficients not cleared iter=%u\n",it); return 0; }
    }
    return 1;
}
static int test8(void) {
    unsigned it;
    for(it=0;it<4000;it++) {
        Edge264Context ctx; Edge264Macroblock mbs; uint8_t buf[512]; unsigned i;
        setup_common(&ctx,&mbs,buf); memset(buf,0xa5,sizeof(buf));
        ctx.t.QP[0]=(int8_t)(rnd()%36);
        for(i=0;i<64;i++) ctx.t.pps.weightScale8x8[0][i]=(uint8_t)(1+(rnd()%64));
        for(i=0;i<64;i++) ctx.c[i]=(int32_t)((int)(rnd()%513)-256);
        add_idct8x8(&ctx,0,ctx.samples_mb[0]);
        digest8=crc32_update(digest8,buf,sizeof(buf));
        for(i=0;i<64;i++) if(ctx.c[i]!=0) { fprintf(stderr,"8x8 coefficients not cleared iter=%u\n",it); return 0; }
    }
    return 1;
}
int main(void) {
    digest4=digest8=0xffffffffu;
    if(!test4()||!test8()) return 1;
    printf("idct4=%08x idct8=%08x\n",(unsigned)(digest4^0xffffffffu),(unsigned)(digest8^0xffffffffu));
    return 0;
}
