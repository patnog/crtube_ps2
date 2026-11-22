/*
 * Double-size text renderer adapted from the PS2SDK debug screen renderer.
 * Original PS2SDK code is Copyright 2001-2004 ps2dev and licensed under
 * Academic Free License 2.0. The 8x8 glyph bitmap is scaled to 16x16 here.
 */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <tamtypes.h>
#include <kernel.h>
#include <timer.h>
#include <ee_regs.h>
#include <debug.h>
#include <malloc.h>
#include <gsKit.h>
#include <dmaKit.h>

#include "screen.h"
#include "h264_yuv.h"
#include "debug_log.h"
#include <stdlib.h>
#undef scr_printf

#define CRTUBE_SCREEN_LINES 18
#define CRTUBE_SCREEN_CLEAR_ROWS 19
#define CRTUBE_SCREEN_COLS 53
#define CRTUBE_SCREEN_TEXT 2048
#define CRTUBE_GLYPH_WIDTH 12
#define CRTUBE_GLYPH_HEIGHT 12

static char screen_lines[CRTUBE_SCREEN_LINES][CRTUBE_SCREEN_COLS + 1];
static int screen_line_count;
static int screen_visible = 1;
static u32 screen_bgcolor;
static u32 screen_fontcolor = 0x00ffffff;
static GSGLOBAL *screen_gs;

extern const u8 msx[];

typedef struct screen_setupchar {
    u64 dd0[4];
    u32 dw0[1];
    u16 x;
    u16 y;
    u64 dd1[1];
    u32 dw1[2];
    u64 dd2[5];
} screen_setupchar_t;

static const screen_setupchar_t screen_setupchar_template = {
    {0x1000000000000004ULL, 0xE, 0xA000000000000ULL, 0x50},
    {0},
    100,
    100,
    {0x51},
    {12, 12},
    {0x52, 0, 0x53, 0x800000000008024ULL, 0}
};

static inline void screen_dma_wait(void)
{
    while ((*R_EE_D2_CHCR & 0x100) != 0)
        ;
}

static inline void screen_dma_start(const void *address, int qwords)
{
    *R_EE_D2_QWC = (u32)qwords;
    *R_EE_D2_MADR = (u32)address;
    *R_EE_D2_CHCR = 0x101;
}

static void screen_draw_glyph(int x, int y, u32 color, unsigned char ch)
{
    static screen_setupchar_t setupchar __attribute__((aligned(16)));
    static u32 charmap[CRTUBE_GLYPH_WIDTH * CRTUBE_GLYPH_HEIGHT] __attribute__((aligned(16)));
    screen_setupchar_t *uncached_setup;
    u32 *uncached_charmap;
    const u8 *font = &msx[(unsigned int)ch * 8];
    int row;
    int column;

    uncached_setup = (screen_setupchar_t *)UNCACHED_SEG(&setupchar);
    *uncached_setup = screen_setupchar_template;
    uncached_setup->x = (u16)x;
    uncached_setup->y = (u16)y;
    screen_dma_start(&setupchar, 6);
    screen_dma_wait();

    uncached_charmap = (u32 *)UNCACHED_SEG(charmap);
    for (row = 0; row < CRTUBE_GLYPH_HEIGHT; row++) {
        unsigned char bits = font[(row * 8) / CRTUBE_GLYPH_HEIGHT];
        for (column = 0; column < CRTUBE_GLYPH_WIDTH; column++) {
            u32 pixel = (bits & (0x80 >> ((column * 8) / CRTUBE_GLYPH_WIDTH))) ? color : screen_bgcolor;
            uncached_charmap[row * CRTUBE_GLYPH_WIDTH + column] = pixel;
        }
    }

    screen_dma_start(charmap, 36);
    screen_dma_wait();
}

