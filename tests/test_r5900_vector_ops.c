#define SIMD 6
#include "../third_party/edge264/src/edge264_internal.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static unsigned failures;
#define CHECK(name, expr) do { if (!(expr)) { \
    fprintf(stderr, "FAIL: %s (iteration %u)\n", name, iteration); \
    ++failures; return 0; } } while (0)

static uint32_t rng_state = 0x6d2b79f5u;
static uint32_t rnd(void) { rng_state ^= rng_state << 13; rng_state ^= rng_state >> 17; rng_state ^= rng_state << 5; return rng_state; }

static int test_ops(void)
{
    unsigned iteration;
    for (iteration = 0; iteration < 20000; ++iteration) {
        i8x16 a, b, mask, r8;
        u8x16 ua, ub, ur;
        i16x8 x, y, rx;
        u16x8 ux, uy, urx;
        i32x4 p, q;
        unsigned i;
        for (i=0;i<16;i++) { a[i]=(int8_t)rnd(); b[i]=(int8_t)rnd(); mask[i]=(int8_t)rnd(); ua[i]=(uint8_t)rnd(); ub[i]=(uint8_t)rnd(); }
        for (i=0;i<8;i++) { x[i]=(int16_t)rnd(); y[i]=(int16_t)rnd(); ux[i]=(uint16_t)rnd(); uy[i]=(uint16_t)rnd(); }
        for (i=0;i<4;i++) { p[i]=(int32_t)rnd(); q[i]=(int32_t)rnd(); }

        r8 = ifelse_mask(mask,a,b);
        for(i=0;i<16;i++) CHECK("ifelse_mask", (uint8_t)r8[i] == (((uint8_t)a[i] & (uint8_t)mask[i]) | ((uint8_t)b[i] & (uint8_t)~mask[i])));
        r8 = ifelse_msb(mask,a,b);
        for(i=0;i<16;i++) CHECK("ifelse_msb", r8[i] == (mask[i] < 0 ? a[i] : b[i]));

        ur = subsu8(ua,ub); for(i=0;i<16;i++) CHECK("subsu8",ur[i]==(ua[i]>ub[i]?ua[i]-ub[i]:0));
        { u16x8 sum=sum8(ua); unsigned total=0; for(i=0;i<16;i++)total+=ua[i]; CHECK("sum8",sum[0]==(uint16_t)total); }
        { unsigned bits=movemask(a), expected=0; for(i=0;i<16;i++)if(a[i]<0)expected|=1u<<i; CHECK("movemask",bits==expected); }
        { u8x16 av=abs8(a); for(i=0;i<16;i++)CHECK("abs8",av[i]==(uint8_t)(a[i]<0?-(int)a[i]:a[i])); }
        { u16x8 av=abs16(x); for(i=0;i<8;i++)CHECK("abs16",av[i]==(uint16_t)(x[i]<0?-(int)x[i]:x[i])); }
        ur = avgu8(ua,ub);
        for(i=0;i<16;i++) CHECK("avgu8", ur[i] == ((unsigned)ua[i]+ub[i]+1u)/2u);
        ur = minu8(ua,ub); for(i=0;i<16;i++) CHECK("minu8", ur[i] == (ua[i]<ub[i]?ua[i]:ub[i]));
        ur = maxu8(ua,ub); for(i=0;i<16;i++) CHECK("maxu8", ur[i] == (ua[i]>ub[i]?ua[i]:ub[i]));
        r8 = min8(a,b); for(i=0;i<16;i++) CHECK("min8", r8[i] == (a[i]<b[i]?a[i]:b[i]));
        r8 = max8(a,b); for(i=0;i<16;i++) CHECK("max8", r8[i] == (a[i]>b[i]?a[i]:b[i]));

        rx = adds16(x,y);
        for(i=0;i<8;i++) { int32_t v=(int32_t)x[i]+y[i]; if(v>32767)v=32767; if(v< -32768)v=-32768; CHECK("adds16",rx[i]==v); }
        rx = min16(x,y); for(i=0;i<8;i++) CHECK("min16",rx[i]==(x[i]<y[i]?x[i]:y[i]));
        rx = max16(x,y); for(i=0;i<8;i++) CHECK("max16",rx[i]==(x[i]>y[i]?x[i]:y[i]));
        urx = maddubx(ua,ub);
        for(i=0;i<8;i++) CHECK("maddubx",urx[i]==(uint16_t)((unsigned)ua[2*i]*ub[2*i]+(unsigned)ua[2*i+1]*ub[2*i+1]));
        { i16x8 got=maddubs(ua,b); for(i=0;i<8;i++){int v=(int)ua[2*i]*b[2*i]+(int)ua[2*i+1]*b[2*i+1];if(v>32767)v=32767;if(v< -32768)v=-32768;CHECK("maddubs",got[i]==v);} }

        { uint8_t bytes[8]; uint64_t got=shrlos16(x,3); for(i=0;i<8;i++)bytes[i]=(uint8_t)(x[i]>>3); CHECK("shrlos16",memcmp(&got,bytes,8)==0); }
        { u16x8 lo=cvtlo8u16(ua),hi=cvthi8u16(ua); for(i=0;i<8;i++)CHECK("cvt8u16",lo[i]==ua[i] && hi[i]==ua[i+8]); }
        { i16x8 lo=cvtlo8s16(a),hi=cvthi8s16(a); for(i=0;i<8;i++)CHECK("cvt8s16",lo[i]==a[i] && hi[i]==a[i+8]); }
        { i8x16 packed=packs16(x,y); for(i=0;i<8;i++){int v=x[i],w=y[i];if(v>127)v=127;if(v< -128)v=-128;if(w>127)w=127;if(w< -128)w=-128;CHECK("packs16",packed[i]==v && packed[i+8]==w);} }
        { i16x8 packed=packs32(p,q); for(i=0;i<4;i++){int64_t v=p[i],w=q[i];if(v>32767)v=32767;if(v< -32768)v=-32768;if(w>32767)w=32767;if(w< -32768)w=-32768;CHECK("packs32",packed[i]==v && packed[i+4]==w);} }
        { u8x16 packed=packus16(x,y); for(i=0;i<8;i++){int v=x[i],w=y[i];if(v>255)v=255;if(v<0)v=0;if(w>255)w=255;if(w<0)w=0;CHECK("packus16",packed[i]==v && packed[i+8]==w);} }
        { i16x8 got=shrrs16(x,3); for(i=0;i<8;i++){int16_t sum=(int16_t)((uint16_t)x[i]+4u);CHECK("shrrs16",got[i]==(sum >> 3));} }
        { u16x8 got=shrru16(ux,3); for(i=0;i<8;i++) CHECK("shrru16",got[i]==(((unsigned)ux[i]+4u) >> 3)); }
        { u8x16 got=shrpus16(x,y,3); for(i=0;i<8;i++){int v=x[i]>>3,w=y[i]>>3;if(v<0)v=0;if(v>255)v=255;if(w<0)w=0;if(w>255)w=255;CHECK("sh rpus16",got[i]==v && got[i+8]==w);} }
        { u8x16 got=shrrpu16(ux,uy,3); for(i=0;i<8;i++){unsigned v=((unsigned)ux[i]+4u)>>3,w=((unsigned)uy[i]+4u)>>3;if(v>255)v=255;if(w>255)w=255;CHECK("shrrpu16",got[i]==v && got[i+8]==w);} }
    }
    return 1;
}


