#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <sys/random.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <ps2ip.h>

#include "bearssl.h"
#include "bearssl_transport.h"
#include "http.h"

/*
 * Use the PS2SDK getrandom() API as a strong reference. A weak reference does
 * not cause the static linker to extract the implementation from libc, even
 * when the SDK provides it.
 */
static unsigned char g_entropy_probe[32];
static int g_entropy_probe_ready;
static uint32_t g_clock_override;

extern const br_x509_trust_anchor TLS_TRUST_ANCHORS[];
extern const size_t TLS_TRUST_ANCHORS_COUNT;

typedef struct crtube_bearssl_context {
    br_ssl_client_context client;
    br_x509_minimal_context x509;
    br_sslio_context io;
    unsigned char io_buffer[BR_SSL_BUFSIZE_MONO];
    unsigned char entropy[32];
    int socket_fd;
    int last_error;
} crtube_bearssl_context_t;

/* One static context avoids putting the BearSSL state and 16.7 KB TLS buffer
   on the EE thread stack. This transport is intentionally single-connection
   and must only be called synchronously by one thread at a time. */
static crtube_bearssl_context_t g_bearssl;
static int g_bearssl_last_error;

static int parse_ipv4(const char *text, struct in_addr *address)
{
    unsigned int octets[4];
    char extra;
    unsigned char *bytes;

    if (text == NULL || address == NULL ||
        sscanf(text, "%u.%u.%u.%u%c", &octets[0], &octets[1],
               &octets[2], &octets[3], &extra) != 4 ||
        octets[0] > 255 || octets[1] > 255 ||
        octets[2] > 255 || octets[3] > 255)
        return -1;

    bytes = (unsigned char *)&address->s_addr;
    bytes[0] = (unsigned char)octets[0];
    bytes[1] = (unsigned char)octets[1];
    bytes[2] = (unsigned char)octets[2];
    bytes[3] = (unsigned char)octets[3];
    return 0;
}

static int seed_entropy(void)
{
    ssize_t received;

    if (g_entropy_probe_ready) {
        memcpy(g_bearssl.entropy, g_entropy_probe,
               sizeof(g_bearssl.entropy));
        memset(g_entropy_probe, 0, sizeof(g_entropy_probe));
        g_entropy_probe_ready = 0;
        return 0;
    }

    received = getrandom(g_bearssl.entropy, sizeof(g_bearssl.entropy), 0);
    if (received != (ssize_t)sizeof(g_bearssl.entropy)) {
        memset(g_bearssl.entropy, 0, sizeof(g_bearssl.entropy));
        return -1;
    }
    return 0;
}

static int set_certificate_time(void)
{
    uint32_t now = crtube_bearssl_clock_time();
    uint64_t unix_seconds;
    uint64_t days;
    uint32_t seconds;

    /* Reject unset or implausible RTC values. PS2SDK time() obtains its wall
       clock from the console RTC when the CDVD clock support is linked. */
    if (now < 1577836800UL || (uint64_t)now > 2147483647ULL)
        return -1;

    unix_seconds = (uint64_t)now;
    days = unix_seconds / 86400u + 719528u;
    seconds = (uint32_t)(unix_seconds % 86400u);
    if (days > 0xFFFFFFFFu)
        return -1;

    br_x509_minimal_set_time(&g_bearssl.x509, (uint32_t)days, seconds);
    return 0;
}

static int socket_read(void *context, unsigned char *buffer, size_t length)
{
    crtube_bearssl_context_t *tls =
        (crtube_bearssl_context_t *)context;
    int received;

    received = (int)recv(tls->socket_fd, buffer, length, 0);
    if (received <= 0)
        return -1;
    return received;
}

static int socket_write(void *context, const unsigned char *buffer,
                        size_t length)
{
    crtube_bearssl_context_t *tls =
        (crtube_bearssl_context_t *)context;
    int sent;

    sent = (int)send(tls->socket_fd, buffer, length, 0);
    if (sent <= 0)
        return -1;
    return sent;
}

