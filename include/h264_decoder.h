#ifndef CRTUBE_PS2_H264_DECODER_H
#define CRTUBE_PS2_H264_DECODER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CRTUBE_H264_MAX_WIDTH  480
#define CRTUBE_H264_MAX_HEIGHT 272

typedef struct crtube_h264_frame {
    uint8_t *storage;
    uint8_t *y;
    uint8_t *cb;
    uint8_t *cr;
    unsigned width;
    unsigned height;
    unsigned chroma_width;
    unsigned chroma_height;
    size_t storage_size;
} crtube_h264_frame_t;

typedef struct crtube_h264_nal {
    const uint8_t *data;
    size_t size;
    uint8_t type;
    uint8_t ref_idc;
} crtube_h264_nal_t;

typedef struct crtube_h264_stream_info {
    unsigned profile_idc;
    unsigned level_idc;
    unsigned chroma_format_idc;
    unsigned bit_depth_luma;
    unsigned bit_depth_chroma;
    unsigned width;
    unsigned height;
    unsigned coded_width;
    unsigned coded_height;
    unsigned frame_mbs_only;
    unsigned num_ref_frames;
    unsigned gaps_in_frame_num_value_allowed;
    unsigned delta_pic_order_always_zero;
    int offset_for_non_ref_pic;
    int offset_for_top_to_bottom_field;
    unsigned num_ref_frames_in_pic_order_cnt_cycle;
    int offset_for_ref_frame[256];
    unsigned vui_parameters_present;
    unsigned log2_max_frame_num;
    unsigned pic_order_cnt_type;
    unsigned log2_max_pic_order_cnt_lsb;
    unsigned sps_id;
    unsigned pps_count;
    unsigned idr_count;
    unsigned slice_count;
} crtube_h264_stream_info_t;

typedef struct crtube_h264_pps_info {
    unsigned pps_id;
    unsigned sps_id;
    unsigned entropy_coding_mode;
    unsigned bottom_field_pic_order_in_frame_present;
    unsigned num_slice_groups_minus1;
    unsigned num_ref_idx_l0_default;
    unsigned num_ref_idx_l1_default;
    unsigned weighted_pred;
    unsigned weighted_bipred_idc;
    int pic_init_qp_minus26;
    int pic_init_qs_minus26;
    int chroma_qp_index_offset;
    unsigned deblocking_filter_control_present;
    unsigned constrained_intra_pred;
    unsigned redundant_pic_cnt_present;
    unsigned transform_8x8_mode;
    unsigned pic_scaling_matrix_present;
    int second_chroma_qp_index_offset;
} crtube_h264_pps_info_t;

/* H.264 unidirectional weighted-prediction parameters for one L0 reference.
   A zero-initialized structure means default (unweighted) prediction. */
typedef struct crtube_h264_pred_weight {
    unsigned luma_denom;
    unsigned chroma_denom;
    int luma_weight;
    int luma_offset;
    int chroma_weight_cb;
    int chroma_offset_cb;
    int chroma_weight_cr;
    int chroma_offset_cr;
    unsigned enabled;
} crtube_h264_pred_weight_t;

typedef struct crtube_h264_slice_info {
    unsigned first_mb_in_slice;
    unsigned slice_type;
    unsigned pps_id;
    unsigned frame_num;
    unsigned idr_pic_id;
    unsigned pic_order_cnt_lsb;
    unsigned nal_type;
    unsigned nal_ref_idc;
    unsigned direct_spatial_mv_pred;
    unsigned num_ref_idx_l0_active;
    unsigned num_ref_idx_l1_active;
    unsigned ref_pic_list_reordering_l0;
    unsigned adaptive_ref_pic_marking_mode;
    unsigned no_output_of_prior_pics;
    unsigned long_term_reference;
    unsigned disable_deblocking_filter_idc;
    int slice_alpha_c0_offset_div2;
    int slice_beta_offset_div2;
    int slice_qp_delta;
    unsigned pred_weight_l0_count;
    crtube_h264_pred_weight_t pred_weight_l0[32];
    size_t slice_data_bit_offset;
} crtube_h264_slice_info_t;


