#include <tamtypes.h>
#include <dma.h>
#include <packet2.h>
#include <packet2_vif.h>
#include <packet2_chain.h>
#include <timer.h>
#include <kernel.h>
#include <debug.h>
#include <string.h>
#include <stdio.h>
#include "debug_log.h"

#define VU_GROUPS 16u
#define VU_REPEATS 64u
#define VU_SAMPLE_VECTORS 96u
#define VU_SAMPLE_INPUT_QWORDS 24u
#define VU_COEFF_INPUT_QWORDS 6u
#define VU_OUTPUT_QWORD 102u
/*
 * EE virtual addresses for VU data memories (not micro memories):
 * VU0 DMEM = 0x11004000, VU1 DMEM = 0x1100C000.
 * 0x11000000 and 0x11008000 are the corresponding micro-code memories.
 * Keep these in the EE TLB-mapped VU window; do not use KSEG1 aliases.
 */
#define VU0_DMEM_EE_ADDRESS 0x11004000u
#define VU1_DMEM_EE_ADDRESS 0x1100C000u
#define VU_PACKET_QWORDS 160u
#define VU_UPLOAD_QWORDS 160u
#define VU_TEST_ROUNDS 5u
#define VU_DMA_WAIT_LIMIT 20000000

typedef struct vu_test_data {
    u8 samples[VU_SAMPLE_VECTORS][4];
    float coefficients[6][4];
    u8 padding[32];
} vu_test_data_t __attribute__((aligned(64)));

static vu_test_data_t vu_data[3] __attribute__((aligned(64)));
static float scalar_result[3][VU_GROUPS][4] __attribute__((aligned(64)));
static float vu_readback[2][VU_GROUPS][4] __attribute__((aligned(64)));
static const float filter_coefficients[6] = {1.0f, -5.0f, 20.0f, 20.0f, -5.0f, 1.0f};
static const int filter_coefficients_int[6] = {1, -5, 20, 20, -5, 1};
static int vu_dma_timeout;
static packet2_t *vu_runtime_packet[2];
static int vu_runtime_ready;
static int vu_runtime_enabled;
static u32 vu_inter_calls;
static u32 vu_inter_rows;
static u32 vu_inter_parallel_batches;

extern u32 VU_H264_Filter_CodeStart __attribute__((section(".vudata")));
extern u32 VU_H264_Filter_CodeEnd __attribute__((section(".vudata")));
extern u32 VU_H264_Filter_Once_CodeStart __attribute__((section(".vudata")));
extern u32 VU_H264_Filter_Once_CodeEnd __attribute__((section(".vudata")));

static u32 vu_ticks_to_us(u64 ticks)
{
    return (u32)((ticks * 125u) / 18432u);
}

static void vu_add_start_program(packet2_t *packet)
{
    /*
     * With TTE enabled, each DMA tag is followed by a 64-bit VIF-code
     * pair. Keep each CNT TTE area at exactly two VIF words so the next
     * DMA tag remains 16-byte aligned.
     */
    crtube_log("VU stage: start-program CNT begin\n");
    packet2_chain_open_cnt(packet, 0, 0, 0);
    packet2_vif_flushe(packet, 0);
    packet2_vif_mscal(packet, 0, 0);
    packet2_chain_close_tag(packet);
    crtube_log("VU stage: start-program MSCAL block complete\n");

    /*
     * FLUSHE is required here: FLUSH only waits for GIF PATH1/PATH2
     * transfers after VU1. FLUSHE waits for the VU microprogram itself
     * to finish, so the EE readback cannot race the VU stores.
     */
    packet2_chain_open_cnt(packet, 0, 0, 0);
    packet2_vif_flushe(packet, 0);
    packet2_vif_nop(packet, 0);
    packet2_chain_close_tag(packet);
    crtube_log("VU stage: post-MSCAL FLUSHE block complete\n");
}

static void vu_add_end_tag(packet2_t *packet)
{
    packet2_chain_open_end(packet, 0, 0);
    packet2_vif_nop(packet, 0);
    packet2_vif_nop(packet, 0);
    packet2_chain_close_tag(packet);
}

static void vu_make_input(vu_test_data_t *data, unsigned seed)
{
    unsigned tap, group, lane;
    memset(data, 0, sizeof(*data));
    for (tap = 0; tap < 6u; ++tap) {
        for (group = 0; group < VU_GROUPS; ++group) {
            for (lane = 0; lane < 4u; ++lane) {
                unsigned value = (tap * 47u + group * 13u + lane * 29u + seed * 31u) & 255u;
                data->samples[tap * VU_GROUPS + group][lane] = (u8)value;
            }
        }
    }
    for (tap = 0; tap < 6u; ++tap) {
        for (lane = 0; lane < 4u; ++lane)
            data->coefficients[tap][lane] = filter_coefficients[tap];
    }
}

