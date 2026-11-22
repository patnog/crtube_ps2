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
#define CRTUBE_HTTP_ERR_TLS             -10

#define CRTUBE_HTTP_STAGE_NONE       0
#define CRTUBE_HTTP_STAGE_DNS        1
#define CRTUBE_HTTP_STAGE_SOCKET     2
#define CRTUBE_HTTP_STAGE_CONNECT    3
#define CRTUBE_HTTP_STAGE_SEND       4
#define CRTUBE_HTTP_STAGE_RECV       5
#define CRTUBE_HTTP_STAGE_PARSE      6
#define CRTUBE_HTTP_STAGE_REDIRECT   7
#define CRTUBE_HTTP_STAGE_TLS        8

void crtube_http_set_dns_server(const char *ip);

/* Resolve a hostname through the PS2IPS-compatible UDP DNS client. */
int crtube_http_resolve_ipv4(const char *host, char *ip, size_t ip_size);

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

/* Parses HTTP/HTTPS URLs and follows up to three redirects. HTTPS uses the
   verified BearSSL transport when YOUTUBE_TEST=1 is enabled; otherwise it is
   reported explicitly as unsupported. */
int crtube_http_get_url(const char *url, char *response, size_t response_size);

/* Performs an HTTPS POST with a text body and follows up to three redirects.
   A 303 redirect changes the request to GET; other supported redirects preserve it. */
int crtube_http_post_url(const char *url, const char *content_type,
                         const char *body, char *response, size_t response_size);
/* HTTPS GET dynamic variant. Allocates the complete response; caller frees it. */
int crtube_http_get_url_alloc(const char *url, char **response,
                              size_t *response_size);
/* Dynamic variant: allocates the complete raw HTTP response; caller frees it. */
int crtube_http_post_url_alloc(const char *url, const char *content_type,
                               const char *body, char **response,
                               size_t *response_size);

/* Compatibility wrapper for existing IPv4-only call sites. */
int crtube_http_get_ipv4(const char *ip, unsigned short port,
                         const char *path, char *response, size_t response_size);

#endif