/* Partition geometry is expressed in 4x4 luma blocks. A macroblock can be
   represented by up to sixteen independently motion-compensated partitions.
   Motion vectors use quarter-luma-sample units. */
typedef struct crtube_h264_p_partition {
    uint8_t x4, y4, width4, height4;
    unsigned ref_idx_l0;
    int mvd_x, mvd_y;
    int mv_x_qpel, mv_y_qpel;
} crtube_h264_p_partition_t;

typedef struct crtube_h264_p_mb_partition_header {
    unsigned mb_address;
    unsigned mb_type;
    unsigned coded_block_pattern;
    int mb_qp_delta;
    unsigned partition_count;
    crtube_h264_p_partition_t partitions[16];
    size_t data_bit_offset;
} crtube_h264_p_mb_partition_header_t;

/* Build normative P macroblock partition geometry. mb_type is the P-slice
   macroblock_type value 0..4. For mb_type 3/4, sub_mb_types[4] contains
   P_L0_8x8/P_L0_8x4/P_L0_4x8/P_L0_4x4 values 0..3. Returned partitions
   are in syntax order; the caller fills each partition's motion vector. */
int crtube_h264_p_partition_layout(unsigned mb_type,
    const uint8_t sub_mb_types[4], crtube_h264_p_partition_t partitions[16],
    unsigned *partition_count);
int crtube_h264_parse_p_mb_partitions(
    const uint8_t *rbsp, size_t rbsp_size, size_t *bit_offset,
    unsigned mb_address, unsigned num_ref_idx_l0_active,
    const int mvp_x[16], const int mvp_y[16],
    crtube_h264_p_mb_partition_header_t *header);

/* Motion-compensate a macroblock using a complete, non-overlapping partition
   layout and optional uni-prediction weights. */
int crtube_h264_motion_compensate_partitions(
    const crtube_h264_frame_t *reference, unsigned mb_x, unsigned mb_y,
    const crtube_h264_p_partition_t *partitions, unsigned partition_count,
    const crtube_h264_pred_weight_t *weights,
    uint8_t out_y[256], uint8_t out_cb[64], uint8_t out_cr[64]);

/* Check the compiled CAVLC VLC tables for invalid lengths, out-of-range
   codewords and prefix collisions. Intended for host tests/startup checks. */
int crtube_h264_cavlc_validate_tables(void);

typedef struct crtube_h264_p_mb_header {
    unsigned mb_address;
    unsigned mb_type;
    unsigned skipped;
    unsigned skip_run; /* Number of consecutive skipped MBs from this address. */
    unsigned ref_idx_l0;
    int mvd_l0_x;
    int mvd_l0_y;
    int mv_l0_x;
    int mv_l0_y;
    unsigned coded_block_pattern;
    int mb_qp_delta;
    size_t data_bit_offset;
} crtube_h264_p_mb_header_t;

/* Parses the supported P-slice macroblock header subset:
   P_Skip and P_L0_16x16. mvp_l0 is supplied by the caller from
   neighboring decoded motion vectors. Residual parsing is a later stage.
   Returns 0 on success, -1 on malformed input, -2 on unsupported mb_type. */
int crtube_h264_parse_p_mb_header(
    const uint8_t *rbsp, size_t rbsp_size, size_t *bit_offset,
    unsigned mb_address, unsigned num_ref_idx_l0_active,
    int mvp_l0_x, int mvp_l0_y, crtube_h264_p_mb_header_t *header);

/* Parses one P-slice macroblock address sequence, expanding mb_skip_run.
   The function stops after the first non-skipped P_L0_16x16 header.
   Returns 1 for a parsed macroblock, 0 at rbsp_trailing_bits,
   -1 for malformed input, -2 for unsupported macroblock syntax. */