static void screen_clear_hardware(void)
{
    static screen_setupchar_t setupchar __attribute__((aligned(16)));
    static u32 clear_buffer[640 * CRTUBE_GLYPH_HEIGHT]
        __attribute__((aligned(64)));
    screen_setupchar_t *uncached_setup;
    u32 *uncached_clear;
    int display_height = screen_gs != NULL ? screen_gs->Height :
                         CRTUBE_SCREEN_CLEAR_ROWS * CRTUBE_GLYPH_HEIGHT;
    int y;
    int pixel;

    uncached_setup = (screen_setupchar_t *)UNCACHED_SEG(&setupchar);
    uncached_clear = (u32 *)UNCACHED_SEG(clear_buffer);
    for (pixel = 0; pixel < 640 * CRTUBE_GLYPH_HEIGHT; pixel++)
        uncached_clear[pixel] = screen_bgcolor;

    for (y = 0; y < display_height; y += CRTUBE_GLYPH_HEIGHT) {
        int height = display_height - y;
        int qwords;

        if (height > CRTUBE_GLYPH_HEIGHT)
            height = CRTUBE_GLYPH_HEIGHT;
        qwords = (640 * height * (int)sizeof(u32)) / 16;

        *uncached_setup = screen_setupchar_template;
        uncached_setup->x = 0;
        uncached_setup->y = (u16)y;
        uncached_setup->dw1[0] = 640;
        uncached_setup->dw1[1] = (u32)height;
        uncached_setup->dd2[3] = 0x800000000008000ULL | (u64)qwords;
        screen_dma_start(&setupchar, 6);
        screen_dma_wait();
        screen_dma_start(clear_buffer, qwords);
        screen_dma_wait();
    }
}

static void screen_redraw(void)
{
    int row;
    int column;

    if (!screen_visible)
        return;

    screen_clear_hardware();
    for (row = 0; row < screen_line_count; row++) {
        for (column = 0; screen_lines[row][column] != '\0'; column++) {
            screen_draw_glyph(column * CRTUBE_GLYPH_WIDTH,
                              row * CRTUBE_GLYPH_HEIGHT,
                              screen_fontcolor,
                              (unsigned char)screen_lines[row][column]);
        }
    }
}

/* Returns nonzero when the scrollback buffer had to move. */
static int screen_add_line(void)
{
    if (screen_line_count < CRTUBE_SCREEN_LINES) {
        screen_line_count++;
        screen_lines[screen_line_count - 1][0] = '\0';
        return 0;
    }

    memmove(screen_lines[0], screen_lines[1],
            sizeof(screen_lines[0]) * (CRTUBE_SCREEN_LINES - 1));
    screen_lines[CRTUBE_SCREEN_LINES - 1][0] = '\0';
    return 1;
}

int crtube_screen_init(void)
{
    if (screen_gs != NULL)
        return 0;

    screen_gs = gsKit_init_global();
    if (screen_gs == NULL)
        return -1;

    /* Keep the existing text renderer's single framebuffer model. Text glyphs
       are still uploaded by its DMA path; gsKit owns texture allocation and
       textured sprite rendering on the same GS framebuffer. */
    screen_gs->PSM = GS_PSM_CT32;
    screen_gs->DoubleBuffering = GS_SETTING_OFF;
    screen_gs->ZBuffering = GS_SETTING_OFF;
    screen_gs->PrimAlphaEnable = GS_SETTING_OFF;

    dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC,
                D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
    dmaKit_chan_init(DMA_CHANNEL_GIF);
    gsKit_init_screen(screen_gs);
    gsKit_mode_switch(screen_gs, GS_ONESHOT);

    screen_line_count = 1;
    screen_visible = 1;
    screen_clear_hardware();
    return 0;
}

int crtube_screen_texture_create(crtube_screen_texture_t *texture,
                                 const unsigned int *pixels,
                                 int width, int height)
{
    GSTEXTURE *native;
    u32 memory_size;
    u32 vram_size;

    if (texture == NULL || pixels == NULL || width <= 0 || height <= 0 ||
        width > 128 || height > 128 || screen_gs == NULL)
        return -1;

    texture->native = NULL;
    texture->width = 0;
    texture->height = 0;

    native = (GSTEXTURE *)calloc(1, sizeof(*native));
    if (native == NULL)
        return -1;

    native->Width = 128;
    native->Height = 128;
    native->PSM = GS_PSM_CT32;
    native->Filter = GS_FILTER_LINEAR;
    native->VramClut = 0;
    native->Clut = NULL;

    memory_size = gsKit_texture_size_ee(128, 128, GS_PSM_CT32);
    vram_size = gsKit_texture_size(128, 128, GS_PSM_CT32);
    native->Mem = (u32 *)memalign(128, memory_size);
    if (native->Mem == NULL) {
        free(native);
        return -1;
    }
    memcpy(native->Mem, pixels, memory_size);

    native->Vram = gsKit_vram_alloc(screen_gs, vram_size,
                                    GSKIT_ALLOC_USERBUFFER);
    if (native->Vram == GSKIT_ALLOC_ERROR) {
        free(native->Mem);
        free(native);
        return -1;
    }

    gsKit_texture_upload(screen_gs, native);
    free(native->Mem);
    native->Mem = NULL;

    texture->native = native;
    texture->width = width;
    texture->height = height;
    return 0;
}

