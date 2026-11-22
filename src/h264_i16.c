#include "h264_decoder.h"

#include <limits.h>
#include <string.h>

typedef struct i16_reader {
    const uint8_t *data;
    size_t size;
    size_t bit;
} i16_reader_t;

static int get_bit(i16_reader_t *r, unsigned *v)
{
    if (!r || !v || r->bit >= r->size * 8u) return -1;
    *v = (r->data[r->bit >> 3] >> (7u - (r->bit & 7u))) & 1u;
    ++r->bit;
    return 0;
}

static int get_ue(i16_reader_t *r, unsigned *v)
{
    unsigned zeros = 0, count, bit, suffix = 0;
    if (!r || !v) return -1;
    do {
        if (get_bit(r, &bit) < 0) return -1;
        if (!bit && ++zeros > 31u) return -1;
    } while (!bit);
    count = zeros;
    while (count--) {
        if (get_bit(r, &bit) < 0) return -1;
        suffix = (suffix << 1) | bit;
    }
    *v = ((1u << zeros) - 1u) + suffix;
    return 0;
}

static int get_se(i16_reader_t *r, int *v)
{
    unsigned code;
    if (get_ue(r, &code) < 0) return -1;
    *v = (code & 1u) ? (int)((code + 1u) >> 1) : -(int)(code >> 1);
    return 0;
}

int crtube_h264_parse_i16x16_mb(
    const uint8_t *rbsp, size_t rbsp_size, size_t *bit_offset,
    unsigned current_qp, const uint8_t left_nz[4],
    const uint8_t top_nz[4], unsigned have_left, unsigned have_top,
    const uint8_t left_cb[2], const uint8_t top_cb[2],
    const uint8_t left_cr[2], const uint8_t top_cr[2],
    crtube_h264_i16x16_mb_data_t *out)
{
    static const uint8_t bx[16] = {0,1,0,1,2,3,2,3,0,1,0,1,2,3,2,3};
    static const uint8_t by[16] = {0,0,1,1,0,0,1,1,2,2,3,3,2,2,3,3};
    i16_reader_t r;
    crtube_h264_i16x16_mb_data_t m;
    unsigned mb_type, i, chroma_mode, cbp_luma, cbp_chroma;
    unsigned total;
    int qp_delta;
    size_t pos;

    if (!rbsp || !rbsp_size || !bit_offset || rbsp_size > SIZE_MAX / 8u || *bit_offset > rbsp_size * 8u ||
        current_qp > 51u || !out || have_left > 1u || have_top > 1u ||
        (have_left && (!left_nz || !left_cb || !left_cr)) ||
        (have_top && (!top_nz || !top_cb || !top_cr))) return -1;
    r.data = rbsp; r.size = rbsp_size; r.bit = *bit_offset;
    memset(&m, 0, sizeof(m));
    if (get_ue(&r, &mb_type) < 0 || mb_type < 1u || mb_type > 24u)
        return -2;
    i = mb_type - 1u;
    m.pred_mode = i & 3u;
    cbp_chroma = (i >> 2) % 3u;
    cbp_luma = (i / 12u) ? 15u : 0u;
    m.coded_block_pattern = cbp_luma | (cbp_chroma << 4);
    if (get_ue(&r, &chroma_mode) < 0 || chroma_mode > 3u ||
        get_se(&r, &qp_delta) < 0 || qp_delta < -26 || qp_delta > 25)
        return -1;
    m.chroma_pred_mode = chroma_mode;
    m.qp_y = (unsigned)(((int)current_qp + qp_delta + 52) % 52);

    /* Intra16 luma DC block uses nC=0 and contains all sixteen coefficients. */
    if (crtube_h264_cavlc_decode_block(rbsp, rbsp_size, &r.bit, 0, 16u,
                                       m.luma_dc, &total) < 0)
        return -1;
    m.luma_dc_total = (uint8_t)total;

    for (i = 0; i < 16u; ++i) {
        unsigned x = bx[i], y = by[i];
        int a = -1, b = -1, nc;
        if (cbp_luma) {
            if (x) {
                unsigned j;
                for (j = 0; j < 16u; ++j)
                    if (bx[j] == x - 1u && by[j] == y) { a = m.luma_ac_total[j]; break; }
            } else if (have_left) a = left_nz[y];
            if (y) {
                unsigned j;
                for (j = 0; j < 16u; ++j)
                    if (bx[j] == x && by[j] == y - 1u) { b = m.luma_ac_total[j]; break; }
            } else if (have_top) b = top_nz[x];
            nc = (a >= 0 && b >= 0) ? ((a + b + 1) >> 1) :
                 (a >= 0 ? a : (b >= 0 ? b : 0));
            {
                int16_t coeffs[16];
                if (crtube_h264_cavlc_decode_block(rbsp, rbsp_size, &r.bit,
                        nc, 15u, coeffs, &total) < 0) return -1;
                memcpy(m.luma_ac[i], coeffs, sizeof(m.luma_ac[i]));
            }
            m.luma_ac_total[i] = (uint8_t)total;
        }
    }

    if (cbp_chroma) {
        for (i = 0; i < 2u; ++i) {
            int16_t coeffs[16];
            if (crtube_h264_cavlc_decode_block(rbsp, rbsp_size, &r.bit,
                    -1, 4u, coeffs, &total) < 0) return -1;
            memcpy(m.chroma_dc[i], coeffs, sizeof(m.chroma_dc[i]));
            m.chroma_dc_total[i] = (uint8_t)total;
        }
    }
    if (cbp_chroma == 2u) {
        unsigned plane, block;
        for (plane = 0; plane < 2u; ++plane) {
            for (block = 0; block < 4u; ++block) {
                unsigned x = block & 1u, y = block >> 1;
                int a = -1, b = -1, nc;
                if (x) a = m.chroma_ac_total[plane][block - 1u];
                else if (have_left) a = (plane ? left_cr : left_cb)[y];
                if (y) b = m.chroma_ac_total[plane][block - 2u];
                else if (have_top) b = (plane ? top_cr : top_cb)[x];
                nc = (a >= 0 && b >= 0) ? ((a + b + 1) >> 1) :
                     (a >= 0 ? a : (b >= 0 ? b : 0));
                {
                    int16_t coeffs[16];
                    if (crtube_h264_cavlc_decode_block(rbsp, rbsp_size, &r.bit,
                            nc, 15u, coeffs, &total) < 0) return -1;
                    memcpy(m.chroma_ac[plane][block], coeffs,
                           sizeof(m.chroma_ac[plane][block]));
                }
                m.chroma_ac_total[plane][block] = (uint8_t)total;
            }
        }
    }
    pos = r.bit;
    *out = m;
    *bit_offset = pos;
    return 0;
}