int crtube_h264_parse_p_slice_mb(
    const uint8_t *rbsp, size_t rbsp_size, size_t *bit_offset,
    unsigned *next_mb_address, unsigned num_ref_idx_l0_active,
    int mvp_l0_x, int mvp_l0_y, crtube_h264_p_mb_header_t *header);

struct crtube_h264_dpb;

/* Decodes one complete raster-ordered P slice for the supported subset:
   P_Skip and P_L0_16x16, one reference, CAVLC, no FMO. first_mb_in_slice
   may identify any raster macroblock; neighbors outside this slice are
   unavailable. The frame and DPB dimensions must match. qp_y is the slice
   QP on entry and final QP on exit.
   Returns decoded macroblock count, -1 on malformed input, -2 if unsupported. */
int crtube_h264_decode_p_slice_data(
    const uint8_t *rbsp, size_t rbsp_size, size_t bit_offset,
    crtube_h264_frame_t *current, const struct crtube_h264_dpb *dpb,
    unsigned first_mb_in_slice, unsigned num_ref_idx_l0_active,
    unsigned *qp_y, int chroma_qp_index_offset,
    int second_chroma_qp_index_offset);
int crtube_h264_decode_p_slice_data_weighted(
    const uint8_t *rbsp, size_t rbsp_size, size_t bit_offset,
    crtube_h264_frame_t *current, const struct crtube_h264_dpb *dpb,
    unsigned first_mb_in_slice, unsigned num_ref_idx_l0_active,
    const crtube_h264_pred_weight_t *weights, unsigned weight_count,
    unsigned *qp_y, int chroma_qp_index_offset,
    int second_chroma_qp_index_offset);



typedef struct crtube_h264_p_mb_residual {
    int16_t luma_levels[16][16];
    uint8_t luma_total_coeff[16];
    int16_t chroma_dc[2][4];
    int16_t chroma_ac[2][4][15];
    uint8_t chroma_dc_total[2];
    uint8_t chroma_ac_total[2][4];
} crtube_h264_p_mb_residual_t;

/* Parse residual() for a P_L0 macroblock after its macroblock header.
   coded_block_pattern uses the H.264 inter mapping result (0..47).
   Neighbor non-zero counts are supplied by the slice walker. The bit
   offset and output are committed only if the entire residual parses. */
int crtube_h264_parse_p_mb_residual(
    const uint8_t *rbsp, size_t rbsp_size, size_t *bit_offset,
    unsigned coded_block_pattern,
    const uint8_t left_luma_nz[4], const uint8_t top_luma_nz[4],
    const uint8_t left_cb_nz[2], const uint8_t top_cb_nz[2],
    const uint8_t left_cr_nz[2], const uint8_t top_cr_nz[2],
    unsigned have_left, unsigned have_top,
    crtube_h264_p_mb_residual_t *residual);

/* Extracts the non-zero coefficient contexts needed by the next macroblock.
   right_* are the current macroblock's right edge; bottom_* are its bottom
   edge. For P_Skip or a zero residual, pass a zero-initialized residual. */
int crtube_h264_p_mb_nz_edges(
    const crtube_h264_p_mb_residual_t *residual,
    uint8_t right_luma_nz[4], uint8_t bottom_luma_nz[4],
    uint8_t right_cb_nz[2], uint8_t bottom_cb_nz[2],
    uint8_t right_cr_nz[2], uint8_t bottom_cr_nz[2]);

typedef struct crtube_h264_i4x4_mb_data {
    unsigned mb_address;
    uint8_t modes[16];
    unsigned chroma_pred_mode;
    unsigned coded_block_pattern;
    unsigned qp_y;
    int16_t luma_levels[16][16];
    uint8_t luma_total_coeff[16];
    int16_t chroma_dc[2][4];
    int16_t chroma_ac[2][4][15];
    uint8_t chroma_dc_total[2];
    uint8_t chroma_ac_total[2][4];
    /* External neighbors are available only when they belong to this slice. */
    uint8_t slice_have_top;
    uint8_t slice_have_left;
    uint8_t slice_have_top_right;
} crtube_h264_i4x4_mb_data_t;

