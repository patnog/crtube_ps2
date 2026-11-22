#include "h264_edge264.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct frame_stats {
    unsigned count;
    uint32_t last_display_order;
    uint32_t min_display_order;
    uint32_t max_display_order;
    int have_display_order;
} frame_stats_t;

static int on_frame(const crtube_h264_edge264_frame_t *frame, void *opaque)
{
    frame_stats_t *stats = (frame_stats_t *)opaque;
    uint64_t packed_poc;
    uint32_t display_order;
    if (!frame || !frame->y || !frame->cb || !frame->cr ||
        frame->bit_depth_y != 8 || frame->bit_depth_c != 8 ||
        !frame->width || !frame->height ||
        frame->stride_y < frame->width ||
        frame->stride_c < frame->chroma_width)
        return -1;

    packed_poc = (uint64_t)frame->display_poc;
    display_order = (uint32_t)(packed_poc >> 32);
    if (stats->have_display_order &&
        display_order < stats->last_display_order)
        return -2;
    stats->last_display_order = display_order;
    if (!stats->have_display_order ||
        display_order < stats->min_display_order)
        stats->min_display_order = display_order;
    if (!stats->have_display_order ||
        display_order > stats->max_display_order)
        stats->max_display_order = display_order;
    stats->have_display_order = 1;
    ++stats->count;
    return 0;
}

int main(int argc, char **argv)
{
    FILE *file;
    long length;
    uint8_t *data = NULL;
    crtube_h264_edge264_t *decoder = NULL;
    frame_stats_t frame_stats = {0};
    crtube_h264_edge264_memory_stats_t memory_stats;
    int result;
    int exit_code = 0;

    if (argc != 2)
        return 2;
    file = fopen(argv[1], "rb");
    if (!file)
        return 3;
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) <= 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 4;
    }
    data = (uint8_t *)malloc((size_t)length);
    if (!data || fread(data, 1, (size_t)length, file) != (size_t)length) {
        free(data);
        fclose(file);
        return 5;
    }
    fclose(file);

    decoder = crtube_h264_edge264_create();
    if (!decoder) {
        exit_code = 6;
        goto done;
    }

    result = crtube_h264_edge264_push_annexb(
        decoder, data, (size_t)length, on_frame, &frame_stats);
    if (result < 0) {
        fprintf(stderr, "push failed: %d\n", result);
        exit_code = 7;
        goto done;
    }
    result = crtube_h264_edge264_finish(decoder, on_frame, &frame_stats);
    if (result < 0 || frame_stats.count != 116u ||
        !frame_stats.have_display_order) {
        fprintf(stderr, "finish failed: %d frames=%u\n", result, frame_stats.count);
        exit_code = 8;
        goto done;
    }

    crtube_h264_edge264_get_memory_stats(decoder, &memory_stats);
    if (memory_stats.allocation_count == 0 || memory_stats.peak_bytes == 0 ||
        memory_stats.current_bytes > memory_stats.peak_bytes ||
        memory_stats.peak_bytes > memory_stats.allocation_limit) {
        fprintf(stderr, "invalid decoder memory accounting\n");
        exit_code = 9;
        goto done;
    }
    printf("edge264 adapter: %u frames, display_order=%u..%u, peak=%lu bytes\n",
           frame_stats.count, frame_stats.min_display_order,
           frame_stats.max_display_order,
           (unsigned long)memory_stats.peak_bytes);

done:
    crtube_h264_edge264_destroy(decoder);
    free(data);
    return exit_code;
}
