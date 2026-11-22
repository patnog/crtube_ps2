#ifndef CRTUBE_SCREEN_H
#define CRTUBE_SCREEN_H

#include <stdarg.h>
#include "h264_decoder.h"
#ifdef CRTUBE_H264_EDGE264_RUNTIME_SMOKE
#include "h264_edge264.h"
#endif

typedef struct crtube_screen_texture {
    void *native;
    int width;
    int height;
} crtube_screen_texture_t;

int crtube_screen_init(void);
void crtube_screen_clear(void);
void crtube_screen_printf(const char *format, ...);
void crtube_screen_set_visible(int visible);
int crtube_screen_texture_create(crtube_screen_texture_t *texture,
                                 const unsigned int *pixels,
                                 int width, int height);
void crtube_screen_texture_draw(const crtube_screen_texture_t *texture,
                                int x, int y, int width, int height);
void crtube_screen_flush(void);
int crtube_screen_draw_h264_frame(const crtube_h264_frame_t *frame,
                                  int x, int y);
#ifdef CRTUBE_H264_EDGE264_RUNTIME_SMOKE
int crtube_screen_draw_edge264_frame(
    const crtube_h264_edge264_frame_t *frame, int x, int y);
#endif
void crtube_screen_draw_rect_outline(int x, int y, int width, int height);
void crtube_screen_draw_text(int x, int y, unsigned int color,
                             const char *text);

#define scr_printf crtube_screen_printf

#endif