/* Reconstructs a parsed Intra_4x4 macroblock into an initialized YUV420
   frame. Macroblocks must be submitted in raster order so top/left neighbors
   are already available. This entry point currently supports I_NxN only. */
int crtube_h264_reconstruct_i4x4_mb_to_frame(
    crtube_h264_frame_t *frame,
    const crtube_h264_i4x4_mb_data_t *mb,
    int chroma_qp_index_offset);


/* Parses raster-ordered I_NxN (Intra_4x4) macroblocks from slice_data().
   rbsp is the slice RBSP without the NAL header or emulation-prevention bytes.
   bit_offset points to the first bit after the slice header. The caller owns
   macroblocks and supplies capacity in macroblocks. Returns the number parsed,
   -1 for malformed/truncated input, or -2 for unsupported syntax. */
int crtube_h264_parse_i4x4_slice_data(
    const uint8_t *rbsp, size_t rbsp_size, size_t bit_offset,
    unsigned first_mb_in_slice, unsigned mb_width, unsigned mb_height,
    unsigned current_qp, unsigned transform_8x8_mode,
    crtube_h264_i4x4_mb_data_t *macroblocks, unsigned max_macroblocks);

/* Parses and reconstructs a supported I_NxN slice into a YUV420 frame.
   The current parser requires first_mb_in_slice == 0. The caller supplies
   a reusable macroblock array; for 320x240, capacity must be at least 300.
   Macroblocks are reconstructed in raster
   order. Returns reconstructed macroblock count, -1 for invalid input or
   malformed data, and -2 for unsupported syntax/reconstruction. */
int crtube_h264_decode_i4x4_slice_to_frame(
    const uint8_t *rbsp, size_t rbsp_size, size_t bit_offset,
    crtube_h264_frame_t *frame, unsigned first_mb_in_slice,
    unsigned current_qp, unsigned transform_8x8_mode,
    int chroma_qp_index_offset,
    crtube_h264_i4x4_mb_data_t *macroblocks, unsigned max_macroblocks);


typedef struct crtube_h264_i16x16_mb_data {
    unsigned mb_address;
    unsigned pred_mode; /* 0 vertical, 1 horizontal, 2 DC, 3 plane */
    unsigned chroma_pred_mode;
    unsigned coded_block_pattern;
    unsigned qp_y;
    int16_t luma_dc[16];
    uint8_t luma_dc_total;
    int16_t luma_ac[16][15];
    uint8_t luma_ac_total[16];
    int16_t chroma_dc[2][4];
    uint8_t chroma_dc_total[2];
    int16_t chroma_ac[2][4][15];
    uint8_t chroma_ac_total[2][4];
} crtube_h264_i16x16_mb_data_t;

/* Parses one Intra_16x16 macroblock_layer including luma/chroma CAVLC
   residuals. Neighbor non-zero contexts are supplied by the caller. */
int crtube_h264_parse_i16x16_mb(
    const uint8_t *rbsp, size_t rbsp_size, size_t *bit_offset,
    unsigned current_qp, const uint8_t left_nz[4],
    const uint8_t top_nz[4], unsigned have_left, unsigned have_top,
    const uint8_t left_cb[2], const uint8_t top_cb[2],
    const uint8_t left_cr[2], const uint8_t top_cr[2],
    crtube_h264_i16x16_mb_data_t *out);

/* Reconstructs one parsed Intra_16x16 macroblock into a planar YUV420
   frame. Raster-order neighbors must already be reconstructed. */
int crtube_h264_reconstruct_i16x16_mb_to_frame(
    crtube_h264_frame_t *frame, const crtube_h264_i16x16_mb_data_t *mb,
    int chroma_qp_index_offset);
/* Parses and reconstructs a complete I-slice containing Intra_16x16 and
   Intra_4x4 macroblocks in raster order. Requires first_mb_in_slice == 0,
   CAVLC, 8-bit 4:2:0 and transform_8x8_mode_flag == 0. */
