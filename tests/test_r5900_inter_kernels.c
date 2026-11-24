#include "../third_party/edge264/src/edge264_inter.c"
#include <stdio.h>
#ifdef CRTUBE_R5900_SMB_LOG
#include "../include/debug_log.h"
#define TEST_LOG(...) crtube_log(__VA_ARGS__)
#else
#define TEST_LOG(...) printf(__VA_ARGS__)
#endif
int main(void) {
    u8x16 a,b,c,d,e,f;for(int i=0;i<16;i++){a[i]=i*13;b[i]=255-i*7;c[i]=i*9;d[i]=i*5;e[i]=255-i*3;f[i]=i*11;}
    i16x8 lo=sixtapVlo(a,b,c,d,e,f),hi=sixtapVhi(a,b,c,d,e,f);
    for(int i=0;i<8;i++){int x=(int)a[i]+f[i]-5*((int)b[i]+e[i])+20*((int)c[i]+d[i]);int y=(int)a[i+8]+f[i+8]-5*((int)b[i+8]+e[i+8])+20*((int)c[i+8]+d[i+8]);if(lo[i]!=x||hi[i]!=y)return 1;}
    i16x8 h=sixtapH8(a);for(int i=0;i<8;i++){int x=(int)a[i]-5*(int)a[i+1]+20*(int)a[i+2]+20*(int)a[i+3]-5*(int)a[i+4]+a[i+5];if(h[i]!=x)return 2;}
    u8x16 q,p; i16x8 wq,wp,o,wd;for(int i=0;i<16;i++){q[i]=i*15;p[i]=255-i*9;}for(int i=0;i<8;i++){wq[i]=i*3-8;wp[i]=9-i*2;o[i]=i*19-50;wd[i]=3;}
    u8x16 got=maddshrL(q,p,wq,wp,o,wd);for(int i=0;i<8;i++){int x=(int)q[i]*wq[i]+(int)p[i]*wp[i]+o[i];int y=(int)q[i+8]*wq[i]+(int)p[i+8]*wp[i]+o[i];x=mins(maxs(x,-32768),32767);y=mins(maxs(y,-32768),32767);x>>=wd[i];y>>=wd[i];if(got[i]!=(uint8_t)mins(maxs(x,0),255)||got[i+8]!=(uint8_t)mins(maxs(y,0),255))return 3;}
    /* The diagonal six-tap combiner must preserve the EE halfword wrap and
     * arithmetic-shift semantics for every lane. */
    {
        uint32_t seed = 0x91e10da5u;
        for (int n = 0; n < 20000; ++n) {
            i16x8 v[6], got;
            for (int k = 0; k < 6; ++k)
                for (int lane = 0; lane < 8; ++lane) {
                    seed = seed * 1664525u + 1013904223u;
                    v[k][lane] = (int16_t)((int)(seed >> 16) - 32768);
                }
            got = sixtapHV(v[0], v[1], v[2], v[3], v[4], v[5]);
            for (int lane = 0; lane < 8; ++lane) {
                int16_t af = (int16_t)((uint16_t)v[0][lane] + (uint16_t)v[5][lane]);
                int16_t be = (int16_t)((uint16_t)v[1][lane] + (uint16_t)v[4][lane]);
                int16_t cd = (int16_t)((uint16_t)v[2][lane] + (uint16_t)v[3][lane]);
                int16_t x = (int16_t)((uint16_t)af - (uint16_t)be);
                int16_t y = (int16_t)((uint16_t)cd - (uint16_t)be);
                x = (int16_t)(x >> 2);
                x = (int16_t)((uint16_t)x + (uint16_t)y);
                x = (int16_t)(x >> 2);
                x = (int16_t)((uint16_t)x + (uint16_t)cd);
                if (got[lane] != x) return 4;
            }
        }
    }
    TEST_LOG("R5900 inter kernels: PASS\n");return 0;
}
