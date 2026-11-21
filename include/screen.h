#ifndef CRTUBE_SCREEN_H
#define CRTUBE_SCREEN_H

#include <stdarg.h>

void crtube_screen_clear(void);
void crtube_screen_printf(const char *format, ...);

/* Keep existing diagnostic call sites, but route them through the scrollback. */
#define scr_printf crtube_screen_printf

#endif
