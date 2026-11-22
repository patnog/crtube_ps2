#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <malloc.h>
#include <jpeglib.h>

#include "images.h"
#include "screen.h"
#include "youtube_search.h"
#include "http.h"
#include "debug_log.h"

#define CRTUBE_THUMBNAIL_URL_SIZE 160
#define CRTUBE_THUMBNAIL_MAX_BYTES (64u * 1024u)
#define CRTUBE_THUMBNAIL_DIMENSION 128
#define CRTUBE_THUMBNAIL_DISPLAY_W 160
#define CRTUBE_THUMBNAIL_DISPLAY_H 120

typedef struct crtube_jpeg_error {
    struct jpeg_error_mgr base;
    jmp_buf jump;
} crtube_jpeg_error_t;

typedef struct crtube_jpeg_memory_source {
    struct jpeg_source_mgr base;
    const JOCTET *data;
    size_t size;
    JOCTET eoi[2];
} crtube_jpeg_memory_source_t;

static crtube_screen_texture_t g_home_textures[CRTUBE_YOUTUBE_MAX_HOME_RESULTS];
static char g_home_texture_ids[CRTUBE_YOUTUBE_MAX_HOME_RESULTS][CRTUBE_YOUTUBE_VIDEO_ID_SIZE];
static int g_home_texture_ready[CRTUBE_YOUTUBE_MAX_HOME_RESULTS];
static unsigned int g_decode_pixels[CRTUBE_THUMBNAIL_DIMENSION *
                                CRTUBE_THUMBNAIL_DIMENSION]
                                __attribute__((aligned(128)));

static void jpeg_error_exit(j_common_ptr common)
{
    crtube_jpeg_error_t *error =
        (crtube_jpeg_error_t *)common->err;
    longjmp(error->jump, 1);
}

static void jpeg_memory_init_source(j_decompress_ptr decoder)
{
    (void)decoder;
}

static boolean jpeg_memory_fill_input_buffer(j_decompress_ptr decoder)
{
    crtube_jpeg_memory_source_t *source =
        (crtube_jpeg_memory_source_t *)decoder->src;

    source->eoi[0] = 0xFF;
    source->eoi[1] = JPEG_EOI;
    source->base.next_input_byte = source->eoi;
    source->base.bytes_in_buffer = 2;
    return TRUE;
}

static void jpeg_memory_skip_input_data(j_decompress_ptr decoder,
                                        long byte_count)
{
    crtube_jpeg_memory_source_t *source =
        (crtube_jpeg_memory_source_t *)decoder->src;

    if (byte_count <= 0)
        return;

    if ((size_t)byte_count > source->base.bytes_in_buffer) {
        size_t remaining = (size_t)byte_count -
                           source->base.bytes_in_buffer;
        source->base.next_input_byte += source->base.bytes_in_buffer;
        source->base.bytes_in_buffer = 0;
        if (remaining != 0)
            (void)jpeg_memory_fill_input_buffer(decoder);
    } else {
        source->base.next_input_byte += byte_count;
        source->base.bytes_in_buffer -= (size_t)byte_count;
    }
}

static void jpeg_memory_term_source(j_decompress_ptr decoder)
{
    (void)decoder;
}

static void jpeg_memory_source(j_decompress_ptr decoder,
                               const unsigned char *data, size_t size,
                               crtube_jpeg_memory_source_t *source)
{
    memset(source, 0, sizeof(*source));
    source->data = data;
    source->size = size;
    source->base.init_source = jpeg_memory_init_source;
    source->base.fill_input_buffer = jpeg_memory_fill_input_buffer;
    source->base.skip_input_data = jpeg_memory_skip_input_data;
    source->base.resync_to_restart = jpeg_resync_to_restart;
    source->base.term_source = jpeg_memory_term_source;
    source->base.next_input_byte = source->data;
    source->base.bytes_in_buffer = source->size;
    decoder->src = &source->base;
}

/* Decode the small baseline JPEG into a fixed 128x128 RGBA buffer.
   The source thumbnails are requested as YouTube's 120x90 default.jpg. */