static void vu_scalar_kernel(const vu_test_data_t *data,
                             float output[VU_GROUPS][4])
{
    /*
     * This routine is also the EE benchmark reference. Volatile input loads
     * prevent -O2 from hoisting the invariant six-tap sums out of the 64
     * repetitions; volatile stores preserve the same repeated output traffic.
     */
    const volatile u8 *samples = &data->samples[0][0];
    volatile float *result = &output[0][0];
    unsigned repeat, group, lane, tap;
    for (repeat = 0; repeat < VU_REPEATS; ++repeat) {
        for (group = 0; group < VU_GROUPS; ++group) {
            for (lane = 0; lane < 4u; ++lane) {
                int sum = 0;
                for (tap = 0; tap < 6u; ++tap) {
                    unsigned index = (tap * VU_GROUPS + group) * 4u + lane;
                    sum += (int)samples[index] *
                           filter_coefficients_int[tap];
                }
                result[group * 4u + lane] = (float)sum;
            }
        }
    }
}

static int vu_upload_program_range(packet2_t *packet, int channel,
                                   u32 *code_start, u32 *code_end)
{
    packet2_reset(packet, 0);
    packet2_vif_add_micro_program(packet, 0, code_start, code_end);
    vu_add_end_tag(packet);
    /*
     * packet2_vif_add_micro_program() emits REF tags pointing directly at
     * .vutext; it does not copy the microcode into the packet. Both the
     * chain packet and the referenced code therefore need a D-cache writeback.
     */
    SyncDCache(packet->base, packet->next);
    SyncDCache((void *)code_start, (void *)code_end);
    crtube_log("VU stage: upload send channel=%d qwords=%u code_bytes=%u\n",
               channel, VU_UPLOAD_QWORDS,
               (unsigned)((u8 *)code_end - (u8 *)code_start));
    dma_channel_send_packet2(packet, channel, 1);
    {
        int wait_result = dma_channel_wait(channel, VU_DMA_WAIT_LIMIT);
        crtube_log("VU stage: upload wait channel=%d result=%d\n",
                   channel, wait_result);
        if (wait_result != 0)
            vu_dma_timeout = 1;
        return wait_result;
    }
}

static int vu_upload_program(packet2_t *packet, int channel)
{
    return vu_upload_program_range(packet, channel,
                                   &VU_H264_Filter_CodeStart,
                                   &VU_H264_Filter_CodeEnd);
}

static int vu_upload_program_once(packet2_t *packet, int channel)
{
    return vu_upload_program_range(packet, channel,
                                   &VU_H264_Filter_Once_CodeStart,
                                   &VU_H264_Filter_Once_CodeEnd);
}

static void vu_build_compute(packet2_t *packet,
                             const vu_test_data_t *data)
{
    crtube_log("VU stage: compute packet reset begin\n");
    packet2_reset(packet, 0);
    crtube_log("VU stage: compute packet reset complete\n");
    crtube_log("VU stage: sample chain ref begin\n");
    packet2_chain_ref(packet, (void *)data->samples,
                      VU_SAMPLE_INPUT_QWORDS, 0, 0, 0);
    packet2_vif_stcycl(packet, 1, 1, 0);
    packet2_vif_open_unpack(packet, P2_UNPACK_V4_8, 0, 0, 0, 1, 0);
    packet2_vif_close_unpack_manual(packet, VU_SAMPLE_VECTORS);
    crtube_log("VU stage: sample unpack complete\n");
    packet2_chain_ref(packet, (void *)data->coefficients,
                      VU_COEFF_INPUT_QWORDS, 0, 0, 0);
    packet2_vif_stcycl(packet, 1, 1, 0);
    packet2_vif_open_unpack(packet, P2_UNPACK_V4_32, 96, 0, 0, 1, 0);
    packet2_vif_close_unpack_manual(packet, VU_COEFF_INPUT_QWORDS);
    crtube_log("VU stage: coefficient unpack complete\n");
    vu_add_start_program(packet);
    crtube_log("VU stage: start-program blocks appended\n");
    /* END tag follows on the next 16-byte boundary. */
    crtube_log("VU stage: END tag begin\n");
    vu_add_end_tag(packet);
    crtube_log("VU stage: END tag complete\n");
    crtube_log("VU stage: compute packet build complete base=%p next=%p\n",
               packet->base, packet->next);
}

