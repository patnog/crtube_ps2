#include "h264_decoder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_WIDTH 320u
#define TEST_HEIGHT 240u

static const uint8_t bar_y[8] = {180, 162, 131, 112, 84, 65, 35, 16};
static const uint8_t bar_cb[8] = {128, 44, 156, 72, 184, 100, 212, 128};
static const uint8_t bar_cr[8] = {128, 142, 44, 58, 198, 212, 114, 128};

static int write_ppm(const char *path, const uint8_t *rgb,
                     unsigned width, unsigned height)
{
    FILE *file = fopen(path, "wb");
    if (!file) return -1;
    if (fprintf(file, "P6\n%u %u\n255\n", width, height) < 0 ||
        fwrite(rgb, 3u, (size_t)width * height, file) !=
            (size_t)width * height) {
        fclose(file);
        return -1;
    }
    return fclose(file) == 0 ? 0 : -1;
}

int main(int argc, char **argv)
{
    const char *output_path = argc > 1 ? argv[1] : "h264-test.ppm";
    const size_t frame_size = crtube_h264_frame_bytes(TEST_WIDTH, TEST_HEIGHT);
    const size_t rgb_size = (size_t)TEST_WIDTH * TEST_HEIGHT * 3u;
    uint8_t *storage = (uint8_t *)malloc(frame_size);
    uint8_t *rgb = (uint8_t *)malloc(rgb_size);
    crtube_h264_frame_t frame;
    unsigned mb_x, mb_y;

    if (crtube_h264_frame_bytes(TEST_WIDTH + 1u, TEST_HEIGHT) != 0u ||
        crtube_h264_frame_bytes(TEST_WIDTH, TEST_HEIGHT + 1u) != 0u) {
        fprintf(stderr, "invalid frame dimensions were accepted\n");
        return 1;
    }
    if (!frame_size || !storage || !rgb) {
        fprintf(stderr, "allocation failed\n");
        free(storage);
        free(rgb);
        return 1;
    }
    if (crtube_h264_frame_init(&frame, storage, frame_size,
                               TEST_WIDTH, TEST_HEIGHT) < 0) {
        fprintf(stderr, "frame init failed\n");
        free(storage);
        free(rgb);
        return 1;
    }
    crtube_h264_frame_clear(&frame, 16u, 128u, 128u);
    if (crtube_h264_frame_to_rgb24(&frame, rgb, rgb_size - 1u) == 0) {
        fprintf(stderr, "undersized RGB destination was accepted\n");
        free(storage);
        free(rgb);
        return 1;
    }
    {
        uint8_t dummy_y[256] = {0};
        uint8_t dummy_c[64] = {0};
        if (crtube_h264_frame_put_macroblock(&frame, TEST_WIDTH / 16u, 0,
                dummy_y, dummy_c, dummy_c) == 0) {
            fprintf(stderr, "out-of-range macroblock was accepted\n");
            free(storage);
            free(rgb);
            return 1;
        }
    }

    for (mb_y = 0; mb_y < TEST_HEIGHT / 16u; ++mb_y) {
        for (mb_x = 0; mb_x < TEST_WIDTH / 16u; ++mb_x) {
            uint8_t luma[256];
            uint8_t cb[64];
            uint8_t cr[64];
            unsigned x, y;
            unsigned bar = (mb_x * 16u * 8u) / TEST_WIDTH;
            if (bar > 7u) bar = 7u;

            for (y = 0; y < 16u; ++y) {
                for (x = 0; x < 16u; ++x) {
                    unsigned gx = mb_x * 16u + x;
                    unsigned gy = mb_y * 16u + y;
                    unsigned local_bar = (gx * 8u) / TEST_WIDTH;
                    int luma_value = (int)bar_y[local_bar];
                    if (((gx >> 3) ^ (gy >> 3)) & 1u)
                        luma_value += 8;
                    if (luma_value > 235) luma_value = 235;
                    luma[y * 16u + x] = (uint8_t)luma_value;
                }
            }
            for (y = 0; y < 8u; ++y) {
                for (x = 0; x < 8u; ++x) {
                    cb[y * 8u + x] = bar_cb[bar];
                    cr[y * 8u + x] = bar_cr[bar];
                }
            }
            if (crtube_h264_frame_put_macroblock(&frame, mb_x, mb_y,
                    luma, cb, cr) < 0) {
                fprintf(stderr, "macroblock copy failed at %u,%u\n",
                        mb_x, mb_y);
                free(storage);
                free(rgb);
                return 1;
            }
        }
    }

    if (crtube_h264_frame_to_rgb24(&frame, rgb, rgb_size) < 0) {
        fprintf(stderr, "YUV420 to RGB24 conversion failed\n");
        free(storage);
        free(rgb);
        return 1;
    }
    if (write_ppm(output_path, rgb, TEST_WIDTH, TEST_HEIGHT) < 0) {
        fprintf(stderr, "could not write %s\n", output_path);
        free(storage);
        free(rgb);
        return 1;
    }

    printf("Generated %s (%ux%u, YUV420 -> RGB24)\n",
           output_path, TEST_WIDTH, TEST_HEIGHT);
    free(storage);
    free(rgb);
    return 0;
}