int crtube_h264_decode_i_slice_to_frame(
    const uint8_t *rbsp, size_t rbsp_size, size_t bit_offset,
    crtube_h264_frame_t *frame, unsigned first_mb_in_slice,
    unsigned current_qp, unsigned transform_8x8_mode,
    int chroma_qp_index_offset);

/* Parses and reconstructs a raster-ordered Intra_16x16 CAVLC slice starting
   at first_mb_in_slice. All macroblocks in the slice must use Intra_16x16. */
int crtube_h264_decode_i16x16_slice_to_frame(
    const uint8_t *rbsp, size_t rbsp_size, size_t bit_offset,
    crtube_h264_frame_t *frame, unsigned first_mb_in_slice,
    unsigned current_qp, int chroma_qp_index_offset);


typedef struct crtube_h264_intra4x4_mb_header {
    uint8_t modes[16];
    unsigned chroma_pred_mode;
    unsigned coded_block_pattern;
    unsigned coded_block_pattern_code_num;
    int mb_qp_delta;
    unsigned qp_y;
} crtube_h264_intra4x4_mb_header_t;

/* Initializes a caller-owned planar YUV420 frame. Dimensions must be
   multiples of 16 and within the initial PS2 decoder limits. Storage layout
   is Y, then Cb, then Cr; required bytes are width*height*3/2. */

/* Single-reference decoded picture buffer and integer-pel motion compensation.
   Motion vectors are expressed in quarter-luma-sample units, as in H.264.
   This first backend accepts only vectors divisible by four; fractional
   interpolation is intentionally rejected rather than approximated. */
typedef struct crtube_h264_dpb {
    crtube_h264_frame_t reference;
    uint8_t *owned_storage;
    size_t owned_capacity;
    unsigned valid;
    unsigned frame_num;
    int poc;
} crtube_h264_dpb_t;

int crtube_h264_dpb_init(crtube_h264_dpb_t *dpb, uint8_t *storage,
                         size_t storage_size, unsigned width,
                         unsigned height);
int crtube_h264_dpb_store(crtube_h264_dpb_t *dpb,
                          const crtube_h264_frame_t *frame,
                          unsigned frame_num, int poc);
void crtube_h264_dpb_reset(crtube_h264_dpb_t *dpb);

/* Reconstruct the prediction-only subset of P macroblocks. Supports
   P_Skip and P_L0_16x16 when coded_block_pattern is zero. The caller must
   supply a valid single reference DPB and the already derived motion vector.
   Returns 0 on success, -1 on invalid state, -2 for unsupported residual. */


/* Reconstructs a P_L0_16x16 macroblock including its parsed CAVLC residual.
   qp_y is the macroblock luma QP; chroma offsets come from the PPS. */
int crtube_h264_reconstruct_p16x16(
    crtube_h264_frame_t *current, const crtube_h264_dpb_t *dpb,
    const crtube_h264_p_mb_header_t *mb,
    const crtube_h264_p_mb_residual_t *residual,
    unsigned qp_y, int chroma_qp_index_offset,
    int second_chroma_qp_index_offset);
int crtube_h264_reconstruct_p_partitioned(
    crtube_h264_frame_t *current, const crtube_h264_dpb_t *dpb,
    const crtube_h264_p_mb_header_t *mb,
    const crtube_h264_p_partition_t *partitions, unsigned partition_count,
    const crtube_h264_pred_weight_t *weights,
    const crtube_h264_p_mb_residual_t *residual,
    unsigned qp_y, int chroma_qp_index_offset,
    int second_chroma_qp_index_offset);

int crtube_h264_reconstruct_p16x16_prediction(
    crtube_h264_frame_t *current, const crtube_h264_dpb_t *dpb,
    const crtube_h264_p_mb_header_t *mb);

/* Connects a parsed prediction-only P macroblock to the current frame.
   Supports P_Skip and P_L0_16x16 with reference index zero and CBP zero.
   Returns -3 for residual-bearing macroblocks, which require the full
   residual reconstruction path. */