static void vu_queue_compute(packet2_t *packet, int channel,
                             const vu_test_data_t *data)
{
    crtube_log("VU stage: compute build begin channel=%d\n", channel);
    vu_build_compute(packet, data);
    crtube_log("VU stage: compute build returned channel=%d\n", channel);
    crtube_log("VU stage: packet cache sync begin channel=%d\n", channel);
    SyncDCache(packet->base, packet->next);
    crtube_log("VU stage: packet cache sync complete channel=%d\n", channel);
    crtube_log("VU stage: source cache sync begin channel=%d\n", channel);
    SyncDCache((void *)data->samples,
               (void *)(data->coefficients + 6));
    crtube_log("VU stage: source cache sync complete channel=%d\n", channel);
    crtube_log("VU stage: DMA send begin channel=%d\n", channel);
    dma_channel_send_packet2(packet, channel, 0);
    crtube_log("VU stage: DMA send returned channel=%d\n", channel);
}

static int vu_wait_compute(int channel)
{
    int wait_result;
    crtube_log("VU stage: compute wait channel=%d\n", channel);
    wait_result = dma_channel_wait(channel, VU_DMA_WAIT_LIMIT);
    crtube_log("VU stage: compute wait complete channel=%d result=%d\n",
               channel, wait_result);
    if (wait_result != 0)
        vu_dma_timeout = 1;
    return wait_result;
}

static int vu_send_compute(packet2_t *packet, int channel,
                           const vu_test_data_t *data)
{
    vu_queue_compute(packet, channel, data);
    return vu_wait_compute(channel);
}

/* vu_memory must address VU data memory, not the micro-code memory. */
static void vu_log_dmem_probe(const volatile float *vu_memory,
                              const char *name)
{
    const volatile float *memory = vu_memory;
    crtube_log("VU DMEM probe unit=%s qword102=%d,%d,%d,%d\n",
               name, (int)memory[VU_OUTPUT_QWORD * 4u],
               (int)memory[VU_OUTPUT_QWORD * 4u + 1u],
               (int)memory[VU_OUTPUT_QWORD * 4u + 2u],
               (int)memory[VU_OUTPUT_QWORD * 4u + 3u]);
}

static void vu_dump_dmem_words(const volatile u32 *vu_memory,
                               const char *name, unsigned qword)
{
    const volatile u32 *p = vu_memory + qword * 4u;
    crtube_log("VU DMEM probe unit=%s qword=%u words=%08x,%08x,%08x,%08x\n",
               name, qword, (unsigned)p[0], (unsigned)p[1],
               (unsigned)p[2], (unsigned)p[3]);
}

static void vu_copy_output(const volatile float *vu_memory,
                           float output[VU_GROUPS][4])
{
    unsigned group, lane;
    const volatile float *source = vu_memory + VU_OUTPUT_QWORD * 4u;
    for (group = 0; group < VU_GROUPS; ++group)
        for (lane = 0; lane < 4u; ++lane)
            output[group][lane] = source[group * 4u + lane];
}

static int vu_compare_output(const float got[VU_GROUPS][4],
                             const float expected[VU_GROUPS][4],
                             const char *name)
{
    unsigned group, lane;
    for (group = 0; group < VU_GROUPS; ++group) {
        for (lane = 0; lane < 4u; ++lane) {
            if (got[group][lane] != expected[group][lane]) {
                crtube_log("VU kernel mismatch unit=%s group=%u lane=%u got=%d expected=%d\n",
                           name, group, lane, (int)got[group][lane],
                           (int)expected[group][lane]);
                return -1;
            }
        }
    }
    return 0;
}

static u32 vu_bench_ee_three(void)
{
    u64 start = GetTimerSystemTime();
    unsigned i;
    for (i = 0; i < 3u; ++i)
        vu_scalar_kernel(&vu_data[i], scalar_result[i]);
    return vu_ticks_to_us(GetTimerSystemTime() - start);
}

static u32 vu_bench_vu0_plus_ee(packet2_t *packet, u32 *ee_us)
{
    u64 start;
    int wait_result;
    crtube_log_set_enabled(0);
    start = GetTimerSystemTime();
    vu_queue_compute(packet, DMA_CHANNEL_VIF0, &vu_data[0]);
    vu_scalar_kernel(&vu_data[1], scalar_result[1]);
    vu_scalar_kernel(&vu_data[2], scalar_result[2]);
    wait_result = vu_wait_compute(DMA_CHANNEL_VIF0);
    vu_copy_output((volatile float *)VU0_DMEM_EE_ADDRESS, vu_readback[0]);
    *ee_us = vu_ticks_to_us(GetTimerSystemTime() - start);
    crtube_log_set_enabled(1);
    if (wait_result != 0) {
        crtube_log("VU BENCH VU0: DMA wait failed result=%d\n", wait_result);
        return 0xffffffffu;
    }
    if (vu_compare_output(vu_readback[0], scalar_result[0], "VU0") != 0)
        return 0xffffffffu;
    return *ee_us;
}