static int clip8(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

static int predict16(unsigned mode, const uint8_t top[16],
                     const uint8_t left[16], uint8_t tl,
                     unsigned ht, unsigned hl, uint8_t out[256])
{
    unsigned x, y;
    if (mode > 3u || !out || (ht && !top) || (hl && !left)) return -1;
    if (mode == 0u) {
        if (!ht) return -2;
        for (y=0;y<16;y++) for(x=0;x<16;x++) out[y*16+x]=top[x];
    } else if (mode == 1u) {
        if (!hl) return -2;
        for (y=0;y<16;y++) for(x=0;x<16;x++) out[y*16+x]=left[y];
    } else if (mode == 2u) {
        unsigned sum=0, n=0;
        if (ht) { for(x=0;x<16;x++) sum+=top[x]; n+=16; }
        if (hl) { for(y=0;y<16;y++) sum+=left[y]; n+=16; }
        { uint8_t dc=n?(uint8_t)((sum+n/2u)/n):128u;
          memset(out,dc,256); }
    } else {
        int h=0,v=0,a,b,c;
        if (!ht || !hl) return -2;
        for(x=0;x<8;x++) {
            int tminus = x==7 ? (int)tl : (int)top[6u-x];
            int lminus = x==7 ? (int)tl : (int)left[6u-x];
            h += (int)(x+1u)*((int)top[8u+x]-tminus);
            v += (int)(x+1u)*((int)left[8u+x]-lminus);
        }
        a=16*((int)top[15]+(int)left[15]);
        b=(5*h+32)>>6; c=(5*v+32)>>6;
        for(y=0;y<16;y++) for(x=0;x<16;x++)
            out[y*16+x]=(uint8_t)clip8((a+b*((int)x-7)+c*((int)y-7)+16)>>5);
    }
    return 0;
}

static int inverse_luma_dc(const int16_t scan[16], unsigned qp, int16_t dc[16])
{
    static const uint8_t dequant[6]={10,11,13,14,16,18};
    static const uint8_t zigzag[16]={0,1,4,8,5,2,3,6,9,12,13,10,7,11,14,15};
    int32_t in[16], tmp[16], had[16];
    unsigned i, x, y, qdiv=qp/6u, qrem=qp%6u;
    for(i=0;i<16;i++) in[zigzag[i]]=scan[i];
    for(y=0;y<4;y++) {
        int32_t s0=in[y*4]+in[y*4+1], s1=in[y*4]-in[y*4+1];
        int32_t s2=in[y*4+2]+in[y*4+3], s3=in[y*4+2]-in[y*4+3];
        tmp[y*4]=s0+s2; tmp[y*4+1]=s1+s3;
        tmp[y*4+2]=s0-s2; tmp[y*4+3]=s1-s3;
    }
    for(x=0;x<4;x++) {
        int32_t s0=tmp[x]+tmp[4+x], s1=tmp[x]-tmp[4+x];
        int32_t s2=tmp[8+x]+tmp[12+x], s3=tmp[8+x]-tmp[12+x];
        had[x]=s0+s2; had[4+x]=s1+s3;
        had[8+x]=s0-s2; had[12+x]=s1-s3;
    }
    for(i=0;i<16;i++) {
        int64_t v=(int64_t)had[i]*dequant[qrem]*16;
        if(qdiv>=4u) v <<= qdiv-4u;
        else v=(v+((int64_t)1<<(3u-qdiv)))>>(4u-qdiv);
        if(v<INT16_MIN || v>INT16_MAX) return -1;
        dc[i]=(int16_t)v;
    }
    return 0;
}

int crtube_h264_reconstruct_i16x16_mb_to_frame(
    crtube_h264_frame_t *frame, const crtube_h264_i16x16_mb_data_t *mb,
    int chroma_qp_index_offset)
{
    static const uint8_t zigzag[16]={0,1,4,8,5,2,3,6,9,12,13,10,7,11,14,15};
    uint8_t top[16], left[16], pred[256], luma[256], cb[64], cr[64];
    uint8_t topcb[8], leftcb[8], topcr[8], leftcr[8];
    uint8_t tl=128, tlcb=128, tlcr=128;
    int16_t dc[16];
    unsigned mbw, mbx, mby, ht, hl, x, y, plane, qp_c;
    size_t ys, cs;
    if(!frame||!frame->y||!frame->cb||!frame->cr||!mb||mb->qp_y>51u||
       mb->pred_mode>3u||chroma_qp_index_offset < -12||chroma_qp_index_offset>12)
        return -1;
    if (!frame->width || !frame->height || (frame->width & 15u) ||
        (frame->height & 15u)) return -1;
    mbw=frame->width/16u;
    if(!mbw || mb->mb_address>=mbw*(frame->height/16u)) return -1;
    mbx=mb->mb_address%mbw; mby=mb->mb_address/mbw;
    ht=mby!=0u; hl=mbx!=0u; ys=frame->width; cs=frame->chroma_width;
    for(x=0;x<16;x++) top[x]=ht?frame->y[(size_t)(mby*16u-1u)*ys+mbx*16u+x]:128u;
    for(y=0;y<16;y++) left[y]=hl?frame->y[(size_t)(mby*16u+y)*ys+mbx*16u-1u]:128u;
    if(ht&&hl) tl=frame->y[(size_t)(mby*16u-1u)*ys+mbx*16u-1u];
    if(predict16(mb->pred_mode,top,left,tl,ht,hl,pred)<0) return -2;
    if(inverse_luma_dc(mb->luma_dc,mb->qp_y,dc)<0) return -1;
    {
        static const uint8_t bx[16]={0,1,0,1,2,3,2,3,0,1,0,1,2,3,2,3};
        static const uint8_t by[16]={0,0,1,1,0,0,1,1,2,2,3,3,2,2,3,3};
        unsigned bi;
        for(bi=0;bi<16u;bi++) {
            int16_t raster[16]={0}, coeff[16], residual[16];
            uint8_t p[16], out[16];
            unsigned k, xx, yy, ox=(unsigned)bx[bi]*4u, oy=(unsigned)by[bi]*4u;
            if(mb->coded_block_pattern&15u)
                for(k=0;k<15u;k++) raster[zigzag[k+1u]]=mb->luma_ac[bi][k];
            if(crtube_h264_inverse_quant4x4(raster,mb->qp_y,NULL,coeff)<0) return -1;
            coeff[0]=dc[bi];
            if(crtube_h264_inverse_transform4x4(coeff,residual)<0) return -1;
            for(yy=0;yy<4u;yy++) for(xx=0;xx<4u;xx++)
                p[yy*4u+xx]=pred[(oy+yy)*16u+ox+xx];
            crtube_h264_add_residual4x4(p,residual,out);
            for(yy=0;yy<4u;yy++) for(xx=0;xx<4u;xx++)
                luma[(oy+yy)*16u+ox+xx]=out[yy*4u+xx];
        }
    }
    for(x=0;x<8;x++) {
        topcb[x]=ht?frame->cb[(size_t)(mby*8u-1u)*cs+mbx*8u+x]:128u;
        topcr[x]=ht?frame->cr[(size_t)(mby*8u-1u)*cs+mbx*8u+x]:128u;
    }
    for(y=0;y<8;y++) {
        leftcb[y]=hl?frame->cb[(size_t)(mby*8u+y)*cs+mbx*8u-1u]:128u;
        leftcr[y]=hl?frame->cr[(size_t)(mby*8u+y)*cs+mbx*8u-1u]:128u;
    }
    if(ht&&hl) { size_t p=(size_t)(mby*8u-1u)*cs+mbx*8u-1u; tlcb=frame->cb[p]; tlcr=frame->cr[p]; }
    qp_c=crtube_h264_chroma_qp(mb->qp_y,chroma_qp_index_offset);
    for(plane=0;plane<2;plane++) {
        const int16_t (*ac)[15]=mb->chroma_ac[plane];
        if(crtube_h264_reconstruct_chroma8x8(mb->chroma_pred_mode,
            plane?topcr:topcb,plane?leftcr:leftcb,plane?tlcr:tlcb,
            ht,hl,ht&&hl,mb->chroma_dc[plane],ac,qp_c,plane?cr:cb)<0) return -2;
    }
    return crtube_h264_frame_put_macroblock(frame,mbx,mby,luma,cb,cr);
}

static int i16_only_trailing(const uint8_t *data, size_t size, size_t bit)
{
    size_t p=bit;
    unsigned v;
    if (p >= size*8u) return 0;
    v=(data[p>>3]>>(7u-(p&7u)))&1u;
    if (!v) return 0;
    ++p;
    while (p<size*8u) {
        v=(data[p>>3]>>(7u-(p&7u)))&1u;
        if (v) return 0;
        ++p;
    }
    return 1;
}

static int i16_peek_mb_type(const uint8_t *rbsp, size_t size,
                            size_t bit_offset, unsigned *mb_type)
{
    i16_reader_t r;
    r.data=rbsp; r.size=size; r.bit=bit_offset;
    return get_ue(&r,mb_type);
}

int crtube_h264_decode_i_slice_to_frame(
    const uint8_t *rbsp, size_t rbsp_size, size_t bit_offset,
    crtube_h264_frame_t *frame, unsigned first_mb_in_slice,
    unsigned current_qp, unsigned transform_8x8_mode,
    int chroma_qp_index_offset)
{
    uint8_t top_nz[30][4], top_cb[30][2], top_cr[30][2];
    uint8_t top_modes[30][4], left_nz[4], left_cb[2], left_cr[2];
    uint8_t left_modes[4];
    unsigned mbw, mbh, total, address, count=0;
    if (!rbsp || !rbsp_size || !frame || !frame->storage ||
        !frame->width || !frame->height || (frame->width&15u) ||
        (frame->height&15u) || current_qp>51u ||
        transform_8x8_mode>1u || chroma_qp_index_offset < -12 ||
        chroma_qp_index_offset > 12)
        return -1;
    mbw=frame->width/16u; mbh=frame->height/16u; total=mbw*mbh;
    if (!mbw || mbw>30u || mbh>17u || first_mb_in_slice>=total ||
        transform_8x8_mode) return -2;
    memset(top_nz,0,sizeof(top_nz)); memset(top_cb,0,sizeof(top_cb));
    memset(top_cr,0,sizeof(top_cr)); memset(top_modes,2,sizeof(top_modes));
    memset(left_nz,0,sizeof(left_nz)); memset(left_cb,0,sizeof(left_cb));
    memset(left_cr,0,sizeof(left_cr)); memset(left_modes,2,sizeof(left_modes));
    address=first_mb_in_slice;
    while (address<total) {
        unsigned mx=address%mbw, my=address/mbw;
        /* Neighbors before first_mb_in_slice belong to another slice and
           are unavailable, even when they are inside the picture. */
        unsigned hl=mx!=0u && address-1u>=first_mb_in_slice;
        unsigned ht=my!=0u && address-mbw>=first_mb_in_slice;
        unsigned mb_type;
        int rc;
        if (i16_only_trailing(rbsp,rbsp_size,bit_offset)) break;
        if (!hl) {
            memset(left_nz,0,sizeof(left_nz)); memset(left_cb,0,sizeof(left_cb));
            memset(left_cr,0,sizeof(left_cr)); memset(left_modes,2,sizeof(left_modes));
        }
        if (i16_peek_mb_type(rbsp,rbsp_size,bit_offset,&mb_type)<0) return -1;
        if (mb_type>=1u && mb_type<=24u) {
            crtube_h264_i16x16_mb_data_t mb;
            rc=crtube_h264_parse_i16x16_mb(rbsp,rbsp_size,&bit_offset,
                current_qp,hl?left_nz:NULL,ht?top_nz[mx]:NULL,hl,ht,
                hl?left_cb:NULL,ht?top_cb[mx]:NULL,
                hl?left_cr:NULL,ht?top_cr[mx]:NULL,&mb);
            if (rc<0) return rc;
            mb.mb_address=address;
            if (crtube_h264_reconstruct_i16x16_mb_to_frame(frame,&mb,
                    chroma_qp_index_offset)<0) return -2;
            left_nz[0]=mb.luma_ac_total[5]; left_nz[1]=mb.luma_ac_total[7];
            left_nz[2]=mb.luma_ac_total[13]; left_nz[3]=mb.luma_ac_total[15];
            top_nz[mx][0]=mb.luma_ac_total[10]; top_nz[mx][1]=mb.luma_ac_total[11];
            top_nz[mx][2]=mb.luma_ac_total[14]; top_nz[mx][3]=mb.luma_ac_total[15];
            left_cb[0]=mb.chroma_ac_total[0][1]; left_cb[1]=mb.chroma_ac_total[0][3];
            left_cr[0]=mb.chroma_ac_total[1][1]; left_cr[1]=mb.chroma_ac_total[1][3];
            top_cb[mx][0]=mb.chroma_ac_total[0][2]; top_cb[mx][1]=mb.chroma_ac_total[0][3];
            top_cr[mx][0]=mb.chroma_ac_total[1][2]; top_cr[mx][1]=mb.chroma_ac_total[1][3];
            memset(left_modes,2,sizeof(left_modes));
            memset(top_modes[mx],2,sizeof(top_modes[mx]));
            current_qp=mb.qp_y;
        } else if (mb_type==0u) {
            crtube_h264_intra4x4_mb_header_t h;
            crtube_h264_i4x4_mb_data_t mb;
            int16_t chroma_dc[2][4];
            int16_t chroma_ac[2][4][15];
            uint8_t chroma_dc_total[2], chroma_ac_total[2][4];
            rc=crtube_h264_parse_intra4x4_mb_header(rbsp,rbsp_size,&bit_offset,
                hl?left_modes:NULL,ht?top_modes[mx]:NULL,hl,ht,
                transform_8x8_mode,current_qp,&h);
            if (rc<0) return rc;
            memset(&mb,0,sizeof(mb)); mb.mb_address=address;
            mb.slice_have_top=(uint8_t)ht;
            mb.slice_have_left=(uint8_t)hl;
            mb.slice_have_top_right=(uint8_t)(ht && mx + 1u < mbw &&
                address - mbw + 1u >= first_mb_in_slice);
            memcpy(mb.modes,h.modes,sizeof(mb.modes));
            mb.chroma_pred_mode=h.chroma_pred_mode;
            mb.coded_block_pattern=h.coded_block_pattern; mb.qp_y=h.qp_y;
            if (crtube_h264_parse_intra4x4_luma_residual(rbsp,rbsp_size,
                &bit_offset,h.coded_block_pattern,hl?left_nz:NULL,
                ht?top_nz[mx]:NULL,hl,ht,mb.luma_levels,
                mb.luma_total_coeff)<0) return -1;
            if (crtube_h264_parse_intra4x4_chroma_residual(rbsp,rbsp_size,
                &bit_offset,h.coded_block_pattern>>4,hl?left_cb:NULL,
                ht?top_cb[mx]:NULL,hl?left_cr:NULL,ht?top_cr[mx]:NULL,
                hl,ht,chroma_dc,chroma_ac,chroma_dc_total,chroma_ac_total)<0)
                return -1;
            memcpy(mb.chroma_dc,chroma_dc,sizeof(chroma_dc));
            memcpy(mb.chroma_ac,chroma_ac,sizeof(chroma_ac));
            memcpy(mb.chroma_dc_total,chroma_dc_total,sizeof(chroma_dc_total));
            memcpy(mb.chroma_ac_total,chroma_ac_total,sizeof(chroma_ac_total));
            if (crtube_h264_reconstruct_i4x4_mb_to_frame(frame,&mb,
                    chroma_qp_index_offset)<0) return -2;
            left_modes[0]=h.modes[5]; left_modes[1]=h.modes[7];
            left_modes[2]=h.modes[13]; left_modes[3]=h.modes[15];
            top_modes[mx][0]=h.modes[10]; top_modes[mx][1]=h.modes[11];
            top_modes[mx][2]=h.modes[14]; top_modes[mx][3]=h.modes[15];
            left_nz[0]=mb.luma_total_coeff[5]; left_nz[1]=mb.luma_total_coeff[7];
            left_nz[2]=mb.luma_total_coeff[13]; left_nz[3]=mb.luma_total_coeff[15];
            top_nz[mx][0]=mb.luma_total_coeff[10]; top_nz[mx][1]=mb.luma_total_coeff[11];
            top_nz[mx][2]=mb.luma_total_coeff[14]; top_nz[mx][3]=mb.luma_total_coeff[15];
            left_cb[0]=mb.chroma_ac_total[0][1]; left_cb[1]=mb.chroma_ac_total[0][3];
            left_cr[0]=mb.chroma_ac_total[1][1]; left_cr[1]=mb.chroma_ac_total[1][3];
            top_cb[mx][0]=mb.chroma_ac_total[0][2]; top_cb[mx][1]=mb.chroma_ac_total[0][3];
            top_cr[mx][0]=mb.chroma_ac_total[1][2]; top_cr[mx][1]=mb.chroma_ac_total[1][3];
            current_qp=h.qp_y;
        } else {
            return -2;
        }
        ++count; ++address;
    }
    return (int)count;
}

int crtube_h264_decode_i16x16_slice_to_frame(
    const uint8_t *rbsp, size_t rbsp_size, size_t bit_offset,
    crtube_h264_frame_t *frame, unsigned first_mb_in_slice,
    unsigned current_qp, int chroma_qp_index_offset)
{
    return crtube_h264_decode_i_slice_to_frame(rbsp,rbsp_size,bit_offset,
        frame,first_mb_in_slice,current_qp,0u,chroma_qp_index_offset);
}