static int open_socket(const char *host)
{
    struct sockaddr_in address;
    struct timeval timeout;
    char ip[16];
    int result;

    if (crtube_http_resolve_ipv4(host, ip, sizeof(ip)) != 0)
        return CRTUBE_BEARSSL_ERR_DNS;

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(443);
    if (parse_ipv4(ip, &address.sin_addr) != 0)
        return CRTUBE_BEARSSL_ERR_DNS;

    g_bearssl.socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_bearssl.socket_fd < 0)
        return CRTUBE_BEARSSL_ERR_SOCKET;

    timeout.tv_sec = 10;
    timeout.tv_usec = 0;
    setsockopt(g_bearssl.socket_fd, SOL_SOCKET, SO_RCVTIMEO,
               &timeout, sizeof(timeout));
    setsockopt(g_bearssl.socket_fd, SOL_SOCKET, SO_SNDTIMEO,
               &timeout, sizeof(timeout));

    result = connect(g_bearssl.socket_fd, (struct sockaddr *)&address,
                     sizeof(address));
    if (result != 0) {
        close(g_bearssl.socket_fd);
        g_bearssl.socket_fd = -1;
        return CRTUBE_BEARSSL_ERR_CONNECT;
    }
    return CRTUBE_BEARSSL_OK;
}

static void close_socket(void)
{
    if (g_bearssl.socket_fd >= 0) {
        close(g_bearssl.socket_fd);
        g_bearssl.socket_fd = -1;
    }
}

/*
 * Diagnostic only: PS2SDK/newlib may expose the C rand() interface, but rand()
 * is a deterministic PRNG and is not suitable for TLS seeding. Probe the
 * conventional device path separately; do not use it for TLS until its
 * implementation and entropy quality are established.
 */
int crtube_bearssl_entropy_probe(void)
{
    unsigned char sample[32];
    int fd;
    int result = 0;
    ssize_t received;

    memset(g_entropy_probe, 0, sizeof(g_entropy_probe));
    g_entropy_probe_ready = 0;
    received = getrandom(g_entropy_probe, sizeof(g_entropy_probe), 0);
    if (received == (ssize_t)sizeof(g_entropy_probe)) {
        g_entropy_probe_ready = 1;
        result |= CRTUBE_BEARSSL_ENTROPY_GETRANDOM_READ;
    } else {
        memset(g_entropy_probe, 0, sizeof(g_entropy_probe));
    }

    fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        received = read(fd, sample, sizeof(sample));
        close(fd);
        if (received == (ssize_t)sizeof(sample))
            result |= CRTUBE_BEARSSL_ENTROPY_URANDOM_DEVICE;
    }

    memset(sample, 0, sizeof(sample));
    return result;
}

int crtube_bearssl_entropy_available(void)
{
    return g_entropy_probe_ready;
}

uint32_t crtube_bearssl_clock_time(void)
{
    time_t rtc_time;

    if (g_clock_override >= 1577836800UL &&
        g_clock_override <= 2147483647UL)
        return g_clock_override;

    rtc_time = time(NULL);
    if (rtc_time < (time_t)1577836800 ||
        (uint64_t)rtc_time > 2147483647ULL)
        return 0;
    return (uint32_t)rtc_time;
}

int crtube_bearssl_set_clock_time(uint32_t unix_time)
{
    if (unix_time < 1577836800UL || unix_time > 2147483647UL)
        return -1;
    g_clock_override = unix_time;
    return 0;
}

int crtube_bearssl_clock_available(void)
{
    return crtube_bearssl_clock_time() != 0;
}

int crtube_bearssl_last_error(void)
{
    return g_bearssl_last_error;
}