static u32 vu_bench_vu1_plus_ee(packet2_t *packet, u32 *ee_us)
{
    u64 start;
    int wait_result;
    crtube_log_set_enabled(0);
    start = GetTimerSystemTime();
    vu_queue_compute(packet, DMA_CHANNEL_VIF1, &vu_data[0]);
    vu_scalar_kernel(&vu_data[1], scalar_result[1]);
    vu_scalar_kernel(&vu_data[2], scalar_result[2]);
    wait_result = vu_wait_compute(DMA_CHANNEL_VIF1);
    vu_copy_output((volatile float *)VU1_DMEM_EE_ADDRESS, vu_readback[1]);
    *ee_us = vu_ticks_to_us(GetTimerSystemTime() - start);
    crtube_log_set_enabled(1);
    if (wait_result != 0) {
        crtube_log("VU BENCH VU1: DMA wait failed result=%d\n", wait_result);
        return 0xffffffffu;
    }
    if (vu_compare_output(vu_readback[1], scalar_result[0], "VU1") != 0)
        return 0xffffffffu;
    return *ee_us;
}

static u32 vu_bench_all_parallel(packet2_t *packet0, packet2_t *packet1)
{
    u64 start;
    u32 elapsed_us;
    int wait0, wait1;
    crtube_log_set_enabled(0);
    start = GetTimerSystemTime();
    vu_build_compute(packet0, &vu_data[0]);
    vu_build_compute(packet1, &vu_data[1]);
    SyncDCache(packet0->base, packet0->next);
    SyncDCache(packet1->base, packet1->next);
    SyncDCache((void *)vu_data[0].samples,
               (void *)(vu_data[0].coefficients + 6));
    SyncDCache((void *)vu_data[1].samples,
               (void *)(vu_data[1].coefficients + 6));
    dma_channel_send_packet2(packet0, DMA_CHANNEL_VIF0, 0);
    dma_channel_send_packet2(packet1, DMA_CHANNEL_VIF1, 0);
    vu_scalar_kernel(&vu_data[2], scalar_result[2]);
    wait0 = vu_wait_compute(DMA_CHANNEL_VIF0);
    wait1 = vu_wait_compute(DMA_CHANNEL_VIF1);
    vu_copy_output((volatile float *)VU0_DMEM_EE_ADDRESS, vu_readback[0]);
    vu_copy_output((volatile float *)VU1_DMEM_EE_ADDRESS, vu_readback[1]);
    elapsed_us = vu_ticks_to_us(GetTimerSystemTime() - start);
    crtube_log_set_enabled(1);
    if (wait0 != 0 || wait1 != 0) {
        crtube_log("VU BENCH parallel: DMA wait failed VIF0=%d VIF1=%d\n",
                   wait0, wait1);
        return 0xffffffffu;
    }
    if (vu_compare_output(vu_readback[0], scalar_result[0], "VU0") != 0 ||
        vu_compare_output(vu_readback[1], scalar_result[1], "VU1") != 0)
        return 0xffffffffu;
    return elapsed_us;
}


static void vu_make_inter_block(vu_test_data_t *data,
                                const u8 *src, unsigned stride)
{
    unsigned tap, row, col_group, lane;
    memset(data, 0, sizeof(*data));
    for (tap = 0; tap < 6u; ++tap) {
        for (row = 0; row < 4u; ++row) {
            const u8 *source_row = src +
                ((int)row + (int)tap - 2) * (int)stride;
            for (col_group = 0; col_group < 4u; ++col_group) {
                unsigned group = row * 4u + col_group;
                for (lane = 0; lane < 4u; ++lane)
                    data->samples[tap * VU_GROUPS + group][lane] =
                        source_row[col_group * 4u + lane];
            }
        }
    }
    for (tap = 0; tap < 6u; ++tap)
        for (lane = 0; lane < 4u; ++lane)
            data->coefficients[tap][lane] = filter_coefficients[tap];
}