static int decode_thumbnail_jpeg(const unsigned char *data, size_t size,
                                 int *width_out, int *height_out)
{
    struct jpeg_decompress_struct *decoder;
    crtube_jpeg_error_t error;
    crtube_jpeg_memory_source_t source;
    JSAMPLE * volatile row = NULL;
    volatile int decoder_created = 0;
    volatile int result = -1;
    JDIMENSION x;
    JDIMENSION y;

    if (data == NULL || size < 4 || width_out == NULL || height_out == NULL ||
        data[0] != 0xFF || data[1] != 0xD8)
        return -1;

    decoder = (struct jpeg_decompress_struct *)calloc(1, sizeof(*decoder));
    if (decoder == NULL)
        return -1;

    decoder->err = jpeg_std_error(&error.base);
    error.base.error_exit = jpeg_error_exit;

    if (setjmp(error.jump) != 0) {
        if (decoder_created)
            jpeg_destroy_decompress(decoder);
        free((void *)row);
        free(decoder);
        return -1;
    }

    jpeg_create_decompress(decoder);
    decoder_created = 1;
    jpeg_memory_source(decoder, data, size, &source);

    if (jpeg_read_header(decoder, TRUE) != JPEG_HEADER_OK)
        goto cleanup;

    decoder->out_color_space = JCS_RGB;
    decoder->scale_num = 1;
    decoder->scale_denom = 1;

    if (!jpeg_start_decompress(decoder) ||
        decoder->output_components != 3 ||
        decoder->output_width == 0 || decoder->output_height == 0 ||
        decoder->output_width > CRTUBE_THUMBNAIL_DIMENSION ||
        decoder->output_height > CRTUBE_THUMBNAIL_DIMENSION)
        goto cleanup;

    row = (JSAMPLE *)malloc((size_t)decoder->output_width * 3u);
    if (row == NULL)
        goto cleanup;

    memset(g_decode_pixels, 0, sizeof(g_decode_pixels));
    while (decoder->output_scanline < decoder->output_height) {
        JSAMPROW row_pointer = (JSAMPLE *)row;
        y = decoder->output_scanline;
        if (jpeg_read_scanlines(decoder, &row_pointer, 1) != 1)
            goto cleanup;

        for (x = 0; x < decoder->output_width; x++) {
            unsigned int red = row[x * 3u + 0u];
            unsigned int green = row[x * 3u + 1u];
            unsigned int blue = row[x * 3u + 2u];
            /* GS PSMCT32 uses R,G,B,A byte order in EE memory. */
            g_decode_pixels[(size_t)y * CRTUBE_THUMBNAIL_DIMENSION + x] =
                0x80000000u | (blue << 16) | (green << 8) | red;
        }
    }

    if (!jpeg_finish_decompress(decoder))
        goto cleanup;

    *width_out = (int)decoder->output_width;
    *height_out = (int)decoder->output_height;
    result = 0;

cleanup:
    free((void *)row);
    if (decoder_created)
        jpeg_destroy_decompress(decoder);
    free(decoder);
    return (int)result;
}

static char *find_http_body(char *response)
{
    char *p;
    if (response == NULL)
        return NULL;

    for (p = response; p[0] != '\0' && p[1] != '\0' &&
                       p[2] != '\0' && p[3] != '\0'; p++) {
        if (p[0] == '\r' && p[1] == '\n' &&
            p[2] == '\r' && p[3] == '\n')
            return p + 4;
    }
    return NULL;
}

static void draw_title(int x, int y, const char *title)
{
    char line1[24];
    char line2[24];
    size_t length;
    size_t split;
    const char *space;

    if (title == NULL)
        title = "";

    length = strlen(title);
    if (length > 44)
        length = 44;

    split = length > 22 ? 22 : length;
    if (length > 22) {
        space = NULL;
        {
            size_t i;
            for (i = 0; i < 22; i++)
                if (title[i] == ' ')
                    space = title + i;
        }
        if (space != NULL && (size_t)(space - title) >= 12)
            split = (size_t)(space - title);
    }

    memcpy(line1, title, split);
    line1[split] = '\0';

    if (length > split) {
        size_t rest = length - split;
        const char *second = title + split;
        while (*second == ' ')
            second++;
        rest = strlen(second);
        if (rest > 22)
            rest = 22;
        memcpy(line2, second, rest);
        line2[rest] = '\0';
    } else {
        line2[0] = '\0';
    }

    if (strlen(title) > 44 && strlen(line2) >= 3) {
        line2[19] = '.';
        line2[20] = '.';
        line2[21] = '.';
        line2[22] = '\0';
    }

    crtube_screen_draw_text(x, y, 0x00ffffffu, line1);
    crtube_screen_draw_text(x, y + 12, 0x00ffffffu, line2);
}