int crtube_h264_reconstruct_p_mb_to_frame(
    crtube_h264_frame_t *current, const crtube_h264_dpb_t *dpb,
    const crtube_h264_p_mb_header_t *header);

/* Reconstructs P_Skip or P_L0_16x16 after residual parsing. P_Skip ignores
   the residual buffer; non-skipped macroblocks use qp_y and PPS chroma QP
   offsets for inverse quantization. Only reference index zero is supported. */
int crtube_h264_reconstruct_p_mb_with_residual(
    crtube_h264_frame_t *current, const crtube_h264_dpb_t *dpb,
    const crtube_h264_p_mb_header_t *header,
    const crtube_h264_p_mb_residual_t *residual,
    unsigned qp_y, int chroma_qp_index_offset,
    int second_chroma_qp_index_offset);

/* Parses, decodes residual(), and reconstructs one P_L0_16x16 macroblock.
   The caller supplies the motion-vector predictor and CAVLC neighbor counts.
   qp_y is updated only after successful reconstruction. P_Skip runs and
   unsupported P macroblock types remain the slice walker's responsibility. */
int crtube_h264_decode_p_l0_16x16_mb(
    const uint8_t *rbsp, size_t rbsp_size, size_t *bit_offset,
    crtube_h264_frame_t *current, const crtube_h264_dpb_t *dpb,
    unsigned mb_address, unsigned num_ref_idx_l0_active,
    int mvp_l0_x, int mvp_l0_y, unsigned *qp_y,
    int chroma_qp_index_offset, int second_chroma_qp_index_offset,
    const uint8_t left_luma_nz[4], const uint8_t top_luma_nz[4],
    const uint8_t left_cb_nz[2], const uint8_t top_cb_nz[2],
    const uint8_t left_cr_nz[2], const uint8_t top_cr_nz[2],
    unsigned have_left, unsigned have_top,
    crtube_h264_p_mb_header_t *header);

int crtube_h264_motion_compensate_16x16(
    const crtube_h264_frame_t *reference,
    unsigned mb_x, unsigned mb_y,
    int mv_x_qpel, int mv_y_qpel,
    uint8_t out_y[256], uint8_t out_cb[64], uint8_t out_cr[64]);

size_t crtube_h264_frame_bytes(unsigned width, unsigned height);
int crtube_h264_frame_init(crtube_h264_frame_t *frame, uint8_t *storage,
                           size_t storage_size, unsigned width,
                           unsigned height);
void crtube_h264_frame_clear(crtube_h264_frame_t *frame, uint8_t y,
                             uint8_t cb, uint8_t cr);
/* Copies one reconstructed 16x16 Y plus 8x8 Cb/Cr macroblock. */
int crtube_h264_frame_put_macroblock(crtube_h264_frame_t *frame,
                                     unsigned mb_x, unsigned mb_y,
                                     const uint8_t luma[256],
                                     const uint8_t cb[64],
                                     const uint8_t cr[64]);
/* Converts the complete YUV420 frame to packed RGB24 (R,G,B byte order).
   The destination must contain at least width*height*3 bytes. */
int crtube_h264_frame_to_rgb24(const crtube_h264_frame_t *frame,
                               uint8_t *rgb, size_t rgb_size);

/* Annex-B iterator. Initialize offset to zero; returns 1 for a NAL,
   0 at end, and a negative value for invalid arguments/empty NALs. */
int crtube_h264_annexb_next(const uint8_t *data, size_t size, size_t *offset,
                            crtube_h264_nal_t *nal);
/* Removes the NAL header and emulation-prevention bytes into caller RBSP storage. */
int crtube_h264_nal_to_rbsp(const uint8_t *nal, size_t nal_size,
                            uint8_t *rbsp, size_t capacity, size_t *rbsp_size);

/* Parses an SPS NAL payload (including its one-byte NAL header). */
int crtube_h264_parse_sps(const uint8_t *nal, size_t size,
                          crtube_h264_stream_info_t *info);

/* Parses a PPS NAL. FMO/slice groups are reported but not supported by the
   initial decoder profile. */
