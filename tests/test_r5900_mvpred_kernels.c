#include "../third_party/edge264/src/edge264_mvpred.c"
#include <stdio.h>
#ifdef CRTUBE_R5900_SMB_LOG
#include "../include/debug_log.h"
#define TEST_LOG(...) crtube_log(__VA_ARGS__)
#else
#define TEST_LOG(...) printf(__VA_ARGS__)
#endif
int main(void){i16x8 mv={-32768,-30000,-257,-1,0,1,257,32767};int factors[]={-32768,-16384,-1,0,1,16384,32767};for(unsigned k=0;k<sizeof(factors)/sizeof(factors[0]);k++){i16x8 got=temporal_scale(mv,(int16_t)factors[k]);for(int i=0;i<8;i++){int32_t v=(int32_t)mv[i]*factors[k]+128;int16_t e=(int16_t)(v>>8);if(got[i]!=e)return 1;}}TEST_LOG("R5900 motion-vector temporal scaling: PASS\n");return 0;}
