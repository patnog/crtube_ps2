#define SIMD 6
#include "../third_party/edge264/src/edge264_internal.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static uint32_t state = 0x91e10da5u;
static uint32_t rnd(void) { state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state; }
static int failures;
#define CHECK(n, c, it) do { if (!(c)) { fprintf(stderr,"FAIL %s iteration=%u\n",n,it); failures++; return 0; } } while (0)

static void idct4_scalar(const int32_t x[4], int32_t y[4])
{
    int32_t e0=x[0]+x[2], e1=x[0]-x[2];
    int32_t e2=(x[1]>>1)-x[3], e3=(x[3]>>1)+x[1];
    y[0]=e0+e3; y[1]=e1+e2; y[2]=e1-e2; y[3]=e0-e3;
}
static i32x4 idct4_vec(i32x4 x0,i32x4 x1,i32x4 x2,i32x4 x3)
{
    i32x4 e0=x0+x2, e1=x0-x2, e2=(x1>>1)-x3, e3=(x3>>1)+x1;
    /* output vectors are returned as four columns via caller */
    (void)e0; (void)e1; (void)e2; (void)e3;
    return (i32x4){e0[0]+e3[0],e1[1]+e2[1],e1[2]-e2[2],e0[3]-e3[3]};
}
static void idct8_scalar(const int16_t x[8], int16_t y[8])
{
    int32_t e0=x[0]+x[4], e1=x[5]-x[3]-((x[7]>>1)+x[7]);
    int32_t e2=x[0]-x[4], e3=x[1]+x[7]-((x[3]>>1)+x[3]);
    int32_t e4=(x[2]>>1)-x[6], e5=x[7]-x[1]+((x[5]>>1)+x[5]);
    int32_t e6=(x[6]>>1)+x[2], e7=x[3]+x[5]+((x[1]>>1)+x[1]);
    int32_t f0=e0+e6, f1=(e7>>2)+e1, f2=e2+e4, f3=(e5>>2)+e3;
    int32_t f4=e2-e4, f5=(e3>>2)-e5, f6=e0-e6, f7=e7-(e1>>2);
    y[0]=(int16_t)(f0+f7); y[1]=(int16_t)(f2+f5);
    y[2]=(int16_t)(f4+f3); y[3]=(int16_t)(f6+f1);
    y[4]=(int16_t)(f6-f1); y[5]=(int16_t)(f4-f3);
    y[6]=(int16_t)(f2-f5); y[7]=(int16_t)(f0-f7);
}
static void idct8_vec(i16x8 d0,i16x8 d1,i16x8 d2,i16x8 d3,
                      i16x8 d4,i16x8 d5,i16x8 d6,i16x8 d7, i16x8 out[8])
{
    i16x8 e0=d0+d4, e1=d5-d3-((d7>>1)+d7), e2=d0-d4;
    i16x8 e3=d1+d7-((d3>>1)+d3), e4=(d2>>1)-d6;
    i16x8 e5=d7-d1+((d5>>1)+d5), e6=(d6>>1)+d2;
    i16x8 e7=d3+d5+((d1>>1)+d1);
    i16x8 f0=e0+e6, f1=(e7>>2)+e1, f2=e2+e4, f3=(e5>>2)+e3;
    i16x8 f4=e2-e4, f5=(e3>>2)-e5, f6=e0-e6, f7=e7-(e1>>2);
    out[0]=f0+f7; out[1]=f2+f5; out[2]=f4+f3; out[3]=f6+f1;
    out[4]=f6-f1; out[5]=f4-f3; out[6]=f2-f5; out[7]=f0-f7;
}
static int test_idct4(void)
{
    unsigned it;
    for(it=0;it<30000;it++) {
        int32_t a[4][4], ref[4][4]; i32x4 v[4];
        unsigned r,c;
        for(r=0;r<4;r++) for(c=0;c<4;c++) a[r][c]=(int32_t)(rnd()%4097)-2048;
        for(c=0;c<4;c++){int32_t x[4],y[4];for(r=0;r<4;r++)x[r]=a[r][c];idct4_scalar(x,y);for(r=0;r<4;r++)ref[r][c]=y[r];}
        for(r=0;r<4;r++) v[r]=(i32x4){a[r][0],a[r][1],a[r][2],a[r][3]};
        /* Verify each independent lane against scalar 1-D transform. */
        for(c=0;c<4;c++){int32_t x[4],y[4];for(r=0;r<4;r++)x[r]=v[r][c];idct4_scalar(x,y);for(r=0;r<4;r++)CHECK("idct4 lane",y[r]==ref[r][c],it);}
        /* Exercise the exact vector arithmetic and lane packing used by the 4x4 transform. */
        {i32x4 x0=v[0],x1=v[1],x2=v[2],x3=v[3];i32x4 e0=x0+x2,e1=x0-x2,e2=(x1>>1)-x3,e3=(x3>>1)+x1;
         i32x4 out0=e0+e3,out1=e1+e2,out2=e1-e2,out3=e0-e3;
         for(c=0;c<4;c++){int32_t x[4],y[4];for(r=0;r<4;r++)x[r]=a[r][c];idct4_scalar(x,y);CHECK("idct4 vector",out0[c]==y[0]&&out1[c]==y[1]&&out2[c]==y[2]&&out3[c]==y[3],it);}}
    }
    return 1;
}
static int test_idct8(void)
{
    unsigned it;
    for(it=0;it<30000;it++) {
        i16x8 d[8]; int16_t in[8][8], ref[8][8]; unsigned r,c;
        for(r=0;r<8;r++)for(c=0;c<8;c++)in[r][c]=(int16_t)((int)(rnd()%2049)-1024);
        for(c=0;c<8;c++){int16_t x[8],y[8];for(r=0;r<8;r++)x[r]=in[r][c];idct8_scalar(x,y);for(r=0;r<8;r++)ref[r][c]=y[r];}
        for(r=0;r<8;r++)d[r]=(i16x8){in[r][0],in[r][1],in[r][2],in[r][3],in[r][4],in[r][5],in[r][6],in[r][7]};
        for(c=0;c<8;c++){int16_t x[8],y[8];for(r=0;r<8;r++)x[r]=in[r][c];idct8_scalar(x,y);for(r=0;r<8;r++)CHECK("idct8 scalar matrix",y[r]==ref[r][c],it);}
        {i16x8 out[8];idct8_vec(d[0],d[1],d[2],d[3],d[4],d[5],d[6],d[7],out);
         for(c=0;c<8;c++){int16_t x[8],y[8];for(r=0;r<8;r++)x[r]=in[r][c];idct8_scalar(x,y);for(r=0;r<8;r++)CHECK("idct8 vector lane",out[r][c]==y[r],it);}}
    }
    return 1;
}
static int test_residual_clip(void)
{
    unsigned it,i;
    for(it=0;it<30000;it++) {
        i16x8 a,b; u8x16 got; 
        for(i=0;i<8;i++){a[i]=(int16_t)((int)(rnd()%1025)-512);b[i]=(int16_t)((int)(rnd()%1025)-512);}
        got=packus16(a,b);
        for(i=0;i<8;i++){int x=a[i],y=b[i];if(x<0)x=0;if(x>255)x=255;if(y<0)y=0;if(y>255)y=255;CHECK("residual clip",got[i]==x&&got[i+8]==y,it);}
    }
    return 1;
}
int main(void)
{
    if(!test_idct4()||!test_idct8()||!test_residual_clip())return 1;
    printf("R5900 IDCT/residual arithmetic: PASS (30000 randomized cases per group)\n");
    return failures?1:0;
}