int crtube_h264_parse_pps(const uint8_t *nal, size_t size,
                          crtube_h264_pps_info_t *pps);

/* Parses the complete supported progressive Baseline slice header (I/P,
   CAVLC). slice_data_bit_offset is the first bit of slice_data(). Returns -2
   for unsupported syntax such as B/SP/SI, FMO, CABAC or weighted prediction
   configurations not supported by this initial implementation. */
int crtube_h264_parse_slice_header(const uint8_t *nal, size_t size,
                                   const crtube_h264_stream_info_t *sps,
                                   const crtube_h264_pps_info_t *pps,
                                   crtube_h264_slice_info_t *slice);

/* Decodes one CAVLC residual block from an RBSP bitstream. n_c is the
   predicted non-zero count (0..16), or -1 for 4:2:0 chroma DC.
   max_coeff must be 16 (4x4) or 4 (chroma DC). Coefficients are returned
   in scan order. bit_offset advances only when decoding succeeds. */
/* Validates all built-in CAVLC VLC codebooks for bounds and prefix conflicts. */
int crtube_h264_cavlc_validate_tables(void);

int crtube_h264_cavlc_decode_block(const uint8_t *rbsp, size_t rbsp_size,
                                    size_t *bit_offset, int n_c,
                                    unsigned max_coeff, int16_t coeffs[16],
                                    unsigned *total_coeff);

/* Inverse-quantizes one 4x4 coefficient block in raster order.
   qp is the luma/chroma QP (0..51). scaling may be NULL for a flat
   scaling list (16); otherwise it supplies 16 raster-order scale values. */
int crtube_h264_inverse_quant4x4(const int16_t levels[16], unsigned qp,
                                  const uint8_t scaling[16], int16_t coeff[16]);

/* Predicts one Intra_4x4 block. top[0..7] and left[0..3] are already
   edge-extended by the caller where neighboring samples are unavailable.
   Modes follow H.264: 0 vertical, 1 horizontal, 2 DC, 3 diagonal-down-left,
   4 diagonal-down-right, 5 vertical-right, 6 horizontal-down,
   7 vertical-left, 8 horizontal-up. Returns -2 when required references
   are unavailable. */
int crtube_h264_intra_predict4x4(unsigned mode, const uint8_t top[8],
                                  const uint8_t left[4], uint8_t top_left,
                                  unsigned have_top, unsigned have_left,
                                  unsigned have_top_left,
                                  uint8_t prediction[16]);

/* Reconstructs the luma plane of one Intra_4x4 macroblock. levels contains
   sixteen CAVLC coefficient blocks in scan order. top supplies 16 top samples
   plus four top-right samples; left supplies 16 left samples. Unavailable
   edges are ignored and edge-extended. The output is a 16x16 raster block. */
int crtube_h264_reconstruct_intra4x4_mb(const uint8_t top[20],
                                         const uint8_t left[16],
                                         uint8_t top_left,
                                         unsigned have_top,
                                         unsigned have_top_right,
                                         unsigned have_left,
                                         unsigned have_top_left,
                                         const uint8_t modes[16],
                                         const int16_t levels[16][16],
                                         unsigned qp,
                                         const uint8_t scaling[16],
                                         uint8_t output[256]);

/* Parses the macroblock_layer header for an I_NxN macroblock (mb_type 0).
   The input is RBSP data positioned at mb_type. transform_8x8_mode must be
   zero; other intra macroblock types are rejected. External neighbor modes
   are indexed by 4x4 row/column and use mode 2 when unavailable. The parser
   stops before residual() and advances bit_offset only on success. */
int crtube_h264_parse_intra4x4_mb_header(
    const uint8_t *rbsp, size_t rbsp_size, size_t *bit_offset,
    const uint8_t left_modes[4], const uint8_t top_modes[4],
    unsigned have_left, unsigned have_top, unsigned transform_8x8_mode,
    unsigned current_qp, crtube_h264_intra4x4_mb_header_t *header);

