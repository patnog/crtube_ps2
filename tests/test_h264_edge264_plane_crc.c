#include "h264_edge264.h"
#include "h264_mp4.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef struct plane_test_state { unsigned frames, invalid; } plane_test_state_t;

static uint32_t plane_crc32(const uint8_t *plane, unsigned width,
                            unsigned height, unsigned stride)
{
    uint32_t crc = 0xffffffffu;
    unsigned row, col, bit;
    for (row = 0; row < height; ++row) {
        const uint8_t *line = plane + (size_t)row * stride;
        for (col = 0; col < width; ++col) {
            crc ^= line[col];
            for (bit = 0; bit < 8u; ++bit)
                crc = (crc >> 1) ^
                    (0xedb88320u & (uint32_t)-(int32_t)(crc & 1u));
        }
    }
    return crc ^ 0xffffffffu;
}

static int frame_cb(const crtube_h264_edge264_frame_t *f, void *opaque)
{
    plane_test_state_t *state = (plane_test_state_t *)opaque;
    if (!f || !f->y || !f->cb || !f->cr || f->width != 320u ||
        f->height != 240u || f->chroma_width != 160u ||
        f->chroma_height != 120u || f->bit_depth_y != 8u ||
        f->bit_depth_c != 8u || f->stride_y < f->width ||
        f->stride_c < f->chroma_width) {
        state->invalid++;
        return -1;
    }
    state->frames++;
    printf("%u,%08lx,%08lx,%08lx\n", state->frames,
           (unsigned long)plane_crc32(f->y, f->width, f->height,
                                      f->stride_y),
           (unsigned long)plane_crc32(f->cb, f->chroma_width,
                                      f->chroma_height, f->stride_c),
           (unsigned long)plane_crc32(f->cr, f->chroma_width,
                                      f->chroma_height, f->stride_c));
    return 0;
}

int main(int argc, char **argv)
{
    FILE *in = NULL;
    uint8_t *data = NULL, *scratch = NULL;
    long file_size;
    size_t max_sample = 0, scratch_size;
    crtube_mp4_t mp4;
    crtube_mp4_sample_t sample;
    crtube_h264_edge264_t *decoder = NULL;
    plane_test_state_t state;
    unsigned i;
    int result = 1;

    memset(&mp4, 0, sizeof(mp4));
    memset(&state, 0, sizeof(state));
    if (argc != 2) return 2;
    in = fopen(argv[1], "rb");
    if (!in || fseek(in, 0, SEEK_END) || (file_size = ftell(in)) <= 0 ||
        fseek(in, 0, SEEK_SET)) goto done;
    data = (uint8_t *)malloc((size_t)file_size);
    if (!data || fread(data, 1, (size_t)file_size, in) != (size_t)file_size)
        goto done;
    fclose(in); in = NULL;
    if (crtube_mp4_open_memory(&mp4, data, (size_t)file_size) < 0 ||
        mp4.width != 320u || mp4.height != 240u || !mp4.sample_count)
        goto done;
    for (i = 0; i < mp4.sample_count; ++i) {
        if (crtube_mp4_get_sample(&mp4, i, &sample) < 0) goto done;
        if (sample.size > max_sample) max_sample = sample.size;
    }
    if (!max_sample || max_sample > (SIZE_MAX - 64u) / 2u) goto done;
    scratch_size = max_sample * 2u + 64u;
    scratch = (uint8_t *)malloc(scratch_size);
    decoder = crtube_h264_edge264_create();
    if (!scratch || !decoder || crtube_h264_edge264_open_mp4(
            decoder, &mp4, scratch, scratch_size) < 0) goto done;
    for (i = 0; i < mp4.sample_count; ++i)
        if (crtube_h264_edge264_decode_mp4_sample(decoder, &mp4, i,
                scratch, scratch_size, frame_cb, &state) < 0) goto done;
    if (crtube_h264_edge264_finish(decoder, frame_cb, &state) < 0 ||
        state.invalid || state.frames != mp4.sample_count) goto done;
    result = 0;
done:
    if (in) fclose(in);
    crtube_h264_edge264_destroy(decoder);
    free(scratch);
    free(data);
    return result;
}
