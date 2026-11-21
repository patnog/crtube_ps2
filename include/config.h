#ifndef CRTUBE_CONFIG_H
#define CRTUBE_CONFIG_H

#include <stdint.h>

typedef struct {
    char ip[16];
    char netmask[16];
    char gateway[16];
    char dns[16];
    char smb_server[22];
    unsigned short smb_port;
    char smb_share[128];
    char ip_config_source[32];
    char smb_config_source[32];
} crtube_config_t;

int crtube_config_load(crtube_config_t *config);

#endif