/* Parses the sixteen Intra_4x4 luma residual blocks following the macroblock
   header. coded_block_pattern is the full H.264 value (0..47); this function
   consumes only its low four luma bits. Neighbor non-zero counts are indexed
   by 4x4 row/column. Coefficients are returned in CAVLC scan order. */
int crtube_h264_parse_intra4x4_luma_residual(
    const uint8_t *rbsp, size_t rbsp_size, size_t *bit_offset,
    unsigned coded_block_pattern, const uint8_t left_nz[4],
    const uint8_t top_nz[4], unsigned have_left, unsigned have_top,
    int16_t levels[16][16], uint8_t total_coeff[16]);

/* Parses residual_chroma() for 4:2:0. chroma_cbp is coded_block_pattern>>4
   (0..2). DC coefficients are four CAVLC scan-order values per plane.
   AC coefficients are fifteen scan-order values per 4x4 block; element 0
   corresponds to scan position 1 (the DC position is omitted). Neighbor
   non-zero counts use two entries per edge, indexed by 4x4 block row/column. */
int crtube_h264_parse_intra4x4_chroma_residual(
    const uint8_t *rbsp, size_t rbsp_size, size_t *bit_offset,
    unsigned chroma_cbp, const uint8_t left_nz_cb[2],
    const uint8_t top_nz_cb[2], const uint8_t left_nz_cr[2],
    const uint8_t top_nz_cr[2], unsigned have_left, unsigned have_top,
    int16_t dc_levels[2][4], int16_t ac_levels[2][4][15],
    uint8_t dc_total_coeff[2], uint8_t ac_total_coeff[2][4]);

/* Maps QP_Y plus chroma_qp_index_offset to the normative 4:2:0 QP_C. */
unsigned crtube_h264_chroma_qp(unsigned qp_y, int chroma_qp_index_offset);

/* Applies the 2x2 chroma DC Hadamard transform and scaling process. */
int crtube_h264_inverse_chroma_dc4x4(const int16_t dc_levels[4],
                                      unsigned qp_c, int16_t dc_scaled[4]);

/* Predicts an 8x8 4:2:0 chroma block. Modes: 0 DC, 1 horizontal,
   2 vertical, 3 plane. top/left contain eight edge samples. */
int crtube_h264_intra_predict_chroma8x8(unsigned mode,
    const uint8_t top[8], const uint8_t left[8], uint8_t top_left,
    unsigned have_top, unsigned have_left, unsigned have_top_left,
    uint8_t prediction[64]);

/* Reconstructs one 8x8 chroma component from its DC and AC residuals. */
int crtube_h264_reconstruct_chroma8x8(unsigned mode,
    const uint8_t top[8], const uint8_t left[8], uint8_t top_left,
    unsigned have_top, unsigned have_left, unsigned have_top_left,
    const int16_t dc_levels[4], const int16_t ac_levels[4][15],
    unsigned qp_c, uint8_t output[64]);

/* Converts a 4x4 zig-zag coefficient block to raster order. */
void crtube_h264_scan4x4_to_raster(const int16_t scan[16], int16_t raster[16]);

/* H.264 integer inverse transform. Input coefficients must already be
   inverse-quantized and in raster order. Output is the 8-bit-domain residual. */
int crtube_h264_inverse_transform4x4(const int16_t coeff[16], int16_t residual[16]);

/* Adds a residual block to an 8-bit prediction block, clipping to [0,255]. */
void crtube_h264_add_residual4x4(const uint8_t prediction[16],
                                  const int16_t residual[16], uint8_t output[16]);

/* Inspects an Annex-B stream and validates the first SPS against the initial
   PS2 target: 8-bit 4:2:0 progressive H.264, up to 480x272 coded pixels.
   Returns 0 when compatible, -1 when malformed, -2 when unsupported. */
int crtube_h264_probe_annexb(const uint8_t *data, size_t size,
                             crtube_h264_stream_info_t *info);

#ifdef __cplusplus
}
#endif

#endif
