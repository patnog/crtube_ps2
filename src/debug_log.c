#include <stdio.h>
#include <stdarg.h>

#include "debug_log.h"

#define CRTUBE_SMB_LOG_PATH "smb0:/APPS/CRTUBE_PS2/dbg_crtube.txt"

static FILE *g_log_file;

int crtube_log_init(const char *unused_path)
{
    (void)unused_path;

    if (g_log_file != NULL) {
        fclose(g_log_file);
        g_log_file = NULL;
    }

    /* Open only after smb0: has been authenticated and the share mounted. */
    g_log_file = fopen(CRTUBE_SMB_LOG_PATH, "w");
    return g_log_file != NULL ? 0 : -1;
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