void crtube_screen_texture_draw(const crtube_screen_texture_t *texture,
                                int x, int y, int width, int height)
{
    GSTEXTURE *native;

    if (screen_gs == NULL || texture == NULL || texture->native == NULL ||
        width <= 0 || height <= 0)
        return;

    native = (GSTEXTURE *)texture->native;
    gsKit_prim_sprite_texture(screen_gs, native,
                              (float)x, (float)y, 0.0f, 0.0f,
                              (float)(x + width), (float)(y + height),
                              (float)texture->width, (float)texture->height,
                              0, GS_SETREG_RGBAQ(0x80, 0x80, 0x80, 0x80, 0));
}

void crtube_screen_draw_rect_outline(int x, int y, int width, int height)
{
    u64 color;

    if (screen_gs == NULL || width <= 0 || height <= 0)
        return;

    color = GS_SETREG_RGBAQ(0x60, 0x60, 0x60, 0x80, 0);
    gsKit_prim_line(screen_gs, (float)x, (float)y,
                    (float)(x + width), (float)y, 0, color);
    gsKit_prim_line(screen_gs, (float)(x + width), (float)y,
                    (float)(x + width), (float)(y + height), 0, color);
    gsKit_prim_line(screen_gs, (float)(x + width), (float)(y + height),
                    (float)x, (float)(y + height), 0, color);
    gsKit_prim_line(screen_gs, (float)x, (float)(y + height),
                    (float)x, (float)y, 0, color);
}

void crtube_screen_flush(void)
{
    if (screen_gs != NULL) {
        /* GS_ONESHOT queues are not a persistent scene. Finish this frame,
           then reset the queue before the next Home redraw. Without this,
           repeated grid renders append to the old queue and can corrupt it. */
        gsKit_queue_exec(screen_gs);
        gsKit_sync_flip(screen_gs);
        gsKit_queue_reset(screen_gs->Os_Queue);
    }
}

void crtube_screen_draw_text(int x, int y, unsigned int color,
                             const char *text)
{
    int origin_x = x;

    if (text == NULL)
        return;

    while (*text != '\0') {
        unsigned char ch = (unsigned char)*text++;
        if (ch == '\r')
            continue;
        if (ch == '\n') {
            x = origin_x;
            y += CRTUBE_GLYPH_HEIGHT;
            continue;
        }
        if (x + CRTUBE_GLYPH_WIDTH > 640)
            break;
        if (y + CRTUBE_GLYPH_HEIGHT > 512)
            break;
        screen_draw_glyph(x, y, (u32)color, ch);
        x += CRTUBE_GLYPH_WIDTH;
    }
}

void crtube_screen_set_visible(int visible)
{
    int next_visible = visible != 0;

    if (screen_visible == next_visible)
        return;

    screen_visible = next_visible;
    if (screen_visible)
        screen_redraw();
}

void crtube_screen_clear(void)
{
    memset(screen_lines, 0, sizeof(screen_lines));
    screen_line_count = 1;
    if (screen_visible)
        screen_clear_hardware();
}

void crtube_screen_printf(const char *format, ...)
{
    char text[CRTUBE_SCREEN_TEXT];
    va_list args;
    size_t index;
    char *line;
    size_t used;
    int scrolled;

    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);

    if (screen_line_count == 0)
        screen_line_count = 1;

    for (index = 0; text[index] != '\0'; index++) {
        line = screen_lines[screen_line_count - 1];
        used = strlen(line);

        if (text[index] == '\r')
            continue;

        if (text[index] == '\n' || used >= CRTUBE_SCREEN_COLS) {
            scrolled = screen_add_line();
            if (scrolled && screen_visible)
                screen_redraw();
            if (text[index] == '\n')
                continue;
            line = screen_lines[screen_line_count - 1];
            used = 0;
        }

        line[used] = text[index];
        line[used + 1] = '\0';
        if (screen_visible) {
            screen_draw_glyph((int)used * CRTUBE_GLYPH_WIDTH,
                              (screen_line_count - 1) * CRTUBE_GLYPH_HEIGHT,
                              screen_fontcolor,
                              (unsigned char)text[index]);
        }
    }
}