static int test_permutations(void)
{
    i8x16 a,b,c,d,m,r;
    i8x16 blocks[4];
    unsigned i,n;
    for(i=0;i<16;i++){a[i]=(int8_t)(i+1);b[i]=(int8_t)(i+33);c[i]=(int8_t)(i+65);d[i]=(int8_t)(i+97);}
    blocks[0]=a; blocks[1]=b; blocks[2]=c; blocks[3]=d;
    for(n=0;n<16;n++) {
        r=shl128(a,n); for(i=0;i<16;i++) if(r[i]!=(i>=n?a[i-n]:0)) return 0;
        r=shr128(a,n); for(i=0;i<16;i++) if(r[i]!=(i+n<16?a[i+n]:0)) return 0;
        r=shlc128(a,n); for(i=0;i<16;i++) if(r[i]!=a[i<n?0:i-n]) return 0;
        r=shrc128(a,n); for(i=0;i<16;i++) if(r[i]!=a[i+n>15?15:i+n]) return 0;
        r=shlv128(a,n); for(i=0;i<16;i++) if(r[i]!=(i>=n?a[i-n]:0)) return 0;
        r=shrv128(a,n); for(i=0;i<16;i++) if(r[i]!=(i+n<16?a[i+n]:0)) return 0;
        r=shrd128(a,b,n); for(i=0;i<16;i++) if(r[i]!=(i+n<16?a[i+n]:b[i+n-16])) return 0;
    }
    for(i=0;i<16;i++) m[i]=(int8_t)(15-i);
    r=shuffle(a,m); for(i=0;i<16;i++) if(r[i]!=a[15-i]) return 0;
    for(i=0;i<16;i++) m[i]=(int8_t)(i+16);
    r=shuffle2(a,b,m); for(i=0;i<16;i++) if(r[i]!=b[i]) return 0;
    for(i=0;i<16;i++) m[i]=(int8_t)(i+32);
    r=shuffle3(blocks,m); for(i=0;i<16;i++) if(r[i]!=c[i]) return 0;
    for(i=0;i<16;i++) m[i]=(int8_t)(i+48);
    r=shuffle4z(blocks,m); for(i=0;i<16;i++) if(r[i]!=d[i]) return 0;
    for(i=0;i<16;i++) m[i]=(int8_t)i;
    r=ziplo8(a,b); for(i=0;i<8;i++) if(r[2*i]!=a[i]||r[2*i+1]!=b[i]) return 0;
    r=ziphi8(a,b); for(i=0;i<8;i++) if(r[2*i]!=a[i+8]||r[2*i+1]!=b[i+8]) return 0;
    return 1;
}

int main(void)
{
    if (!test_ops()) return 1;
    if (!test_permutations()) { puts("R5900 vector permutation tests: FAIL"); return 1; }
    printf("R5900 vector operation tests: PASS (20000 randomized vectors)\n");
    return failures ? 1 : 0;
}
