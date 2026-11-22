#include "h264_edge264.h"

#include <stdio.h>
#include <stdlib.h>

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t size)
{
    size_t i;
    for (i = 0; i < size; ++i) {
        unsigned bit;
        crc ^= data[i];
        for (bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return crc;
}

typedef struct { uint32_t crc; unsigned count; } test_state_t;

static int on_frame(const crtube_h264_edge264_frame_t *frame, void *opaque)
{
    test_state_t *state = (test_state_t *)opaque;
    uint32_t *crc = &state->crc;
    unsigned y;
    if (!frame || !frame->y || !frame->cb || !frame->cr ||
        frame->bit_depth_y != 8 || frame->bit_depth_c != 8 ||
        !frame->width || !frame->height ||
        frame->stride_y < frame->width ||
        frame->stride_c < frame->chroma_width)
        return -1;
    ++state->count;
    for (y = 0; y < frame->height; ++y)
        *crc = crc32_update(*crc, frame->y + y * frame->stride_y,
                            frame->width);
    for (y = 0; y < frame->chroma_height; ++y) {
        *crc = crc32_update(*crc, frame->cb + y * frame->stride_c,
                            frame->chroma_width);
        *crc = crc32_update(*crc, frame->cr + y * frame->stride_c,
                            frame->chroma_width);
    }
    return 0;
}

int main(int argc, char **argv)
{
    FILE *file;
    long length;
    uint8_t *data;
    crtube_h264_edge264_t *decoder;
    test_state_t state = {0xffffffffu, 0};
    int result;
    if (argc != 2)
        return 2;
    file = fopen(argv[1], "rb");
    if (!file)
        return 3;
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) <= 0 ||
        fseek(file, 0, SEEK_SET) != 0)
        return 4;
    data = (uint8_t *)malloc((size_t)length);
    if (!data || fread(data, 1, (size_t)length, file) != (size_t)length)
        return 5;
    fclose(file);
    decoder = crtube_h264_edge264_create();
    if (!decoder)
        return 6;
    result = crtube_h264_edge264_push_annexb(decoder, data, (size_t)length,
                                              on_frame, &state);
    if (result >= 0)
        result = crtube_h264_edge264_finish(decoder, on_frame, &state);
    crtube_h264_edge264_destroy(decoder);
    free(data);
    if (result < 0)
        return 7;
    state.crc ^= 0xffffffffu;
    printf("frames=%u crc=%08x\n", state.count, state.crc);
    if (state.count != 116u) return 8;
    return 0;
}