int crtube_images_load_home_results(void)
{
    const crtube_youtube_video_t *videos;
    size_t count = 0;
    size_t i;
    int loaded = 0;

    videos = crtube_youtube_get_home_results(&count);
    if (videos == NULL || count == 0)
        return 0;
    if (count > CRTUBE_YOUTUBE_MAX_HOME_RESULTS)
        count = CRTUBE_YOUTUBE_MAX_HOME_RESULTS;

    for (i = 0; i < count; i++) {
        char url[CRTUBE_THUMBNAIL_URL_SIZE];
        char *response = NULL;
        char *body;
        size_t response_size = 0;
        size_t body_size;
        int image_width = 0;
        int image_height = 0;
        int http_result;

        if (g_home_texture_ready[i] &&
            strcmp(g_home_texture_ids[i], videos[i].video_id) == 0) {
            loaded++;
            continue;
        }

        if (snprintf(url, sizeof(url),
                     "https://i.ytimg.com/vi/%s/default.jpg",
                     videos[i].video_id) >= (int)sizeof(url))
            continue;

        crtube_log("YouTube thumbnail %u: GET %s\n",
                   (unsigned int)i, url);
        http_result = crtube_http_get_url_alloc(url, &response,
                                                &response_size);
        if (http_result != CRTUBE_HTTP_OK ||
            crtube_http_last_status() != 200) {
            crtube_log("YouTube thumbnail %u: HTTP failed result=%d status=%d stage=%d errno=%d\n",
                       (unsigned int)i, http_result,
                       crtube_http_last_status(),
                       crtube_http_last_stage(),
                       crtube_http_last_errno());
            free(response);
            continue;
        }

        if (response == NULL) {
            crtube_log("YouTube thumbnail %u: empty HTTP response\n",
                       (unsigned int)i);
            continue;
        }
        body = find_http_body(response);
        if (body == NULL || response_size < (size_t)(body - response)) {
            crtube_log("YouTube thumbnail %u: malformed HTTP response\n",
                       (unsigned int)i);
            free(response);
            continue;
        }

        body_size = response_size - (size_t)(body - response);
        if (body_size < 4 || body_size > CRTUBE_THUMBNAIL_MAX_BYTES) {
            crtube_log("YouTube thumbnail %u: invalid JPEG size=%u\n",
                       (unsigned int)i, (unsigned int)body_size);
            free(response);
            continue;
        }
        if (decode_thumbnail_jpeg((const unsigned char *)body, body_size,
                                  &image_width, &image_height) != 0) {
            crtube_log("YouTube thumbnail %u: JPEG decode failed bytes=%u\n",
                       (unsigned int)i, (unsigned int)body_size);
            free(response);
            continue;
        }
        free(response);
        response = NULL;

        if (crtube_screen_texture_create(&g_home_textures[i], g_decode_pixels,
                                         image_width, image_height) != 0) {
            crtube_log("YouTube thumbnail %u: GS texture upload failed\n",
                       (unsigned int)i);
            continue;
        }

        strncpy(g_home_texture_ids[i], videos[i].video_id,
                sizeof(g_home_texture_ids[i]) - 1);
        g_home_texture_ids[i][sizeof(g_home_texture_ids[i]) - 1] = '\0';
        g_home_texture_ready[i] = 1;
        loaded++;

        crtube_log("YouTube thumbnail %u: decoded=%dx%d jpeg_bytes=%u GS texture ready\n",
                   (unsigned int)i, image_width, image_height,
                   (unsigned int)body_size);
    }

    return loaded;
}

void crtube_images_draw_home_grid(void)
{
    const crtube_youtube_video_t *videos;
    size_t count = 0;
    size_t i;

    videos = crtube_youtube_get_home_results(&count);
    if (videos == NULL || count == 0) {
        crtube_screen_draw_text(48, 110, 0x00ffffffu,
                                "Run Network Status test to load videos.");
        return;
    }

    if (count > CRTUBE_YOUTUBE_MAX_HOME_RESULTS)
        count = CRTUBE_YOUTUBE_MAX_HOME_RESULTS;

    for (i = 0; i < count; i++) {
        int column = (int)(i % 2);
        int row = (int)(i / 2);
        int x = column == 0 ? 54 : 344;
        int y = row == 0 ? 58 : 238;

        if (g_home_texture_ready[i] &&
            strcmp(g_home_texture_ids[i], videos[i].video_id) == 0) {
            crtube_screen_texture_draw(&g_home_textures[i], x, y,
                                       CRTUBE_THUMBNAIL_DISPLAY_W,
                                       CRTUBE_THUMBNAIL_DISPLAY_H);
        } else {
            crtube_screen_draw_text(x, y + 52, 0x00ffffffu,
                                    "Thumbnail unavailable");
        }
        crtube_screen_draw_rect_outline(x - 2, y - 2,
                                        CRTUBE_THUMBNAIL_DISPLAY_W + 4,
                                        CRTUBE_THUMBNAIL_DISPLAY_H + 4);
    }

    crtube_screen_flush();

    for (i = 0; i < count; i++) {
        int column = (int)(i % 2);
        int row = (int)(i / 2);
        int x = column == 0 ? 54 : 344;
        int y = row == 0 ? 182 : 362;

        draw_title(x, y, videos[i].title);
        crtube_screen_draw_text(x, y + 25, 0x0080c0ffu,
                                videos[i].channel);
    }
}