/*
 * Upload a 320x240 H.264 YUV420 frame as six 128x128 CT32 textures.
 * The GS texture allocator uses power-of-two 128x128 backing stores;
 * edge tiles are clipped by the sprite dimensions.
 */
int crtube_screen_draw_h264_frame(const crtube_h264_frame_t *frame,
                                  int x, int y)
{
    enum { FRAME_W = 320, FRAME_H = 240, TILE = 128, TILE_COLS = 3,
           TILE_ROWS = 2 };
    static crtube_screen_texture_t textures[TILE_COLS * TILE_ROWS];
    static int initialized;
    uint8_t *rgb;
    unsigned int tile_pixels[TILE * TILE] __attribute__((aligned(16)));
    unsigned tx, ty, row, col;
    int result = 0;

    if (!frame || frame->width != FRAME_W || frame->height != FRAME_H)
        return -1;

    rgb = (uint8_t *)malloc((size_t)FRAME_W * FRAME_H * 3u);
    if (!rgb)
        return -2;
    if (crtube_h264_frame_to_rgb24(frame, rgb,
                                   (size_t)FRAME_W * FRAME_H * 3u) < 0) {
        free(rgb);
        return -3;
    }

    for (ty = 0; ty < TILE_ROWS; ++ty) {
        for (tx = 0; tx < TILE_COLS; ++tx) {
            unsigned index = ty * TILE_COLS + tx;
            unsigned tile_w = FRAME_W - tx * TILE;
            unsigned tile_h = FRAME_H - ty * TILE;
            if (tile_w > TILE) tile_w = TILE;
            if (tile_h > TILE) tile_h = TILE;

            memset(tile_pixels, 0, sizeof(tile_pixels));
            for (row = 0; row < tile_h; ++row) {
                for (col = 0; col < tile_w; ++col) {
                    size_t src = (((size_t)(ty * TILE + row) * FRAME_W) +
                                  tx * TILE + col) * 3u;
                    tile_pixels[row * TILE + col] =
                        0x80000000u |
                        ((unsigned int)rgb[src] << 16) |
                        ((unsigned int)rgb[src + 1u] << 8) |
                        (unsigned int)rgb[src + 2u];
                }
            }

            if (!initialized) {
                if (crtube_screen_texture_create(&textures[index],
                                                 tile_pixels, TILE, TILE) < 0) {
                    result = -4;
                    goto done;
                }
            } else {
                /* Texture memory is resident in VRAM; update through gsKit. */
                GSTEXTURE *native = (GSTEXTURE *)textures[index].native;
                if (!native) {
                    result = -5;
                    goto done;
                }
                native->Mem = (u32 *)memalign(128,
                    gsKit_texture_size_ee(TILE, TILE, GS_PSM_CT32));
                if (!native->Mem) {
                    result = -6;
                    goto done;
                }
                memcpy(native->Mem, tile_pixels,
                       gsKit_texture_size_ee(TILE, TILE, GS_PSM_CT32));
                gsKit_texture_upload(screen_gs, native);
                free(native->Mem);
                native->Mem = NULL;
            }

            crtube_screen_texture_draw(&textures[index],
                                       x + (int)(tx * TILE),
                                       y + (int)(ty * TILE),
                                       (int)tile_w, (int)tile_h);
        }
    }
    initialized = 1;
    crtube_screen_flush();

done:
    free(rgb);
    return result;
}



#ifdef CRTUBE_H264_EDGE264_RUNTIME_SMOKE
/*
 * Experimental Edge264 YUV420 -> GS CT32 path.
 * Keep the staging texture resident in EE RAM and upload it directly:
 * no per-frame malloc, memcpy, or full-buffer clear.
 */
