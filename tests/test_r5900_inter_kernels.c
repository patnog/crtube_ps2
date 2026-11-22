#include "../third_party/edge264/src/edge264_inter.c"
#include <stdio.h>
int main(void) {
    u8x16 a,b,c,d,e,f;for(int i=0;i<16;i++){a[i]=i*13;b[i]=255-i*7;c[i]=i*9;d[i]=i*5;e[i]=255-i*3;f[i]=i*11;}
    i16x8 lo=sixtapVlo(a,b,c,d,e,f),hi=sixtapVhi(a,b,c,d,e,f);
    for(int i=0;i<8;i++){int x=(int)a[i]+f[i]-5*((int)b[i]+e[i])+20*((int)c[i]+d[i]);int y=(int)a[i+8]+f[i+8]-5*((int)b[i+8]+e[i+8])+20*((int)c[i+8]+d[i+8]);if(lo[i]!=x||hi[i]!=y)return 1;}
    i16x8 h=sixtapH8(a);for(int i=0;i<8;i++){int x=(int)a[i]-5*(int)a[i+1]+20*(int)a[i+2]+20*(int)a[i+3]-5*(int)a[i+4]+a[i+5];if(h[i]!=x)return 2;}
    u8x16 q,p; i16x8 wq,wp,o,wd;for(int i=0;i<16;i++){q[i]=i*15;p[i]=255-i*9;}for(int i=0;i<8;i++){wq[i]=i*3-8;wp[i]=9-i*2;o[i]=i*19-50;wd[i]=3;}
    u8x16 got=maddshrL(q,p,wq,wp,o,wd);for(int i=0;i<8;i++){int x=(int)q[i]*wq[i]+(int)p[i]*wp[i]+o[i];int y=(int)q[i+8]*wq[i]+(int)p[i+8]*wp[i]+o[i];x=mins(maxs(x,-32768),32767);y=mins(maxs(y,-32768),32767);x>>=wd[i];y>>=wd[i];if(got[i]!=(uint8_t)mins(maxs(x,0),255)||got[i+8]!=(uint8_t)mins(maxs(y,0),255))return 3;}
    puts("R5900 inter kernels: PASS");return 0;
}
