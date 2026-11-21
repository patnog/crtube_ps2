#ifndef CRTUBE_DEBUG_LOG_H
#define CRTUBE_DEBUG_LOG_H

int crtube_log_init(const char *elf_path);
void crtube_log(const char *format, ...);
void crtube_log_close(void);

#endif
