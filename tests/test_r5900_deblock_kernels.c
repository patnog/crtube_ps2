#include "../third_party/edge264/src/edge264_deblock.c"
#include <stdio.h>
#ifdef CRTUBE_R5900_SMB_LOG
#include "../include/debug_log.h"
#define TEST_LOG(...) crtube_log(__VA_ARGS__)
#else
#define TEST_LOG(...) printf(__VA_ARGS__)
#endif
int main(void){u8x16 a,b; i16x8 x,y,z,w;for(int i=0;i<16;i++){a[i]=(uint8_t)(i*19);b[i]=(uint8_t)(255-i*17);}u8x16 s=addsu8(a,b);for(int i=0;i<16;i++){unsigned v=(unsigned)a[i]+b[i];if(s[i]!=(uint8_t)(v>255?255:v))return 1;}for(int i=0;i<8;i++){x[i]=(int16_t)(i*9000-30000);y[i]=(int16_t)(i*7000-20000);z[i]=(int16_t)(i*5000-15000);w[i]=(int16_t)(i*3000-10000);}u8x16 d=packabd16(x,y,z,w);for(int i=0;i<8;i++){int p=x[i]-y[i],q=z[i]-w[i];if(d[i]!=(uint8_t)(p<0?-p:p)||d[i+8]!=(uint8_t)(q<0?-q:q))return 2;}TEST_LOG("R5900 deblock kernels: PASS\n");return 0;}
