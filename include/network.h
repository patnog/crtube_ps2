#ifndef CRTUBE_PS2_NETWORK_H
#define CRTUBE_PS2_NETWORK_H

#include <stddef.h>
#include "localization.h"

#define CRTUBE_NETWORK_OK             0
#define CRTUBE_NETWORK_ERR_MODULE    -1
#define CRTUBE_NETWORK_ERR_LINK      -2
#define CRTUBE_NETWORK_ERR_DHCP      -3
#define CRTUBE_NETWORK_ERR_CONFIG   -4

int crtube_network_init(void);
void crtube_network_deinit(void);
void crtube_network_run_diagnostics(void);
void crtube_network_set_progress_callback(
    void (*callback)(crtube_text_id_t text_id));
void crtube_network_report_progress(crtube_text_id_t text_id);
void crtube_network_log_diagnostics(void);
int crtube_network_is_ready(void);
int crtube_network_get_ip(char *buffer, size_t buffer_size);
int crtube_network_get_smb_server_ip(char *buffer, size_t buffer_size);
int crtube_network_get_smb_endpoint(char *ip, size_t ip_size,
                                    unsigned short *port);
int crtube_network_get_details(char *ip, size_t ip_size,
                               char *mask, size_t mask_size,
                               char *gateway, size_t gateway_size,
                               char *mac, size_t mac_size,
                               int *link_status);
int crtube_smb_init(void);
int crtube_smb_is_ready(void);

#endif