int crtube_screen_draw_edge264_frame(
    const crtube_h264_edge264_frame_t *frame, int x, int y)
{
    enum { FRAME_W = 320, FRAME_H = 240, TEX_W = 512, TEX_H = 256 };
    static GSTEXTURE *texture;
    static unsigned int pixels[TEX_W * TEX_H]
        __attribute__((aligned(128)));
    static unsigned frame_number;
    size_t vram_size;
    u64 t0, t1, t2, t3;
    u32 convert_us, upload_us, flush_us;
    int result_w, result_h;
    float scale_x, scale_y, scale;
    int dst_x, dst_y;

    if (!frame || !frame->y || !frame->cb || !frame->cr ||
        frame->width != FRAME_W || frame->height != FRAME_H ||
        frame->chroma_width != FRAME_W / 2u ||
        frame->chroma_height != FRAME_H / 2u ||
        frame->bit_depth_y != 8u || frame->bit_depth_c != 8u ||
        frame->stride_y < FRAME_W || frame->stride_c < FRAME_W / 2u ||
        screen_gs == NULL)
        return -1;

    t0 = GetTimerSystemTime();

    /*
     * Convert using the shared, host-tested BT.601 kernel. The kernel
     * reuses each 4:2:0 chroma contribution for its two luma samples.
     */
    if (crtube_h264_yuv420_to_ct32(
            frame->y, frame->cb, frame->cr,
            FRAME_W, FRAME_H, frame->stride_y, frame->stride_c,
            pixels, TEX_W) < 0)
        return -4;

    t1 = GetTimerSystemTime();

    if (!texture) {
        texture = (GSTEXTURE *)calloc(1, sizeof(*texture));
        if (!texture)
            return -2;
        texture->Width = TEX_W;
        texture->Height = TEX_H;
        texture->PSM = GS_PSM_CT32;
        texture->Filter = GS_FILTER_NEAREST;
        texture->VramClut = 0;
        texture->Clut = NULL;

        vram_size = gsKit_texture_size(TEX_W, TEX_H, GS_PSM_CT32);
        texture->Vram = gsKit_vram_alloc(screen_gs, vram_size,
                                         GSKIT_ALLOC_USERBUFFER);
        if (texture->Vram == GSKIT_ALLOC_ERROR) {
            free(texture);
            texture = NULL;
            return -3;
        }
    }

    /*
     * GS texture upload consumes the source synchronously. Keep the
     * 512x256 buffer aligned and reuse it instead of allocating/copying
     * 512 KiB for every decoded frame. The unused right/bottom area is
     * zero-initialized once by static storage.
     */
    texture->Mem = pixels;
    /* Write back the complete padded staging texture before GS DMA reads it. */
    FlushCache(0);
    gsKit_texture_upload(screen_gs, texture);
    texture->Mem = NULL;

    t2 = GetTimerSystemTime();

    scale_x = (float)screen_gs->Width / (float)FRAME_W;
    scale_y = (float)screen_gs->Height / (float)FRAME_H;
    scale = scale_x < scale_y ? scale_x : scale_y;
    result_w = (int)((float)FRAME_W * scale + 0.5f);
    result_h = (int)((float)FRAME_H * scale + 0.5f);
    dst_x = ((int)screen_gs->Width - result_w) / 2 + x;
    dst_y = ((int)screen_gs->Height - result_h) / 2 + y;

    gsKit_prim_sprite_texture(screen_gs, texture,
        (float)dst_x, (float)dst_y, 0.0f, 0.0f,
        (float)(dst_x + result_w), (float)(dst_y + result_h),
        (float)FRAME_W, (float)FRAME_H, 0,
        GS_SETREG_RGBAQ(0x80, 0x80, 0x80, 0x80, 0));

    crtube_screen_flush();
    t3 = GetTimerSystemTime();

    convert_us = (u32)(((t1 - t0) * 125u) / 18432u);
    upload_us = (u32)(((t2 - t1) * 125u) / 18432u);
    flush_us = (u32)(((t3 - t2) * 125u) / 18432u);
    frame_number++;

    if (frame_number == 1u) {
        crtube_log("H.264 GS source: Y=%u Cb=%u Cr=%u stride=%u/%u "
                   "sample_rgb=%08lx\n",
                   frame->y[0], frame->cb[0], frame->cr[0],
                   frame->stride_y, frame->stride_c,
                   (unsigned long)pixels[0]);
    }
    if (frame_number <= 3u || (frame_number % 30u) == 0u) {
        crtube_log("H.264 GS timing frame=%u convert_us=%u "
                   "upload_us=%u flush_us=%u total_us=%u\n",
                   frame_number, convert_us, upload_us, flush_us,
                   convert_us + upload_us + flush_us);
    }
    return 0;
}
#endif
