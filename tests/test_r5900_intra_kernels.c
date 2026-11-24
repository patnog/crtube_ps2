#include "../third_party/edge264/src/edge264_intra.c"
#include <stdio.h>
#ifdef CRTUBE_R5900_SMB_LOG
#include "../include/debug_log.h"
#define TEST_LOG(...) crtube_log(__VA_ARGS__)
#else
#define TEST_LOG(...) printf(__VA_ARGS__)
#endif
int main(void) {
    u8x16 a,b,c; for(int i=0;i<16;i++){a[i]=(uint8_t)(i*17);b[i]=(uint8_t)(255-i*11);c[i]=(uint8_t)(i*7);}
    u8x16 lp=lowpass8(a,b,c); for(int i=0;i<16;i++){unsigned e=((unsigned)a[i]+2u*b[i]+c[i]+2u)>>2;if(lp[i]!=(uint8_t)e)return 1;}
    i8x16 sh=spreadh8((i8x16)a), sq=spreadq8((i8x16)a);
    for(int i=0;i<8;i++)if((uint8_t)sh[i]!=a[i]||(uint8_t)sh[i+8]!=a[7])return 2;
    for(int i=0;i<4;i++)if((uint8_t)sq[i]!=a[i])return 3;
    for(int i=4;i<16;i++)if((uint8_t)sq[i]!=a[3])return 4;
    u16x8 sum=sumh8(a); unsigned total=0;for(int i=0;i<8;i++)total+=a[i];if(sum[0]!=total)return 5;
    i16x8 sums=sum8h8(a,b,c,(u8x16){0});
    for(int j=0;j<3;j++){u8x16 v=j==0?a:j==1?b:c;unsigned lo=0,hi=0;for(int i=0;i<8;i++){lo+=v[i];hi+=v[i+8];}if(sums[2*j]!=(int)lo||sums[2*j+1]!=(int)hi)return 6;}
    TEST_LOG("R5900 intra kernels: PASS\n"); return 0;
}