int crtube_bearssl_exchange_alloc(const char *host, const char *request,
                                  char **response, size_t *response_size)
{
    int result = CRTUBE_BEARSSL_ERR_IO;
    size_t response_length = 0;
    size_t response_capacity = 8192;
    int context_initialized = 0;
    char *buffer = NULL;

    if (host == NULL || host[0] == '\0' || request == NULL ||
        response == NULL || response_size == NULL)
        return CRTUBE_BEARSSL_ERR_ARGUMENT;
    *response = NULL;
    *response_size = 0;
    g_bearssl_last_error = 0;
    memset(&g_bearssl, 0, sizeof(g_bearssl));
    g_bearssl.socket_fd = -1;

    if (seed_entropy() != 0) {
        result = CRTUBE_BEARSSL_ERR_ENTROPY;
        goto cleanup;
    }
    if (!crtube_bearssl_clock_available()) {
        result = CRTUBE_BEARSSL_ERR_CLOCK;
        goto cleanup;
    }

    result = open_socket(host);
    if (result != CRTUBE_BEARSSL_OK)
        goto cleanup;

    br_ssl_client_init_full(&g_bearssl.client, &g_bearssl.x509,
                            TLS_TRUST_ANCHORS, TLS_TRUST_ANCHORS_COUNT);
    context_initialized = 1;
    if (set_certificate_time() != 0) {
        result = CRTUBE_BEARSSL_ERR_CLOCK;
        goto cleanup;
    }
    br_ssl_engine_set_versions(&g_bearssl.client.eng, BR_TLS12, BR_TLS12);
    br_ssl_engine_set_buffer(&g_bearssl.client.eng, g_bearssl.io_buffer,
                             sizeof(g_bearssl.io_buffer), 0);
    br_ssl_engine_inject_entropy(&g_bearssl.client.eng, g_bearssl.entropy,
                                 sizeof(g_bearssl.entropy));

    if (!br_ssl_client_reset(&g_bearssl.client, host, 0)) {
        g_bearssl_last_error = br_ssl_engine_last_error(&g_bearssl.client.eng);
        result = CRTUBE_BEARSSL_ERR_TLS;
        goto cleanup;
    }

    br_sslio_init(&g_bearssl.io, &g_bearssl.client.eng,
                  socket_read, &g_bearssl, socket_write, &g_bearssl);
    if (br_sslio_write_all(&g_bearssl.io, request, strlen(request)) != 0 ||
        br_sslio_flush(&g_bearssl.io) != 0) {
        g_bearssl_last_error = br_ssl_engine_last_error(&g_bearssl.client.eng);
        result = CRTUBE_BEARSSL_ERR_IO;
        goto cleanup;
    }

    buffer = (char *)malloc(response_capacity);
    if (buffer == NULL) {
        result = CRTUBE_BEARSSL_ERR_IO;
        goto cleanup;
    }
    for (;;) {
        int received;
        size_t available;
        if (response_length + 1 >= response_capacity) {
            size_t next_capacity;
            char *grown;
            if (response_capacity - 1 >= CRTUBE_BEARSSL_RESPONSE_LIMIT) {
                unsigned char extra;
                received = br_sslio_read(&g_bearssl.io, &extra, 1);
                if (received > 0)
                    result = CRTUBE_BEARSSL_ERR_TOO_LARGE;
                else if (received == 0 || response_length > 0)
                    result = (int)response_length;
                else
                    result = CRTUBE_BEARSSL_ERR_IO;
                break;
            }
            next_capacity = response_capacity * 2;
            if (next_capacity > CRTUBE_BEARSSL_RESPONSE_LIMIT + 1u)
                next_capacity = CRTUBE_BEARSSL_RESPONSE_LIMIT + 1u;
            grown = (char *)realloc(buffer, next_capacity);
            if (grown == NULL) {
                result = CRTUBE_BEARSSL_ERR_IO;
                break;
            }
            buffer = grown;
            response_capacity = next_capacity;
        }
        available = response_capacity - response_length - 1;
        if (available > 4096)
            available = 4096;
        received = br_sslio_read(&g_bearssl.io, buffer + response_length, available);
        if (received < 0) {
            g_bearssl_last_error = br_ssl_engine_last_error(&g_bearssl.client.eng);
            /* socket_read maps orderly TCP close to -1; preserve completed data. */
            result = response_length > 0 ? (int)response_length :
                     CRTUBE_BEARSSL_ERR_IO;
            break;
        }
        if (received == 0) {
            result = (int)response_length;
            break;
        }
        response_length += (size_t)received;
        buffer[response_length] = '\0';
    }

    if (result >= 0 && response_length > 0) {
        *response = buffer;
        *response_size = response_length;
        buffer = NULL;
    } else if (result == CRTUBE_BEARSSL_ERR_TOO_LARGE) {
        *response = buffer;
        *response_size = response_length;
        buffer = NULL;
    }

cleanup:
    if (buffer != NULL)
        free(buffer);
    close_socket();
    memset(g_bearssl.entropy, 0, sizeof(g_bearssl.entropy));
    if (context_initialized) {
        br_ssl_client_zero(&g_bearssl.client);
        memset(&g_bearssl.x509, 0, sizeof(g_bearssl.x509));
        memset(&g_bearssl.io, 0, sizeof(g_bearssl.io));
        memset(g_bearssl.io_buffer, 0, sizeof(g_bearssl.io_buffer));
    }
    return result;
}

int crtube_bearssl_exchange(const char *host, const char *request,
                            char *response, size_t response_capacity)
{
    char *dynamic_response = NULL;
    size_t dynamic_size = 0;
    size_t copy_size;
    int result;

    if (response == NULL || response_capacity < 2)
        return CRTUBE_BEARSSL_ERR_ARGUMENT;
    response[0] = '\0';
    result = crtube_bearssl_exchange_alloc(host, request,
                                           &dynamic_response, &dynamic_size);
    if (dynamic_response != NULL) {
        copy_size = dynamic_size;
        if (copy_size > response_capacity - 1)
            copy_size = response_capacity - 1;
        memcpy(response, dynamic_response, copy_size);
        response[copy_size] = '\0';
        free(dynamic_response);
        if (result >= 0)
            return (int)copy_size;
    }
    return result;
}
