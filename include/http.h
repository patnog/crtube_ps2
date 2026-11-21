#ifndef CRTUBE_PS2_HTTP_H
#define CRTUBE_PS2_HTTP_H

#include <stddef.h>

#define CRTUBE_HTTP_OK                    0
#define CRTUBE_HTTP_ERR_SOCKET           -1
#define CRTUBE_HTTP_ERR_CONNECT          -2
#define CRTUBE_HTTP_ERR_SEND             -3
#define CRTUBE_HTTP_ERR_RECV             -4
#define CRTUBE_HTTP_ERR_DNS              -5
#define CRTUBE_HTTP_ERR_PROTOCOL         -6
#define CRTUBE_HTTP_ERR_REDIRECT         -7
#define CRTUBE_HTTP_ERR_HTTPS_UNSUPPORTED -8
#define CRTUBE_HTTP_ERR_URL              -9

#define CRTUBE_HTTP_STAGE_NONE       0
#define CRTUBE_HTTP_STAGE_DNS        1
#define CRTUBE_HTTP_STAGE_SOCKET     2
#define CRTUBE_HTTP_STAGE_CONNECT    3
#define CRTUBE_HTTP_STAGE_SEND       4
#define CRTUBE_HTTP_STAGE_RECV       5
#define CRTUBE_HTTP_STAGE_PARSE      6
#define CRTUBE_HTTP_STAGE_REDIRECT   7

void crtube_http_set_dns_server(const char *ip);

int crtube_http_last_errno(void);
int crtube_http_last_stage(void);
int crtube_http_last_socket(void);
int crtube_http_last_connect_result(void);
int crtube_http_last_so_error(void);
int crtube_http_last_so_error_result(void);
int crtube_http_last_so_error_errno(void);
int crtube_http_last_close_result(void);
int crtube_http_last_close_errno(void);
int crtube_http_last_bytes_sent(void);
int crtube_http_last_bytes_received(void);
int crtube_http_last_status(void);
int crtube_http_last_redirects(void);
int crtube_http_last_truncated(void);

/* Performs an HTTP/1.0 GET using either a dotted IPv4 address or DNS hostname. */
int crtube_http_get(const char *host, unsigned short port,
                    const char *path, char *response, size_t response_size);

/* Parses an http:// URL and follows up to three HTTP redirects. HTTPS is not
   supported by this transport and is reported explicitly. */
int crtube_http_get_url(const char *url, char *response, size_t response_size);

/* Compatibility wrapper for existing IPv4-only call sites. */
int crtube_http_get_ipv4(const char *ip, unsigned short port,
                         const char *path, char *response, size_t response_size);

#endif
