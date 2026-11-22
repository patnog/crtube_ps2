#include <stdio.h>
#include <stdarg.h>

#include "debug_log.h"

#define CRTUBE_SMB_LOG_PATH "smb0:/APPS/CRTUBE_PS2/dbg_crtube.txt"

static FILE *g_log_file;
static int g_log_started;

int crtube_log_init(const char *unused_path)
{
    (void)unused_path;

    /* Keep an open session log. The experimental smoke test may close the
       file before the UI later reopens diagnostics. */
    if (g_log_file != NULL)
        return 0;

    /* Truncate once per boot; subsequent opens append instead of erasing
       the decoder instrumentation. */
    g_log_file = fopen(CRTUBE_SMB_LOG_PATH, g_log_started ? "a" : "w");
    if (g_log_file == NULL)
        return -1;
    g_log_started = 1;
    return 0;
}

void crtube_log(const char *format, ...)
{
    va_list args;

    if (format == NULL || g_log_file == NULL)
        return;

    va_start(args, format);
    vfprintf(g_log_file, format, args);
    fflush(g_log_file);
    va_end(args);
}

void crtube_log_close(void)
{
    if (g_log_file != NULL) {
        fflush(g_log_file);
        fclose(g_log_file);
        g_log_file = NULL;
    }
}