static void vu_add_start_program_quiet(packet2_t *packet)
{
    packet2_chain_open_cnt(packet, 0, 0, 0);
    packet2_vif_flushe(packet, 0);
    packet2_vif_mscal(packet, 0, 0);
    packet2_chain_close_tag(packet);
    packet2_chain_open_cnt(packet, 0, 0, 0);
    packet2_vif_flushe(packet, 0);
    packet2_vif_nop(packet, 0);
    packet2_chain_close_tag(packet);
}

static void vu_queue_inter_compute(packet2_t *packet, int channel,
                                   const vu_test_data_t *data)
{
    packet2_reset(packet, 0);
    packet2_chain_ref(packet, (void *)data->samples,
                      VU_SAMPLE_INPUT_QWORDS, 0, 0, 0);
    packet2_vif_stcycl(packet, 1, 1, 0);
    packet2_vif_open_unpack(packet, P2_UNPACK_V4_8, 0, 0, 0, 1, 0);
    packet2_vif_close_unpack_manual(packet, VU_SAMPLE_VECTORS);
    packet2_chain_ref(packet, (void *)data->coefficients,
                      VU_COEFF_INPUT_QWORDS, 0, 0, 0);
    packet2_vif_stcycl(packet, 1, 1, 0);
    packet2_vif_open_unpack(packet, P2_UNPACK_V4_32, 96, 0, 0, 1, 0);
    packet2_vif_close_unpack_manual(packet, VU_COEFF_INPUT_QWORDS);
    vu_add_start_program_quiet(packet);
    vu_add_end_tag(packet);
    SyncDCache(packet->base, packet->next);
    SyncDCache((void *)data->samples,
               (void *)(data->coefficients + 6));
    dma_channel_send_packet2(packet, channel, 0);
}

static int vu_read_inter_output(int channel, unsigned row_offset,
                                s16 *output, unsigned output_stride)
{
    const volatile float *memory =
        (const volatile float *)(channel == DMA_CHANNEL_VIF0 ?
            VU0_DMEM_EE_ADDRESS : VU1_DMEM_EE_ADDRESS);
    unsigned row, col;
    for (row = 0; row < 4u; ++row) {
        for (col = 0; col < 16u; ++col) {
            unsigned group = row * 4u + col / 4u;
            unsigned lane = col & 3u;
            output[(row_offset + row) * output_stride + col] =
                (s16)memory[(VU_OUTPUT_QWORD + group) * 4u + lane];
        }
    }
    return 0;
}

/*
 * Real decoder workload: one 16x4 vertical H.264 luma six-tap block.
 * Two independent 4-row bands are dispatched to VU0 and VU1 concurrently.
 * The VU returns exact integer-valued raw sums; H.264 rounding, clipping,
 * quarter-pel averaging and weighted prediction remain on the EE.
 */
int crtube_vu_inter_luma_vertical(const u8 *src, unsigned stride,
                                  unsigned rows, s16 *output,
                                  unsigned output_stride)
{
    unsigned row = 0;
    if (!vu_runtime_ready || !vu_runtime_enabled || !src || !output ||
        stride < 16u || output_stride < 16u || rows == 0u ||
        rows > 16u || (rows & 3u) != 0u)
        return -1;

    ++vu_inter_calls;
    vu_inter_rows += rows;
    while (row < rows) {
        unsigned remaining = rows - row;
        int parallel = remaining >= 8u;
        if (parallel)
            ++vu_inter_parallel_batches;
        vu_make_inter_block(&vu_data[0], src + row * stride, stride);
        if (parallel)
            vu_make_inter_block(&vu_data[1], src + (row + 4u) * stride, stride);

        vu_queue_inter_compute(vu_runtime_packet[0], DMA_CHANNEL_VIF0,
                               &vu_data[0]);
        if (parallel)
            vu_queue_inter_compute(vu_runtime_packet[1], DMA_CHANNEL_VIF1,
                                   &vu_data[1]);

        if (dma_channel_wait(DMA_CHANNEL_VIF0, VU_DMA_WAIT_LIMIT) != 0) {
            vu_dma_timeout = 1;
            vu_runtime_enabled = 0;
            vu_runtime_ready = 0;
            return -1;
        }
        if (parallel &&
            dma_channel_wait(DMA_CHANNEL_VIF1, VU_DMA_WAIT_LIMIT) != 0) {
            vu_dma_timeout = 1;
            vu_runtime_enabled = 0;
            vu_runtime_ready = 0;
            return -1;
        }
        vu_read_inter_output(DMA_CHANNEL_VIF0, row, output, output_stride);
        if (parallel)
            vu_read_inter_output(DMA_CHANNEL_VIF1, row + 4u,
                                 output, output_stride);
        row += parallel ? 8u : 4u;
    }
    return 0;
}

