#include "h264_edge264.h"
#include "h264_mp4.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct test_state {
    unsigned frames;
    unsigned invalid;
} test_state_t;

static int test_frame(const crtube_h264_edge264_frame_t *frame, void *opaque)
{
    test_state_t *state = (test_state_t *)opaque;

    if (!frame || !frame->y || !frame->cb || !frame->cr ||
        frame->width != 320u || frame->height != 240u ||
        frame->chroma_width != 160u || frame->chroma_height != 120u ||
        frame->bit_depth_y != 8u || frame->bit_depth_c != 8u ||
        frame->stride_y < 320u || frame->stride_c < 160u) {
        state->invalid++;
        return -1;
    }

    state->frames++;
    return 0;
}

int main(int argc, char **argv)
{
    FILE *input;
    uint8_t *data = NULL, *scratch = NULL;
    long file_size;
    size_t max_sample_size = 0, scratch_capacity;
    crtube_mp4_t mp4;
    crtube_mp4_sample_t sample;
    crtube_h264_edge264_t *decoder = NULL;
    crtube_h264_edge264_memory_stats_t memory;
    test_state_t state;
    unsigned i;
    int result = 1;

    memset(&mp4, 0, sizeof(mp4));
    memset(&state, 0, sizeof(state));
    memset(&memory, 0, sizeof(memory));

    if (argc != 2) {
        fprintf(stderr, "usage: %s input.mp4\n", argv[0]);
        return 2;
    }

    input = fopen(argv[1], "rb");
    if (!input) {
        perror("fopen");
        return 3;
    }
    if (fseek(input, 0, SEEK_END) != 0 ||
        (file_size = ftell(input)) <= 0 ||
        fseek(input, 0, SEEK_SET) != 0) {
        fclose(input);
        return 4;
    }

    data = (uint8_t *)malloc((size_t)file_size);
    if (!data || fread(data, 1, (size_t)file_size, input) !=
                 (size_t)file_size) {
        fclose(input);
        goto done;
    }
    fclose(input);

    if (crtube_mp4_open_memory(&mp4, data, (size_t)file_size) < 0 ||
        mp4.width != 320u || mp4.height != 240u || !mp4.sample_count)
        goto done;

    for (i = 0; i < mp4.sample_count; ++i) {
        if (crtube_mp4_get_sample(&mp4, i, &sample) < 0)
            goto done;
        if (sample.size > max_sample_size)
            max_sample_size = sample.size;
    }
    if (!max_sample_size || max_sample_size > (SIZE_MAX - 64u) / 2u)
        goto done;

    scratch_capacity = max_sample_size * 2u + 64u;
    scratch = (uint8_t *)malloc(scratch_capacity);
    decoder = crtube_h264_edge264_create();
    if (!scratch || !decoder)
        goto done;

    if (crtube_h264_edge264_open_mp4(
            decoder, &mp4, scratch, scratch_capacity) < 0)
        goto done;

    for (i = 0; i < mp4.sample_count; ++i) {
        if (crtube_h264_edge264_decode_mp4_sample(
                decoder, &mp4, i, scratch, scratch_capacity,
                test_frame, &state) < 0)
            goto done;
    }

    if (crtube_h264_edge264_finish(decoder, test_frame, &state) < 0)
        goto done;

    crtube_h264_edge264_get_memory_stats(decoder, &memory);
    printf("MP4: %ux%u samples=%u bytes=%ld max_sample=%lu\n",
           mp4.width, mp4.height, mp4.sample_count, file_size,
           (unsigned long)max_sample_size);
    printf("Edge264: frames=%u invalid=%u peak=%lu allocations=%u failures=%u\n",
           state.frames, state.invalid, (unsigned long)memory.peak_bytes,
           memory.allocation_count, memory.allocation_failures);

    if (state.frames != mp4.sample_count || state.invalid ||
        memory.allocation_failures ||
        memory.peak_bytes > memory.allocation_limit)
        goto done;

    puts("H.264 MP4 baseline host test: PASS");
    result = 0;

done:
    crtube_h264_edge264_destroy(decoder);
    free(scratch);
    free(data);
    if (result)
        puts("H.264 MP4 baseline host test: FAIL");
    return result;
}
