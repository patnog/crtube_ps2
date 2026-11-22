#ifndef CRTUBE_SCREEN_H
#define CRTUBE_SCREEN_H

#include <stdarg.h>

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
void crtube_screen_draw_rect_outline(int x, int y, int width, int height);
void crtube_screen_draw_text(int x, int y, unsigned int color,
                             const char *text);

#define scr_printf crtube_screen_printf

#endif