void crtube_vu_inter_luma_set_enabled(int enabled)
{
    vu_runtime_enabled = enabled && vu_runtime_ready;
}

void crtube_vu_inter_luma_reset_stats(void)
{
    vu_inter_calls = 0u;
    vu_inter_rows = 0u;
    vu_inter_parallel_batches = 0u;
}

void crtube_vu_inter_luma_log_stats(void)
{
    crtube_log("H.264 VU inter_luma calls=%u rows=%u parallel_batches=%u enabled=%u\n",
               vu_inter_calls, vu_inter_rows, vu_inter_parallel_batches,
               (unsigned)vu_runtime_enabled);
}

int crtube_vu_parallel_selftest(void)
{
    packet2_t *upload0 = NULL, *upload1 = NULL;
    packet2_t *packet0 = NULL, *packet1 = NULL;
    u32 ee_us, vu0_min = 0xffffffffu, vu1_min = 0xffffffffu;
    u32 parallel_min = 0xffffffffu, baseline_min = 0xffffffffu;
    u64 baseline_sum = 0, vu0_sum = 0, vu1_sum = 0, parallel_sum = 0;
    unsigned round;
    int result = -1;

    vu_dma_timeout = 0;
    crtube_log("VU parallel test: begin kernel=H264_6TAP_FLOAT groups=16 lanes=4 repeats=64 input=V4_8\n");
    crtube_log("VU architecture: VU0 micro via VIF0; VU1 micro via VIF1; EE integer reference\n");
    crtube_log("VU kernel: raw six-tap accumulator only; V4_8 unpack, ITOF0, FMAC, EE readback; no round/clip\n");
    scr_printf("VU test: initializing VIF0/VIF1...\n");

    crtube_log("VU stage: prepare input begin\n");
    for (round = 0; round < 3u; ++round)
        vu_make_input(&vu_data[round], round + 1u);
    crtube_log("VU stage: prepare input complete\n");

    crtube_log("VU stage: initialize VIF0 begin\n");
    if (dma_channel_initialize(DMA_CHANNEL_VIF0, NULL, 0) != 0) {
        crtube_log("VU parallel test: VIF0 DMA channel initialization failed\n");
        goto done;
    }
    crtube_log("VU stage: initialize VIF0 complete\n");
    crtube_log("VU stage: initialize VIF1 begin\n");
    if (dma_channel_initialize(DMA_CHANNEL_VIF1, NULL, 0) != 0) {
        crtube_log("VU parallel test: VIF1 DMA channel initialization failed\n");
        goto done;
    }
    crtube_log("VU stage: initialize VIF1 complete\n");
    dma_channel_fast_waits(DMA_CHANNEL_VIF0);
    dma_channel_fast_waits(DMA_CHANNEL_VIF1);
    crtube_log("VU stage: fast waits configured\n");

    crtube_log("VU stage: packet allocation begin\n");
    upload0 = packet2_create(VU_UPLOAD_QWORDS, P2_TYPE_NORMAL, P2_MODE_CHAIN, 1);
    upload1 = packet2_create(VU_UPLOAD_QWORDS, P2_TYPE_NORMAL, P2_MODE_CHAIN, 1);
    packet0 = packet2_create(VU_PACKET_QWORDS, P2_TYPE_NORMAL, P2_MODE_CHAIN, 1);
    packet1 = packet2_create(VU_PACKET_QWORDS, P2_TYPE_NORMAL, P2_MODE_CHAIN, 1);
    crtube_log("VU stage: packet allocation complete u0=%p u1=%p p0=%p p1=%p\n",
               upload0, upload1, packet0, packet1);
    if (!upload0 || !upload1 || !packet0 || !packet1) {
        crtube_log("VU parallel test: packet allocation failed\n");
        goto done;
    }

    crtube_log("VU stage: upload VU0 program begin\n");
    if (vu_upload_program(upload0, DMA_CHANNEL_VIF0) != 0) {
        crtube_log("VU parallel test: VU0 microprogram upload failed\n");
        scr_printf("VU test: VU0 microprogram upload FAILED\n");
        goto done;
    }
    crtube_log("VU stage: upload VU0 program complete\n");
    crtube_log("VU stage: upload VU1 program begin\n");
    if (vu_upload_program(upload1, DMA_CHANNEL_VIF1) != 0) {
        crtube_log("VU parallel test: VU1 microprogram upload failed\n");
        scr_printf("VU test: VU1 microprogram upload FAILED\n");
        goto done;
    }
    crtube_log("VU stage: upload VU1 program complete\n");
    scr_printf("VU test: microprograms uploaded\n");

    /* Warm up each VU and verify the exact integer-valued six-tap sums. */
    vu_scalar_kernel(&vu_data[0], scalar_result[0]);
    crtube_log("VU stage: VU0 warmup compute begin\n");
    if (vu_send_compute(packet0, DMA_CHANNEL_VIF0, &vu_data[0]) != 0) {
        crtube_log("VU0 six-tap self-test: FAIL\n");
        goto done;
    }
    crtube_log("VU stage: VU0 DMEM readback begin address=0x11004000 qword=%u\n",
               VU_OUTPUT_QWORD);
    vu_dump_dmem_words((volatile u32 *)VU0_DMEM_EE_ADDRESS, "VU0", 0u);
    vu_dump_dmem_words((volatile u32 *)VU0_DMEM_EE_ADDRESS, "VU0", 96u);
    vu_dump_dmem_words((volatile u32 *)VU0_DMEM_EE_ADDRESS, "VU0", 102u);
    vu_dump_dmem_words((volatile u32 *)VU0_DMEM_EE_ADDRESS, "VU0", 117u);
    vu_copy_output((volatile float *)VU0_DMEM_EE_ADDRESS, vu_readback[0]);
    crtube_log("VU stage: VU0 DMEM readback complete first=%d,%d,%d,%d\n",
               (int)vu_readback[0][0][0], (int)vu_readback[0][0][1],
               (int)vu_readback[0][0][2], (int)vu_readback[0][0][3]);
    crtube_log("VU stage: VU0 compare begin\n");
    if (vu_compare_output(vu_readback[0], scalar_result[0], "VU0") != 0) {
        crtube_log("VU0 six-tap self-test: FAIL\n");
        scr_printf("VU0 arithmetic: FAIL\n");
        goto done;
    }
    crtube_log("VU stage: VU0 compare complete\n");
    scr_printf("VU0 arithmetic: PASS\n");
    crtube_log("VU stage: VU1 warmup compute begin\n");
    if (vu_send_compute(packet1, DMA_CHANNEL_VIF1, &vu_data[0]) != 0) {
        crtube_log("VU1 six-tap self-test: FAIL\n");
        goto done;
    }
    crtube_log("VU stage: VU1 DMEM readback begin address=0x1100C000 qword=%u\n",
               VU_OUTPUT_QWORD);
    vu_dump_dmem_words((volatile u32 *)VU1_DMEM_EE_ADDRESS, "VU1", 0u);
    vu_dump_dmem_words((volatile u32 *)VU1_DMEM_EE_ADDRESS, "VU1", 96u);
    vu_dump_dmem_words((volatile u32 *)VU1_DMEM_EE_ADDRESS, "VU1", 102u);
    vu_dump_dmem_words((volatile u32 *)VU1_DMEM_EE_ADDRESS, "VU1", 117u);
    vu_copy_output((volatile float *)VU1_DMEM_EE_ADDRESS, vu_readback[1]);
    crtube_log("VU stage: VU1 DMEM readback complete first=%d,%d,%d,%d\n",
               (int)vu_readback[1][0][0], (int)vu_readback[1][0][1],
               (int)vu_readback[1][0][2], (int)vu_readback[1][0][3]);
    crtube_log("VU stage: VU1 compare begin\n");
    if (vu_compare_output(vu_readback[1], scalar_result[0], "VU1") != 0) {
        crtube_log("VU1 six-tap self-test: FAIL\n");
        scr_printf("VU1 arithmetic: FAIL\n");
        goto done;
    }
    crtube_log("VU stage: VU1 compare complete\n");
    scr_printf("VU1 arithmetic: PASS\n");
    crtube_log("VU0 six-tap self-test: PASS\n");
    crtube_log("VU1 six-tap self-test: PASS\n");
    scr_printf("VU0/VU1 arithmetic: PASS\n");

    /* Compare equal work: EE alone computes 3 batches; hybrid modes split it. */
    for (round = 0; round < VU_TEST_ROUNDS; ++round) {
        u32 value = vu_bench_ee_three();
        if (value < baseline_min) baseline_min = value;
        baseline_sum += value;
    }
    for (round = 0; round < VU_TEST_ROUNDS; ++round) {
        u32 value = vu_bench_vu0_plus_ee(packet0, &ee_us);
        if (value == 0xffffffffu) goto done;
        if (value < vu0_min) vu0_min = value;
        vu0_sum += value;
    }
    for (round = 0; round < VU_TEST_ROUNDS; ++round) {
        u32 value = vu_bench_vu1_plus_ee(packet1, &ee_us);
        if (value == 0xffffffffu) goto done;
        if (value < vu1_min) vu1_min = value;
        vu1_sum += value;
    }
    for (round = 0; round < VU_TEST_ROUNDS; ++round) {
        u32 value = vu_bench_all_parallel(packet0, packet1);
        if (value == 0xffffffffu) goto done;
        if (value < parallel_min) parallel_min = value;
        parallel_sum += value;
    }

    crtube_log("VU BENCH EE_ONLY min_us=%u avg_us=%u work=3x4096_outputs\n",
               baseline_min, (u32)(baseline_sum / VU_TEST_ROUNDS));
    crtube_log("VU BENCH EE+VU0 min_us=%u avg_us=%u work=2x4096_EE+1x4096_VU0\n",
               vu0_min, (u32)(vu0_sum / VU_TEST_ROUNDS));
    crtube_log("VU BENCH EE+VU1 min_us=%u avg_us=%u work=2x4096_EE+1x4096_VU1\n",
               vu1_min, (u32)(vu1_sum / VU_TEST_ROUNDS));
    crtube_log("VU BENCH EE+VU0+VU1 min_us=%u avg_us=%u work=1x4096_each_engine\n",
               parallel_min, (u32)(parallel_sum / VU_TEST_ROUNDS));
    crtube_log("VU BENCH note=includes_VIF_DMA_MSCAL_FLUSH_and_EE_readback\n");
    scr_printf("VU benchmarks complete\n");

    if (vu0_min < baseline_min || vu1_min < baseline_min || parallel_min < baseline_min)
        crtube_log("VU parallel test: at least one hybrid mode improved throughput\n");
    else
        crtube_log("VU parallel test: no hybrid mode beat EE-only in this workload\n");
    /*
     * Validate the exact memory layout used by edge264_inter.c. The two
     * independent 4-row bands must match the EE integer reference exactly.
     */
    crtube_log("VU runtime: uploading single-pass decoder microprograms\\n");
    if (vu_upload_program_once(upload0, DMA_CHANNEL_VIF0) != 0 ||
        vu_upload_program_once(upload1, DMA_CHANNEL_VIF1) != 0) {
        crtube_log("VU runtime: single-pass microprogram upload failed\\n");
        goto done;
    }
    vu_runtime_packet[0] = packet0;
    vu_runtime_packet[1] = packet1;
    vu_runtime_ready = 1;
    vu_runtime_enabled = 1;
    crtube_vu_inter_luma_reset_stats();
    {
        u8 source[13][16] __attribute__((aligned(64)));
        s16 got[8][16] __attribute__((aligned(64)));
        unsigned r, c, tap;
        for (r = 0; r < 13u; ++r)
            for (c = 0; c < 16u; ++c)
                source[r][c] = (u8)((r * 37u + c * 19u + (r * c) * 3u) & 255u);
        if (crtube_vu_inter_luma_vertical(&source[2][0], 16u, 8u,
                                          &got[0][0], 16u) != 0) {
            crtube_log("VU inter luma layout test: FAIL (dispatch)\n");
            goto done;
        }
        for (r = 0; r < 8u; ++r) {
            for (c = 0; c < 16u; ++c) {
                int expected = 0;
                for (tap = 0; tap < 6u; ++tap)
                    expected += (int)source[r + tap][c] *
                                filter_coefficients_int[tap];
                if (got[r][c] != expected) {
                    crtube_log("VU inter luma layout mismatch row=%u col=%u got=%d expected=%d\n",
                               r, c, (int)got[r][c], expected);
                    goto done;
                }
            }
        }
        crtube_log("VU inter luma layout test: PASS rows=8 width=16 engines=VU0+VU1\n");
    }

    result = 0;

done:
    vu_runtime_enabled = 0;
    if (vu_dma_timeout) {
        crtube_log("VU cleanup: DMA timed out; retaining packet buffers to avoid freeing in-flight data\n");
    } else {
        if (result == 0) {
            vu_runtime_packet[0] = packet0;
            vu_runtime_packet[1] = packet1;
            vu_runtime_ready = 1;
            packet0 = NULL;
            packet1 = NULL;
        } else {
            vu_runtime_packet[0] = NULL;
            vu_runtime_packet[1] = NULL;
            vu_runtime_ready = 0;
        }
        if (packet0) packet2_free(packet0);
        if (packet1) packet2_free(packet1);
        if (upload0) packet2_free(upload0);
        if (upload1) packet2_free(upload1);
    }
    crtube_log("VU parallel test: %s\n", result == 0 ? "PASS" : "FAIL");
    return result;
}
