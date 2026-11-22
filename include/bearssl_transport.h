#ifndef CRTUBE_BEARSSL_TRANSPORT_H
#define CRTUBE_BEARSSL_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

#define CRTUBE_BEARSSL_OK             0
#define CRTUBE_BEARSSL_ERR_ARGUMENT  -1
#define CRTUBE_BEARSSL_ERR_DNS       -2
#define CRTUBE_BEARSSL_ERR_ENTROPY   -3
#define CRTUBE_BEARSSL_ERR_CLOCK     -4
#define CRTUBE_BEARSSL_ERR_SOCKET    -5
#define CRTUBE_BEARSSL_ERR_CONNECT   -6
#define CRTUBE_BEARSSL_ERR_TLS       -7
#define CRTUBE_BEARSSL_ERR_IO        -8
#define CRTUBE_BEARSSL_ERR_BUSY      -9
#define CRTUBE_BEARSSL_ERR_TOO_LARGE -10

#define CRTUBE_BEARSSL_RESPONSE_LIMIT (2u * 1024u * 1024u)

#define CRTUBE_BEARSSL_ENTROPY_GETRANDOM_READ   0x01
#define CRTUBE_BEARSSL_ENTROPY_URANDOM_DEVICE   0x02

/*
 * Send one complete HTTP request over a verified TLS 1.2 connection.
 * request must contain the HTTP request line, headers and optional body.
 * response receives the raw HTTP response, including headers.
 * Only one connection may be active at a time.
 */
int crtube_bearssl_exchange(const char *host, const char *request,
                            char *response, size_t response_capacity);
/* Allocates and grows the raw HTTP response up to RESPONSE_LIMIT. Caller frees. */
int crtube_bearssl_exchange_alloc(const char *host, const char *request,
                                  char **response, size_t *response_size);

int crtube_bearssl_last_error(void);
int crtube_bearssl_entropy_available(void);
int crtube_bearssl_entropy_probe(void);
int crtube_bearssl_clock_available(void);
/* Set a validated UTC Unix timestamp obtained from the SNTP bootstrap. */
int crtube_bearssl_set_clock_time(uint32_t unix_time);
uint32_t crtube_bearssl_clock_time(void);

#endif
