#include "h264_edge264.h"
#include "h264_mp4.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef struct dump_state {
    FILE *out;
    unsigned frames;
    unsigned width, height, chroma_width, chroma_height;
    int failed;
} dump_state_t;

static int dump_frame(const crtube_h264_edge264_frame_t *f, void *opaque)
{
    dump_state_t *s = (dump_state_t *)opaque;
    unsigned y;
    if (!f || !f->y || !f->cb || !f->cr ||
        f->width != s->width || f->height != s->height ||
        f->chroma_width != s->chroma_width ||
        f->chroma_height != s->chroma_height ||
        f->bit_depth_y != 8u || f->bit_depth_c != 8u ||
        f->stride_y < f->width || f->stride_c < f->chroma_width) {
        fprintf(stderr, "invalid frame layout at output frame %u\n", s->frames);
        s->failed = 1;
        return -1;
    }
    for (y = 0; y < f->height; ++y)
        if (fwrite(f->y + (size_t)y * f->stride_y, 1, f->width, s->out) != f->width)
            goto io_error;
    for (y = 0; y < f->chroma_height; ++y)
        if (fwrite(f->cb + (size_t)y * f->stride_c, 1, f->chroma_width, s->out) != f->chroma_width)
            goto io_error;
    for (y = 0; y < f->chroma_height; ++y)
        if (fwrite(f->cr + (size_t)y * f->stride_c, 1, f->chroma_width, s->out) != f->chroma_width)
            goto io_error;
    s->frames++;
    return 0;
io_error:
    fprintf(stderr, "write failed at output frame %u\n", s->frames);
    s->failed = 1;
    return -1;
}

int main(int argc, char **argv)
{
    FILE *in = NULL;
    dump_state_t state;
    crtube_mp4_t mp4;
    crtube_mp4_sample_t sample;
    crtube_h264_edge264_t *decoder = NULL;
    uint8_t *data = NULL, *scratch = NULL;
    long file_size;
    size_t max_sample = 0, scratch_size;
    unsigned i;
    int rc = 1;

    memset(&mp4, 0, sizeof(mp4));
    memset(&state, 0, sizeof(state));
    if (argc != 3) {
        fprintf(stderr, "usage: %s input.mp4 output.yuv\n", argv[0]);
        return 2;
    }
    in = fopen(argv[1], "rb");
    if (!in || fseek(in, 0, SEEK_END) || (file_size = ftell(in)) <= 0 ||
        fseek(in, 0, SEEK_SET)) goto cleanup;
    data = (uint8_t *)malloc((size_t)file_size);
    if (!data || fread(data, 1, (size_t)file_size, in) != (size_t)file_size)
        goto cleanup;
    fclose(in); in = NULL;
    if (crtube_mp4_open_memory(&mp4, data, (size_t)file_size) < 0 ||
        !mp4.width || !mp4.height || (mp4.width & 1u) || (mp4.height & 1u) ||
        !mp4.sample_count) {
        fprintf(stderr, "unsupported MP4 dimensions or empty video track\n");
        goto cleanup;
    }
    state.width = mp4.width;
    state.height = mp4.height;
    state.chroma_width = mp4.width / 2u;
    state.chroma_height = mp4.height / 2u;
    for (i = 0; i < mp4.sample_count; ++i) {
        if (crtube_mp4_get_sample(&mp4, i, &sample) < 0) goto cleanup;
        if (sample.size > max_sample) max_sample = sample.size;
    }
    if (!max_sample || max_sample > (SIZE_MAX - 64u) / 2u) goto cleanup;
    scratch_size = max_sample * 2u + 64u;
    scratch = (uint8_t *)malloc(scratch_size);
    state.out = fopen(argv[2], "wb");
    decoder = crtube_h264_edge264_create();
    if (!scratch || !state.out || !decoder ||
        crtube_h264_edge264_open_mp4(decoder, &mp4, scratch, scratch_size) < 0)
        goto cleanup;
    for (i = 0; i < mp4.sample_count; ++i) {
        if (crtube_h264_edge264_decode_mp4_sample(decoder, &mp4, i,
                scratch, scratch_size, dump_frame, &state) < 0)
            goto cleanup;
    }
    if (crtube_h264_edge264_finish(decoder, dump_frame, &state) < 0 || state.failed)
        goto cleanup;
    if (fflush(state.out) != 0) goto cleanup;
    fprintf(stderr, "decoded frames=%u dimensions=%ux%u chroma=%ux%u\n",
            state.frames, state.width, state.height,
            state.chroma_width, state.chroma_height);
    rc = 0;
cleanup:
    if (in) fclose(in);
    if (state.out) fclose(state.out);
    crtube_h264_edge264_destroy(decoder);
    free(scratch);
    free(data);
    if (rc != 0) remove(argv[2]);
    return rc;
}
