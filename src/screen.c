#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <debug.h>

#include "screen.h"
#undef scr_printf

#define CRTUBE_SCREEN_LINES 26
#define CRTUBE_SCREEN_COLS  80
#define CRTUBE_SCREEN_TEXT  2048

static char screen_lines[CRTUBE_SCREEN_LINES][CRTUBE_SCREEN_COLS + 1];
static int screen_line_count;

static void screen_add_line(void)
{
    if (screen_line_count < CRTUBE_SCREEN_LINES) {
        screen_line_count++;
        screen_lines[screen_line_count - 1][0] = '\0';
    } else {
        memmove(screen_lines[0], screen_lines[1],
                sizeof(screen_lines[0]) * (CRTUBE_SCREEN_LINES - 1));
        screen_lines[CRTUBE_SCREEN_LINES - 1][0] = '\0';
    }
}

static void screen_redraw(void)
{
    int i;

    scr_clear();
    for (i = 0; i < screen_line_count; i++)
        scr_printf("%s\n", screen_lines[i]);
}

void crtube_screen_clear(void)
{
    memset(screen_lines, 0, sizeof(screen_lines));
    screen_line_count = 1;
    screen_redraw();
}

void crtube_screen_printf(const char *format, ...)
{
    char text[CRTUBE_SCREEN_TEXT];
    va_list args;
    size_t i;
    char *line;
    size_t used;

    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);

    if (screen_line_count == 0)
        screen_line_count = 1;

    for (i = 0; text[i] != '\0'; i++) {
        line = screen_lines[screen_line_count - 1];
        used = strlen(line);

        if (text[i] == '\r')
            continue;

        if (text[i] == '\n' || used >= CRTUBE_SCREEN_COLS) {
            screen_add_line();
            if (text[i] == '\n')
                continue;
            line = screen_lines[screen_line_count - 1];
            used = 0;
        }

        line[used] = text[i];
        line[used + 1] = '\0';
    }

    screen_redraw();
}
