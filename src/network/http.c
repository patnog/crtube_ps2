#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <strings.h>
#include <fcntl.h>
#include <delaythread.h>

/* PS2IPS sockets are EE libcglue descriptors backed by IOP socket handles. */
#include <ps2ip.h>

#include "http.h"
#include "debug_log.h"
#ifdef CRTUBE_ENABLE_YOUTUBE_PROBE
#include "bearssl_transport.h"
#include "sntp.h"
#endif

/* PS2SDK's PS2IPS libcglue backend does not install inet_ops. Avoid inet_addr()
   and copy dotted IPv4 octets directly into the network-order address bytes. */
static int parse_ipv4_address(const char *text, struct in_addr *address)
{
    unsigned int octets[4];
    char extra;
    unsigned char *bytes;

    if (text == NULL || address == NULL ||
        sscanf(text, "%u.%u.%u.%u%c",
               &octets[0], &octets[1], &octets[2], &octets[3], &extra) != 4)
        return -1;

    if (octets[0] > 255 || octets[1] > 255 ||
        octets[2] > 255 || octets[3] > 255)
        return -1;

    bytes = (unsigned char *)&address->s_addr;
    bytes[0] = (unsigned char)octets[0];
    bytes[1] = (unsigned char)octets[1];
    bytes[2] = (unsigned char)octets[2];
    bytes[3] = (unsigned char)octets[3];
    return 0;
}

static int g_http_last_errno;
static int g_http_last_stage;
static int g_http_last_socket = -1;
static int g_http_last_connect_result;
static int g_http_last_so_error;
static int g_http_last_so_error_result = -1;
static int g_http_last_so_error_errno;
static int g_http_last_close_result = -1;
static int g_http_last_close_errno;
static int g_http_last_bytes_sent;
static int g_http_last_bytes_received;
static int g_http_last_status = -1;
static int g_http_last_redirects;
static int g_http_last_truncated;
static char g_http_dns_server[16] = "0.0.0.0";
static unsigned short g_dns_query_id = 0x4352;

static void reset_http_state(void)
{
    g_http_last_errno = 0;
    g_http_last_stage = CRTUBE_HTTP_STAGE_NONE;
    g_http_last_socket = -1;
    g_http_last_connect_result = 0;
    g_http_last_so_error = 0;
    g_http_last_so_error_result = -1;
    g_http_last_so_error_errno = 0;
    g_http_last_close_result = -1;
    g_http_last_close_errno = 0;
    g_http_last_bytes_sent = 0;
    g_http_last_bytes_received = 0;
    g_http_last_status = -1;
    g_http_last_redirects = 0;
    g_http_last_truncated = 0;
}

void crtube_http_set_dns_server(const char *ip)
{
    struct in_addr parsed;

    if (ip != NULL && parse_ipv4_address(ip, &parsed) == 0) {
        strncpy(g_http_dns_server, ip, sizeof(g_http_dns_server) - 1);
        g_http_dns_server[sizeof(g_http_dns_server) - 1] = '\0';
    }
}

int crtube_http_last_errno(void) { return g_http_last_errno; }
int crtube_http_last_stage(void) { return g_http_last_stage; }
int crtube_http_last_socket(void) { return g_http_last_socket; }
int crtube_http_last_connect_result(void) { return g_http_last_connect_result; }
int crtube_http_last_so_error(void) { return g_http_last_so_error; }
int crtube_http_last_so_error_result(void) { return g_http_last_so_error_result; }
int crtube_http_last_so_error_errno(void) { return g_http_last_so_error_errno; }
int crtube_http_last_close_result(void) { return g_http_last_close_result; }
int crtube_http_last_close_errno(void) { return g_http_last_close_errno; }
int crtube_http_last_bytes_sent(void) { return g_http_last_bytes_sent; }
int crtube_http_last_bytes_received(void) { return g_http_last_bytes_received; }
int crtube_http_last_status(void) { return g_http_last_status; }
int crtube_http_last_redirects(void) { return g_http_last_redirects; }
int crtube_http_last_truncated(void) { return g_http_last_truncated; }

static unsigned short read_u16(const unsigned char *p)
{
    return (unsigned short)(((unsigned short)p[0] << 8) | p[1]);
}

static int skip_dns_name(const unsigned char *packet, size_t packet_size,
                         size_t *offset)
{
    size_t pos = *offset;
    size_t steps = 0;

    while (pos < packet_size && steps++ < 128) {
        unsigned char length = packet[pos];
        if ((length & 0xC0) == 0xC0) {
            if (pos + 1 >= packet_size)
                return -1;
            *offset = pos + 2;
            return 0;
        }
        if ((length & 0xC0) != 0)
            return -1;
        pos++;
        if (length == 0) {
            *offset = pos;
            return 0;
        }
        if (length > 63 || pos + length > packet_size)
            return -1;
        pos += length;
    }
    return -1;
}

static int encode_dns_name(const char *host, unsigned char *out,
                           size_t out_size, size_t *written)
{
    const char *label = host;
    size_t used = 0;

    if (host == NULL || host[0] == '\0')
        return -1;

    while (*label != '\0') {
        const char *dot = strchr(label, '.');
        size_t length = dot != NULL ? (size_t)(dot - label) : strlen(label);
        size_t i;

        if (length == 0 || length > 63 || used + length + 1 >= out_size)
            return -1;
        out[used++] = (unsigned char)length;
        for (i = 0; i < length; i++) {
            unsigned char c = (unsigned char)label[i];
            if (!( (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                   (c >= '0' && c <= '9') || c == '-'))
                return -1;
            out[used++] = c;
        }
        if (dot == NULL) {
            label += length;
            break;
        }
        label = dot + 1;
        if (*label == '\0')
            break;
    }

    if (used + 1 > out_size)
        return -1;
    out[used++] = 0;
    *written = used;
    return 0;
}

static int dns_query_a(const char *host, struct in_addr *address)
{
    unsigned char packet[512];
    unsigned char encoded_name[256];
    struct sockaddr_in dns_address;
    size_t name_length = 0;
    size_t packet_length;
    size_t offset;
    unsigned short query_id;
    int sock = -1;
    int sent;
    int received = -1;
    int waited_ms;
    unsigned int questions;
    unsigned int answers;
    unsigned int i;
    int result = -1;

    crtube_log("DNS A query %s: using connected UDP socket\n",
               host != NULL ? host : "(null)");

    if (encode_dns_name(host, encoded_name, sizeof(encoded_name), &name_length) != 0) {
        g_http_last_errno = EINVAL;
        crtube_log("DNS A query %s: invalid hostname\n", host);
        return -1;
    }

    memset(packet, 0, sizeof(packet));
    query_id = ++g_dns_query_id;
    packet[0] = (unsigned char)(query_id >> 8);
    packet[1] = (unsigned char)query_id;
    packet[2] = 0x01;
    packet[5] = 0x01;
    memcpy(packet + 12, encoded_name, name_length);
    packet_length = 12 + name_length;
    packet[packet_length++] = 0x00;
    packet[packet_length++] = 0x01;
    packet[packet_length++] = 0x00;
    packet[packet_length++] = 0x01;

    memset(&dns_address, 0, sizeof(dns_address));
    dns_address.sin_family = AF_INET;
    if (parse_ipv4_address(g_http_dns_server, &dns_address.sin_addr) != 0) {
        g_http_last_errno = EHOSTUNREACH;
        return -1;
    }
    dns_address.sin_port = htons(53);

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    crtube_log("DNS A query %s: socket=%d errno=%d\n", host, sock, errno);
    if (sock < 0) {
        g_http_last_errno = errno;
        return -1;
    }

    /* Use the connected UDP path: PS2IPS recvfrom() has an IOP-side
       fromlen initialization defect. recv() avoids that RPC entry point. */
    errno = 0;
    if (connect(sock, (struct sockaddr *)&dns_address, sizeof(dns_address)) < 0) {
        g_http_last_errno = errno;
        crtube_log("DNS A query %s: UDP connect failed errno=%d\n", host, errno);
        goto done;
    }

    errno = 0;
    if (fcntl(sock, F_SETFL, O_NONBLOCK) < 0) {
        g_http_last_errno = errno;
        crtube_log("DNS A query %s: nonblocking setup failed errno=%d\n",
                   host, errno);
        goto done;
    }

    errno = 0;
    sent = send(sock, packet, packet_length, 0);
    crtube_log("DNS A query %s: send=%d expected=%u errno=%d\n",
               host, sent, (unsigned int)packet_length, errno);
    if (sent != (int)packet_length) {
        g_http_last_errno = sent < 0 ? errno : EIO;
        goto done;
    }

    memset(packet, 0, sizeof(packet));
    crtube_log("DNS A query %s: waiting up to 4s with recv() polling\n", host);
    for (waited_ms = 0; waited_ms < 4000; waited_ms += 100) {
        errno = 0;
        received = recv(sock, packet, sizeof(packet), 0);
        if (received >= 0)
            break;
        if (errno != EWOULDBLOCK && errno != EAGAIN) {
            g_http_last_errno = errno;
            crtube_log("DNS A query %s: recv failed errno=%d\n", host, errno);
            goto done;
        }
        DelayThread(100 * 1000);
    }

    crtube_log("DNS A query %s: recv=%d waited_ms=%d errno=%d\n",
               host, received, waited_ms, errno);
    if (received < 0) {
        g_http_last_errno = ETIMEDOUT;
        goto done;
    }
    if (received < 12) {
        g_http_last_errno = EPROTO;
        goto done;
    }

    crtube_log("DNS A response %s: id=0x%04x expected=0x%04x flags=0x%02x%02x rcode=%u qd=%u an=%u ns=%u ar=%u\n",
               host,
               (unsigned int)read_u16(packet), (unsigned int)query_id,
               (unsigned int)packet[2], (unsigned int)packet[3],
               (unsigned int)(packet[3] & 0x0F),
               (unsigned int)read_u16(packet + 4),
               (unsigned int)read_u16(packet + 6),
               (unsigned int)read_u16(packet + 8),
               (unsigned int)read_u16(packet + 10));
    {
        unsigned int dump_length = (unsigned int)received;
        unsigned int dump_index;
        if (dump_length > 64)
            dump_length = 64;
        for (dump_index = 0; dump_index < dump_length; dump_index += 16) {
            char line[80];
            unsigned int j;
            int used = snprintf(line, sizeof(line), "DNS A response %s: data[%u]=", host, dump_index);
            for (j = dump_index; j < dump_length && j < dump_index + 16; j++)
                used += snprintf(line + used, sizeof(line) - (size_t)used,
                                 "%02x%s", packet[j],
                                 (j + 1 == dump_length || j + 1 == dump_index + 16) ? "" : " ");
            crtube_log("%s\n", line);
        }
    }

    if (read_u16(packet) != query_id || (packet[2] & 0x80) == 0) {
        g_http_last_errno = EPROTO;
        crtube_log("DNS A response %s: invalid transaction ID or response flag\n", host);
        goto done;
    }
    if ((packet[3] & 0x0F) != 0) {
        g_http_last_errno = EHOSTUNREACH;
        crtube_log("DNS A response %s: server returned RCODE=%u\n",
                   host, (unsigned int)(packet[3] & 0x0F));
        goto done;
    }

    questions = read_u16(packet + 4);
    answers = read_u16(packet + 6);
    offset = 12;
    for (i = 0; i < questions; i++) {
        if (skip_dns_name(packet, (size_t)received, &offset) != 0 ||
            offset + 4 > (size_t)received) {
            g_http_last_errno = EPROTO;
            goto done;
        }
        offset += 4;
    }

    for (i = 0; i < answers; i++) {
        unsigned short type, klass, data_length;
        if (skip_dns_name(packet, (size_t)received, &offset) != 0 ||
            offset + 10 > (size_t)received) {
            g_http_last_errno = EPROTO;
            goto done;
        }
        type = read_u16(packet + offset);
        klass = read_u16(packet + offset + 2);
        data_length = read_u16(packet + offset + 8);
        offset += 10;
        if (offset + data_length > (size_t)received) {
            g_http_last_errno = EPROTO;
            goto done;
        }
        if (type == 1 && klass == 1 && data_length == 4) {
            memcpy(address, packet + offset, 4);
            result = 0;
            break;
        }
        offset += data_length;
    }
    if (result != 0)
        g_http_last_errno = EHOSTUNREACH;

done:
    if (sock >= 0) {
        int close_result;
        int close_errno;
        errno = 0;
        close_result = disconnect(sock);
        close_errno = errno;
        crtube_log("DNS A query %s: socket close=%d errno=%d; query result=%d query_errno=%d\n",
                   host, close_result, close_errno, result, g_http_last_errno);
    } else {
        crtube_log("DNS A query %s: no socket to close; query result=%d query_errno=%d\n",
                   host, result, g_http_last_errno);
    }
    return result;
}

static int resolve_ipv4(const char *host, struct in_addr *address)
{
    crtube_log("HTTP resolve: host=%s\n", host != NULL ? host : "(null)");
    if (parse_ipv4_address(host, address) == 0) {
        crtube_log("HTTP resolve: numeric IPv4 accepted\n");
        return 0;
    }

    g_http_last_stage = CRTUBE_HTTP_STAGE_DNS;
    crtube_log("HTTP resolve: DNS lookup starting for %s\n", host);
    if (dns_query_a(host, address) != 0) {
        crtube_log("HTTP resolve: DNS lookup failed errno=%d\n", g_http_last_errno);
        return -1;
    }
    crtube_log("HTTP resolve: DNS lookup succeeded\n");
    return 0;
}

int crtube_http_resolve_ipv4(const char *host, char *ip, size_t ip_size)
{
    struct in_addr address;
    const unsigned char *bytes;
    int written;

    if (host == NULL || ip == NULL || ip_size == 0)
        return -1;

    if (resolve_ipv4(host, &address) != 0)
        return -1;

    bytes = (const unsigned char *)&address.s_addr;
    written = snprintf(ip, ip_size, "%u.%u.%u.%u",
                       (unsigned int)bytes[0], (unsigned int)bytes[1],
                       (unsigned int)bytes[2], (unsigned int)bytes[3]);
    if (written < 0 || (size_t)written >= ip_size) {
        ip[0] = '\0';
        return -1;
    }

    return 0;
}

static char *find_header_end(char *response)
{
    return strstr(response, "\r\n\r\n");
}

static int parse_status_code(const char *response)
{
    int status = -1;

    if (response == NULL || sscanf(response, "HTTP/%*u.%*u %d", &status) != 1)
        return -1;
    if (status < 100 || status > 599)
        return -1;
    return status;
}

static int header_name_equal(const char *start, size_t length, const char *name)
{
    return strlen(name) == length && strncasecmp(start, name, length) == 0;
}

static int get_header_value(const char *response, const char *name,
                            char *value, size_t value_size)
{
    const char *line;

    if (response == NULL || name == NULL || value == NULL || value_size == 0)
        return -1;

    line = strstr(response, "\r\n");
    if (line == NULL)
        return -1;
    line += 2;

    while (*line != '\0' && !(line[0] == '\r' && line[1] == '\n')) {
        const char *line_end = strstr(line, "\r\n");
        const char *colon;
        const char *value_start;
        size_t value_length;

        if (line_end == NULL)
            return -1;
        colon = memchr(line, ':', (size_t)(line_end - line));
        if (colon != NULL && header_name_equal(line, (size_t)(colon - line), name)) {
            value_start = colon + 1;
            while (value_start < line_end && (*value_start == ' ' || *value_start == '\t'))
                value_start++;
            value_length = (size_t)(line_end - value_start);
            while (value_length > 0 &&
                   (value_start[value_length - 1] == ' ' || value_start[value_length - 1] == '\t'))
                value_length--;
            if (value_length >= value_size)
                value_length = value_size - 1;
            memcpy(value, value_start, value_length);
            value[value_length] = '\0';
            return 0;
        }
        line = line_end + 2;
    }
    return -1;
}

static long parse_content_length(const char *response)
{
    char value[32];
    char *end;
    unsigned long length;

    if (get_header_value(response, "Content-Length", value, sizeof(value)) != 0)
        return -1;
    errno = 0;
    length = strtoul(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' || length > 0x7fffffffUL)
        return -1;
    return (long)length;
}

static int close_http_socket(int sock)
{
    errno = 0;
    g_http_last_close_result = disconnect(sock);
    g_http_last_close_errno = errno;
    return g_http_last_close_result;
}

static int http_request_once(const char *host, unsigned short port,
                             const char *path, char *response,
                             size_t response_size)
{
    int sock = -1;
    struct sockaddr_in address;
    char request[768];
    int request_size;
    int sent = 0;
    size_t received = 0;
    long content_length = -1;
    size_t header_size = 0;
    int header_parsed = 0;
    int result = CRTUBE_HTTP_OK;

    if (response != NULL && response_size > 0)
        response[0] = '\0';

    if (host == NULL || path == NULL || response == NULL || response_size < 2 ||
        host[0] == '\0' || path[0] != '/' ||
        strchr(host, '\r') != NULL || strchr(host, '\n') != NULL ||
        strchr(path, '\r') != NULL || strchr(path, '\n') != NULL) {
        g_http_last_stage = CRTUBE_HTTP_STAGE_PARSE;
        g_http_last_errno = EINVAL;
        return CRTUBE_HTTP_ERR_URL;
    }

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    crtube_log("HTTP request: resolving %s\n", host);
    if (resolve_ipv4(host, &address.sin_addr) != 0)
        return CRTUBE_HTTP_ERR_DNS;
    crtube_log("HTTP request: resolved %s\n", host);
    g_http_last_stage = CRTUBE_HTTP_STAGE_NONE;

    crtube_log("HTTP TCP: creating socket for %s:%u\n",
               host, (unsigned int)port);
    errno = 0;
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    g_http_last_socket = sock;
    crtube_log("HTTP TCP: socket returned %d errno=%d\n", sock, errno);
    if (sock < 0) {
        g_http_last_stage = CRTUBE_HTTP_STAGE_SOCKET;
        g_http_last_errno = errno;
        return CRTUBE_HTTP_ERR_SOCKET;
    }

    crtube_log("HTTP TCP: connect starting for %s:%u socket=%d\n",
               host, (unsigned int)port, sock);
    errno = 0;
    g_http_last_connect_result = connect(sock, (struct sockaddr *)&address,
                                         sizeof(address));
    crtube_log("HTTP TCP: connect returned %d errno=%d\n",
               g_http_last_connect_result, errno);
    if (g_http_last_connect_result < 0) {
        socklen_t so_error_size = sizeof(g_http_last_so_error);
        g_http_last_stage = CRTUBE_HTTP_STAGE_CONNECT;
        g_http_last_errno = errno;
        errno = 0;
        g_http_last_so_error_result = getsockopt(
            sock, SOL_SOCKET, SO_ERROR, &g_http_last_so_error, &so_error_size);
        g_http_last_so_error_errno = errno;
        close_http_socket(sock);
        return CRTUBE_HTTP_ERR_CONNECT;
    }

    if (port == 80) {
        request_size = snprintf(request, sizeof(request),
            "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: CRTube-PS2\r\nAccept: */*\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n",
            path, host);
    } else {
        request_size = snprintf(request, sizeof(request),
            "GET %s HTTP/1.0\r\nHost: %s:%u\r\nUser-Agent: CRTube-PS2\r\nAccept: */*\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n",
            path, host, (unsigned int)port);
    }
    if (request_size < 0 || (size_t)request_size >= sizeof(request)) {
        g_http_last_stage = CRTUBE_HTTP_STAGE_SEND;
        g_http_last_errno = EMSGSIZE;
        close_http_socket(sock);
        return CRTUBE_HTTP_ERR_SEND;
    }

    crtube_log("HTTP TCP: sending request bytes=%d\n", request_size);
    while (sent < request_size) {
        int count;
        errno = 0;
        count = send(sock, request + sent, (size_t)(request_size - sent), 0);
        if (count <= 0) {
            g_http_last_stage = CRTUBE_HTTP_STAGE_SEND;
            g_http_last_errno = count < 0 ? errno : EPIPE;
            close_http_socket(sock);
            return CRTUBE_HTTP_ERR_SEND;
        }
        sent += count;
        g_http_last_bytes_sent += count;
    }
    crtube_log("HTTP TCP: request sent bytes=%d\n", sent);

    errno = 0;
    if (fcntl(sock, F_SETFL, O_NONBLOCK) < 0) {
        g_http_last_stage = CRTUBE_HTTP_STAGE_RECV;
        g_http_last_errno = errno;
        close_http_socket(sock);
        return CRTUBE_HTTP_ERR_RECV;
    }

    response[0] = '\0';
    while (received < response_size - 1) {
        int count = -1;
        int waited_ms;

        for (waited_ms = 0; waited_ms < 6000; waited_ms += 100) {
            errno = 0;
            count = recv(sock, response + received,
                         response_size - 1 - received, 0);
            if (count >= 0)
                break;
            DelayThread(100 * 1000);
        }

        if (count < 0) {
            if (received == 0) {
                g_http_last_stage = CRTUBE_HTTP_STAGE_RECV;
                g_http_last_errno = ETIMEDOUT;
                result = CRTUBE_HTTP_ERR_RECV;
            } else {
                g_http_last_truncated = 1;
            }
            break;
        }
        if (count == 0)
            break;

        received += (size_t)count;
        g_http_last_bytes_received += count;
        response[received] = '\0';

        if (!header_parsed) {
            char *header_end = find_header_end(response);
            if (header_end != NULL) {
                header_size = (size_t)(header_end - response) + 4;
                content_length = parse_content_length(response);
                header_parsed = 1;
                if (content_length == 0)
                    break;
            }
        }

        if (header_parsed && content_length >= 0 &&
            received >= header_size + (size_t)content_length)
            break;
    }

    response[received] = '\0';
    if (received == response_size - 1 &&
        !(header_parsed && content_length >= 0 &&
          received >= header_size + (size_t)content_length))
        g_http_last_truncated = 1;

    if (result == CRTUBE_HTTP_OK) {
        g_http_last_status = parse_status_code(response);
        if (g_http_last_status < 0) {
            g_http_last_stage = CRTUBE_HTTP_STAGE_PARSE;
            result = CRTUBE_HTTP_ERR_PROTOCOL;
        }
    }

    close_http_socket(sock);
    return result;
}


#ifdef CRTUBE_ENABLE_YOUTUBE_PROBE
static int https_request_once(const char *method, const char *host,
                              unsigned short port, const char *path,
                              const char *content_type, const char *body,
                              char *response, size_t response_size)
{
    char request[4096];
    size_t body_size = body != NULL ? strlen(body) : 0;
    int request_size;
    int header_size;
    int result;

    if (method == NULL || (strcmp(method, "GET") != 0 &&
                           strcmp(method, "POST") != 0))
        return CRTUBE_HTTP_ERR_URL;
    if (strcmp(method, "POST") == 0 && content_type == NULL)
        return CRTUBE_HTTP_ERR_URL;
    if (content_type != NULL &&
        (strchr(content_type, '\r') != NULL || strchr(content_type, '\n') != NULL))
        return CRTUBE_HTTP_ERR_URL;

    if (port != 443)
        return CRTUBE_HTTP_ERR_URL;

    if (!crtube_bearssl_entropy_available()) {
        int entropy = crtube_bearssl_entropy_probe();
        if ((entropy & CRTUBE_BEARSSL_ENTROPY_GETRANDOM_READ) == 0) {
            g_http_last_stage = CRTUBE_HTTP_STAGE_SOCKET;
            g_http_last_errno = EACCES;
            return CRTUBE_HTTP_ERR_SOCKET;
        }
    }

    if (!crtube_bearssl_clock_available()) {
        if (crtube_sntp_sync() != 0) {
            g_http_last_stage = CRTUBE_HTTP_STAGE_CONNECT;
            g_http_last_errno = ETIMEDOUT;
            return CRTUBE_HTTP_ERR_CONNECT;
        }
    }

    if (strcmp(method, "POST") == 0) {
        header_size = snprintf(request, sizeof(request),
            "POST %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "User-Agent: CRTube-PS2\r\n"
            "Content-Type: %s\r\n"
            "Accept: application/json\r\n"
            "Accept-Encoding: identity\r\n"
            "Content-Length: %lu\r\n"
            "Connection: close\r\n\r\n",
            path, host, content_type, (unsigned long)body_size);
    } else {
        header_size = snprintf(request, sizeof(request),
            "GET %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "User-Agent: CRTube-PS2\r\n"
            "Accept: */*\r\n"
            "Accept-Encoding: identity\r\n"
            "Connection: close\r\n\r\n",
            path, host);
    }
    if (header_size < 0 || (size_t)header_size >= sizeof(request) ||
        body_size > sizeof(request) - (size_t)header_size - 1) {
        g_http_last_stage = CRTUBE_HTTP_STAGE_SEND;
        g_http_last_errno = EMSGSIZE;
        return CRTUBE_HTTP_ERR_SEND;
    }
    request_size = header_size;
    if (body_size != 0) {
        memcpy(request + request_size, body, body_size);
        request_size += (int)body_size;
    }
    request[request_size] = '\0';

    g_http_last_stage = CRTUBE_HTTP_STAGE_NONE;
    g_http_last_bytes_sent = request_size;
    result = crtube_bearssl_exchange(host, request, response, response_size);
    if (result < 0) {
        g_http_last_stage = CRTUBE_HTTP_STAGE_RECV;
        g_http_last_errno = EIO;
        if (result == CRTUBE_BEARSSL_ERR_DNS)
            g_http_last_stage = CRTUBE_HTTP_STAGE_DNS;
        else if (result == CRTUBE_BEARSSL_ERR_SOCKET ||
                 result == CRTUBE_BEARSSL_ERR_CONNECT)
            g_http_last_stage = CRTUBE_HTTP_STAGE_CONNECT;
        else if (result == CRTUBE_BEARSSL_ERR_ENTROPY)
            g_http_last_errno = EACCES;
        else if (result == CRTUBE_BEARSSL_ERR_CLOCK)
            g_http_last_errno = EINVAL;
        return result == CRTUBE_BEARSSL_ERR_DNS ? CRTUBE_HTTP_ERR_DNS :
               result == CRTUBE_BEARSSL_ERR_CONNECT ? CRTUBE_HTTP_ERR_CONNECT :
               result == CRTUBE_BEARSSL_ERR_SOCKET ? CRTUBE_HTTP_ERR_SOCKET :
               result == CRTUBE_BEARSSL_ERR_ENTROPY ? CRTUBE_HTTP_ERR_SOCKET :
               result == CRTUBE_BEARSSL_ERR_CLOCK ? CRTUBE_HTTP_ERR_CONNECT :
               CRTUBE_HTTP_ERR_RECV;
    }

    g_http_last_bytes_received = result;
    if ((size_t)result >= response_size - 1)
        g_http_last_truncated = 1;
    g_http_last_status = parse_status_code(response);
    if (g_http_last_status < 0) {
        g_http_last_stage = CRTUBE_HTTP_STAGE_PARSE;
        return CRTUBE_HTTP_ERR_PROTOCOL;
    }
    return CRTUBE_HTTP_OK;
}
#endif

static int parse_http_url(const char *url, char *host, size_t host_size,
                          unsigned short *port, char *path, size_t path_size,
                          int *is_https)
{
    const char *cursor;
    const char *host_start;
    const char *host_end;
    const char *path_start;
    size_t host_length;
    unsigned long parsed_port;
    char *port_end;
    int secure;

    if (url == NULL || host == NULL || port == NULL || path == NULL)
        return CRTUBE_HTTP_ERR_URL;
    secure = strncmp(url, "https://", 8) == 0;
    if (!secure && strncmp(url, "http://", 7) != 0)
        return CRTUBE_HTTP_ERR_URL;

    parsed_port = secure ? 443 : 80;
    if (is_https != NULL)
        *is_https = secure;
    cursor = url + (secure ? 8 : 7);
    host_start = cursor;
    while (*cursor != '\0' && *cursor != '/' && *cursor != '?' && *cursor != ':' && *cursor != '#')
        cursor++;
    host_end = cursor;
    host_length = (size_t)(host_end - host_start);
    if (host_length == 0 || host_length >= host_size)
        return CRTUBE_HTTP_ERR_URL;
    memcpy(host, host_start, host_length);
    host[host_length] = '\0';

    if (*cursor == ':') {
        cursor++;
        parsed_port = strtoul(cursor, &port_end, 10);
        if (port_end == cursor || parsed_port == 0 || parsed_port > 65535)
            return CRTUBE_HTTP_ERR_URL;
        cursor = port_end;
        if (*cursor != '\0' && *cursor != '/' && *cursor != '?' && *cursor != '#')
            return CRTUBE_HTTP_ERR_URL;
    }
    *port = (unsigned short)parsed_port;
    *is_https = secure;

    path_start = cursor;
    if (*path_start == '\0' || *path_start == '#') {
        if (path_size < 2)
            return CRTUBE_HTTP_ERR_URL;
        strcpy(path, "/");
    } else if (*path_start == '?') {
        if (strlen(path_start) + 1 >= path_size)
            return CRTUBE_HTTP_ERR_URL;
        path[0] = '/';
        strcpy(path + 1, path_start);
    } else {
        const char *fragment = strchr(path_start, '#');
        size_t length = fragment != NULL ?
                        (size_t)(fragment - path_start) : strlen(path_start);
        if (length == 0 || length >= path_size)
            return CRTUBE_HTTP_ERR_URL;
        memcpy(path, path_start, length);
        path[length] = '\0';
    }
    return CRTUBE_HTTP_OK;
}

static int build_redirect_url(const char *current_url, const char *location,
                              char *next_url, size_t next_url_size)
{
    char host[128];
    char path[384];
    unsigned short port;
    int result;
    int written;

    if (strncmp(location, "https://", 8) == 0 ||
        strncmp(location, "http://", 7) == 0) {
        if (strlen(location) >= next_url_size)
            return CRTUBE_HTTP_ERR_URL;
        strcpy(next_url, location);
        return CRTUBE_HTTP_OK;
    }
    if (strncmp(location, "//", 2) == 0) {
        const char *scheme = strncmp(current_url, "https://", 8) == 0 ?
                             "https:" : "http:";
        int written = snprintf(next_url, next_url_size, "%s%s", scheme, location);
        if (written < 0 || (size_t)written >= next_url_size)
            return CRTUBE_HTTP_ERR_URL;
        return CRTUBE_HTTP_OK;
    }
    result = parse_http_url(current_url, host, sizeof(host), &port,
                            path, sizeof(path), NULL);
    if (result != CRTUBE_HTTP_OK)
        return result;

    {
        const char *scheme = strncmp(current_url, "https://", 8) == 0 ?
                             "https://" : "http://";
        unsigned short default_port = scheme[4] == 's' ? 443 : 80;
        if (location[0] == '/') {
            if (port == default_port)
                written = snprintf(next_url, next_url_size, "%s%s%s", scheme, host, location);
            else
                written = snprintf(next_url, next_url_size, "%s%s:%u%s",
                                   scheme, host, (unsigned int)port, location);
        } else {
            char *last_slash = strrchr(path, '/');
            size_t directory_length = last_slash != NULL ?
                                      (size_t)(last_slash - path + 1) : 1;
            path[directory_length] = '\0';
            if (port == default_port)
                written = snprintf(next_url, next_url_size, "%s%s%.*s%s",
                                   scheme, host, (int)directory_length, path, location);
            else
                written = snprintf(next_url, next_url_size, "%s%s:%u%.*s%s",
                                   scheme, host, (unsigned int)port,
                                   (int)directory_length, path, location);
        }
    }

    if (written < 0 || (size_t)written >= next_url_size)
        return CRTUBE_HTTP_ERR_URL;
    return CRTUBE_HTTP_OK;
}

int crtube_http_get(const char *host, unsigned short port,
                    const char *path, char *response, size_t response_size)
{
    reset_http_state();
    return http_request_once(host, port, path, response, response_size);
}

int crtube_http_get_ipv4(const char *ip, unsigned short port,
                         const char *path, char *response, size_t response_size)
{
    return crtube_http_get(ip, port, path, response, response_size);
}

int crtube_http_get_url(const char *url, char *response, size_t response_size)
{
    char current_url[512];
    char next_url[512];
    int redirects;
    int result;

    reset_http_state();
    crtube_log("HTTP URL request entered: %s\n", url != NULL ? url : "(null)");
    if (response != NULL && response_size > 0)
        response[0] = '\0';
    if (url == NULL || response == NULL || response_size < 2 || strlen(url) >= sizeof(current_url))
        return CRTUBE_HTTP_ERR_URL;
    strcpy(current_url, url);

    for (redirects = 0; redirects <= 3; redirects++) {
        char host[128];
        char path[384];
        char location[384];
        unsigned short port;
        int is_https = 0;

        crtube_log("HTTP URL request: parsing %s\n", current_url);
        result = parse_http_url(current_url, host, sizeof(host), &port,
                                path, sizeof(path), &is_https);
        if (result != CRTUBE_HTTP_OK) {
            g_http_last_stage = CRTUBE_HTTP_STAGE_PARSE;
            return result;
        }
        crtube_log("HTTP URL request: host=%s port=%u path=%s\n",
                   host, (unsigned int)port, path);

        if (is_https) {
#ifdef CRTUBE_ENABLE_YOUTUBE_PROBE
            result = https_request_once("GET", host, port, path, NULL, NULL,
                                         response, response_size);
#else
            result = CRTUBE_HTTP_ERR_HTTPS_UNSUPPORTED;
#endif
        } else {
            result = http_request_once(host, port, path, response, response_size);
        }
        if (result != CRTUBE_HTTP_OK)
            return result;

        if (g_http_last_status != 301 && g_http_last_status != 302 &&
            g_http_last_status != 303 && g_http_last_status != 307 &&
            g_http_last_status != 308)
            return CRTUBE_HTTP_OK;

        if (get_header_value(response, "Location", location, sizeof(location)) != 0)
            return CRTUBE_HTTP_OK;

        if (redirects == 3) {
            g_http_last_stage = CRTUBE_HTTP_STAGE_REDIRECT;
            return CRTUBE_HTTP_ERR_REDIRECT;
        }

        result = build_redirect_url(current_url, location, next_url, sizeof(next_url));
        if (result != CRTUBE_HTTP_OK) {
            g_http_last_stage = CRTUBE_HTTP_STAGE_REDIRECT;
            return result;
        }
        strcpy(current_url, next_url);
        g_http_last_redirects++;
    }

    g_http_last_stage = CRTUBE_HTTP_STAGE_REDIRECT;
    return CRTUBE_HTTP_ERR_REDIRECT;
}

#ifdef CRTUBE_ENABLE_YOUTUBE_PROBE
/* Decode HTTP/1.1 chunked transfer framing in-place at the HTTP API boundary.
   The returned response keeps its status/headers and replaces Transfer-Encoding
   with Content-Length, so callers always receive a normal contiguous body. */
static int http_is_chunked(const char *value)
{
    const char *start;
    const char *end;
    if (value == NULL)
        return 0;
    start = strrchr(value, ',');
    start = start != NULL ? start + 1 : value;
    while (*start == ' ' || *start == '\t')
        start++;
    end = start + strlen(start);
    while (end > start && (end[-1] == ' ' || end[-1] == '\t'))
        end--;
    return (size_t)(end - start) == 7 && strncasecmp(start, "chunked", 7) == 0;
}

/* Parse one chunk stream without copying. Returns decoded byte count and the
   offset immediately after the trailer terminator. */
static int http_scan_chunks(const char *raw, size_t raw_size,
                            size_t body_offset, size_t *decoded_size,
                            size_t *end_offset)
{
    size_t pos = body_offset;
    size_t total = 0;

    for (;;) {
        size_t line_end = pos;
        size_t digits_end;
        size_t chunk_size = 0;
        size_t i;

        while (line_end + 1 < raw_size &&
               !(raw[line_end] == '\r' && raw[line_end + 1] == '\n'))
            line_end++;
        if (line_end + 1 >= raw_size)
            return -1;
        digits_end = pos;
        while (digits_end < line_end && raw[digits_end] != ';' &&
               raw[digits_end] != ' ' && raw[digits_end] != '\t')
            digits_end++;
        if (digits_end == pos)
            return -1;
        for (i = pos; i < digits_end; i++) {
            unsigned int digit;
            unsigned char c = (unsigned char)raw[i];
            if (c >= '0' && c <= '9') digit = c - '0';
            else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
            else return -1;
            if (chunk_size > ((size_t)-1 - digit) / 16)
                return -1;
            chunk_size = chunk_size * 16 + digit;
        }
        /* Permit optional chunk extensions, but reject malformed whitespace
           before an extension instead of silently accepting a bad size. */
        if (digits_end < line_end && raw[digits_end] != ';')
            return -1;
        pos = line_end + 2;

        if (chunk_size == 0) {
            /* Last-chunk is followed by zero or more trailer fields and an
               empty line. */
            for (;;) {
                size_t trailer_end = pos;
                while (trailer_end + 1 < raw_size &&
                       !(raw[trailer_end] == '\r' && raw[trailer_end + 1] == '\n'))
                    trailer_end++;
                if (trailer_end + 1 >= raw_size)
                    return -1;
                if (trailer_end == pos) {
                    *decoded_size = total;
                    *end_offset = trailer_end + 2;
                    return 0;
                }
                if (memchr(raw + pos, ':', trailer_end - pos) == NULL)
                    return -1;
                pos = trailer_end + 2;
            }
        }

        if (chunk_size > raw_size - pos || raw_size - pos - chunk_size < 2)
            return -1;
        if (raw[pos + chunk_size] != '\r' || raw[pos + chunk_size + 1] != '\n')
            return -1;
        if (total > (size_t)-1 - chunk_size)
            return -1;
        total += chunk_size;
        pos += chunk_size + 2;
    }
}

static int http_dechunk_response(char **response, size_t *response_size)
{
    char transfer_encoding[64];
    char *raw;
    size_t raw_size;
    char *header_end;
    size_t old_header_size;
    size_t decoded_size = 0;
    size_t chunks_end = 0;
    size_t new_header_size = 0;
    size_t source_pos;
    char *new_response;
    char length_line[64];
    int length_line_size;

    if (response == NULL || response_size == NULL)
        return -1;
    raw = *response;
    raw_size = *response_size;
    if (raw == NULL)
        return -1;
    if (get_header_value(raw, "Transfer-Encoding", transfer_encoding,
                         sizeof(transfer_encoding)) != 0 ||
        !http_is_chunked(transfer_encoding))
        return 0;

    header_end = find_header_end(raw);
    if (header_end == NULL)
        return -1;
    old_header_size = (size_t)(header_end - raw) + 4;
    if (http_scan_chunks(raw, raw_size, old_header_size,
                         &decoded_size, &chunks_end) != 0 || chunks_end != raw_size)
        return -1;

    /* Rebuild headers, dropping Transfer-Encoding and preserving every other
       response header. Content-Length now describes the dechunked entity. */
    source_pos = 0;
    while (source_pos < old_header_size - 2) {
        const char *line_end = strstr(raw + source_pos, "\r\n");
        size_t line_size;
        const char *colon;
        int is_transfer = 0;
        if (line_end == NULL || (size_t)(line_end - raw) >= old_header_size)
            return -1;
        line_size = (size_t)(line_end - (raw + source_pos));
        colon = memchr(raw + source_pos, ':', line_size);
        if (colon != NULL &&
            (header_name_equal(raw + source_pos,
                               (size_t)(colon - (raw + source_pos)), "Transfer-Encoding") ||
             header_name_equal(raw + source_pos,
                               (size_t)(colon - (raw + source_pos)), "Content-Length")))
            is_transfer = 1;
        if (!is_transfer) {
            if (new_header_size > (size_t)-1 - line_size - 2)
                return -1;
            new_header_size += line_size + 2;
        }
        source_pos += line_size + 2;
        if (line_size == 0)
            break;
    }
    length_line_size = snprintf(length_line, sizeof(length_line),
                                "Content-Length: %lu\r\n", (unsigned long)decoded_size);
    if (length_line_size < 0 || (size_t)length_line_size >= sizeof(length_line) ||
        new_header_size > (size_t)-1 - (size_t)length_line_size - decoded_size - 3)
        return -1;
    new_response = (char *)malloc(new_header_size + (size_t)length_line_size + decoded_size + 3);
    if (new_response == NULL)
        return -2;

    source_pos = 0;
    new_header_size = 0;
    while (source_pos < old_header_size - 2) {
        const char *line_end = strstr(raw + source_pos, "\r\n");
        size_t line_size = (size_t)(line_end - (raw + source_pos));
        const char *colon = memchr(raw + source_pos, ':', line_size);
        int is_transfer = colon != NULL &&
            (header_name_equal(raw + source_pos, (size_t)(colon - (raw + source_pos)),
                               "Transfer-Encoding") ||
             header_name_equal(raw + source_pos, (size_t)(colon - (raw + source_pos)),
                               "Content-Length"));
        if (!is_transfer) {
            memcpy(new_response + new_header_size, raw + source_pos, line_size + 2);
            new_header_size += line_size + 2;
        }
        source_pos += line_size + 2;
        if (line_size == 0)
            break;
    }
    memcpy(new_response + new_header_size, length_line, (size_t)length_line_size);
    new_header_size += (size_t)length_line_size;
    memcpy(new_response + new_header_size, "\r\n", 2);
    new_header_size += 2;

    source_pos = old_header_size;
    {
        size_t output_pos = new_header_size;
        while (source_pos < raw_size) {
            size_t line_end = source_pos;
            size_t digits_end;
            size_t chunk_size = 0;
            size_t i;
            while (!(raw[line_end] == '\r' && raw[line_end + 1] == '\n'))
                line_end++;
            digits_end = source_pos;
            while (digits_end < line_end && raw[digits_end] != ';')
                digits_end++;
            for (i = source_pos; i < digits_end; i++) {
                unsigned char c = (unsigned char)raw[i];
                unsigned int digit = c <= '9' ? c - '0' :
                    (c <= 'F' ? c - 'A' + 10 : c - 'a' + 10);
                chunk_size = chunk_size * 16 + digit;
            }
            source_pos = line_end + 2;
            if (chunk_size == 0)
                break;
            memcpy(new_response + output_pos, raw + source_pos, chunk_size);
            output_pos += chunk_size;
            source_pos += chunk_size + 2;
        }
        new_response[output_pos] = '\0';
        *response_size = output_pos;
    }
    free(raw);
    *response = new_response;
    return 1;
}

static int https_request_once_alloc(const char *method, const char *host,
                                    unsigned short port, const char *path,
                                    const char *content_type, const char *body,
                                    char **response, size_t *response_size)
{
    char header[1024];
    char *request;
    size_t body_size = body != NULL ? strlen(body) : 0;
    int header_size;
    int result;

    *response = NULL;
    *response_size = 0;
    if (port != 443 || method == NULL ||
        (strcmp(method, "GET") != 0 && strcmp(method, "POST") != 0) ||
        (strcmp(method, "POST") == 0 && content_type == NULL) ||
        (content_type != NULL && (strchr(content_type, '\r') != NULL ||
                                  strchr(content_type, '\n') != NULL)))
        return CRTUBE_HTTP_ERR_URL;

    if (!crtube_bearssl_entropy_available()) {
        int entropy = crtube_bearssl_entropy_probe();
        if ((entropy & CRTUBE_BEARSSL_ENTROPY_GETRANDOM_READ) == 0) {
            g_http_last_stage = CRTUBE_HTTP_STAGE_SOCKET;
            g_http_last_errno = EACCES;
            return CRTUBE_HTTP_ERR_SOCKET;
        }
    }
    if (!crtube_bearssl_clock_available() && crtube_sntp_sync() != 0) {
        g_http_last_stage = CRTUBE_HTTP_STAGE_CONNECT;
        g_http_last_errno = ETIMEDOUT;
        return CRTUBE_HTTP_ERR_CONNECT;
    }

    if (strcmp(method, "POST") == 0) {
        header_size = snprintf(header, sizeof(header),
            "POST %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: CRTube-PS2\r\n"
            "Content-Type: %s\r\nAccept: application/json\r\n"
            "Accept-Encoding: identity\r\nContent-Length: %lu\r\n"
            "Connection: close\r\n\r\n",
            path, host, content_type, (unsigned long)body_size);
    } else {
        header_size = snprintf(header, sizeof(header),
            "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: CRTube-PS2\r\n"
            "Accept: */*\r\nAccept-Encoding: identity\r\n"
            "Connection: close\r\n\r\n", path, host);
    }
    if (header_size < 0 || (size_t)header_size >= sizeof(header) ||
        body_size > 256u * 1024u || (size_t)header_size > (size_t)-1 - body_size - 1) {
        g_http_last_stage = CRTUBE_HTTP_STAGE_SEND;
        g_http_last_errno = EMSGSIZE;
        return CRTUBE_HTTP_ERR_SEND;
    }
    request = (char *)malloc((size_t)header_size + body_size + 1);
    if (request == NULL)
        return CRTUBE_HTTP_ERR_SEND;
    memcpy(request, header, (size_t)header_size);
    if (body_size != 0)
        memcpy(request + header_size, body, body_size);
    request[(size_t)header_size + body_size] = '\0';

    g_http_last_stage = CRTUBE_HTTP_STAGE_NONE;
    g_http_last_bytes_sent = header_size + (int)body_size;
    result = crtube_bearssl_exchange_alloc(host, request, response, response_size);
    free(request);
    if (result >= 0 && *response != NULL) {
        int decode_result = http_dechunk_response(response, response_size);
        if (decode_result < 0) {
            free(*response);
            *response = NULL;
            *response_size = 0;
            g_http_last_stage = CRTUBE_HTTP_STAGE_PARSE;
            g_http_last_errno = decode_result == -2 ? ENOMEM : EPROTO;
            return decode_result == -2 ? CRTUBE_HTTP_ERR_RECV : CRTUBE_HTTP_ERR_PROTOCOL;
        }
        if (decode_result > 0) {
            char *decoded_header_end = find_header_end(*response);
            size_t decoded_header_size = decoded_header_end != NULL ?
                (size_t)(decoded_header_end - *response) + 4 : *response_size;
            crtube_log("HTTP chunked response decoded: body_bytes=%lu\n",
                       (unsigned long)(*response_size - decoded_header_size));
        }
    }
    if (result < 0) {
        if (*response != NULL) {
            free(*response);
            *response = NULL;
            *response_size = 0;
        }
        g_http_last_stage = CRTUBE_HTTP_STAGE_RECV;
        g_http_last_errno = EIO;
        if (result == CRTUBE_BEARSSL_ERR_DNS)
            g_http_last_stage = CRTUBE_HTTP_STAGE_DNS;
        else if (result == CRTUBE_BEARSSL_ERR_SOCKET ||
                 result == CRTUBE_BEARSSL_ERR_CONNECT)
            g_http_last_stage = CRTUBE_HTTP_STAGE_CONNECT;
        else if (result == CRTUBE_BEARSSL_ERR_ENTROPY)
            g_http_last_errno = EACCES;
        else if (result == CRTUBE_BEARSSL_ERR_CLOCK)
            g_http_last_errno = EINVAL;
        else if (result == CRTUBE_BEARSSL_ERR_TOO_LARGE)
            g_http_last_errno = EMSGSIZE;
        return result == CRTUBE_BEARSSL_ERR_DNS ? CRTUBE_HTTP_ERR_DNS :
               result == CRTUBE_BEARSSL_ERR_CONNECT ? CRTUBE_HTTP_ERR_CONNECT :
               result == CRTUBE_BEARSSL_ERR_SOCKET ? CRTUBE_HTTP_ERR_SOCKET :
               result == CRTUBE_BEARSSL_ERR_ENTROPY ? CRTUBE_HTTP_ERR_SOCKET :
               result == CRTUBE_BEARSSL_ERR_CLOCK ? CRTUBE_HTTP_ERR_CONNECT :
               CRTUBE_HTTP_ERR_RECV;
    }
    g_http_last_bytes_received = result;
    g_http_last_status = parse_status_code(*response);
    if (g_http_last_status >= 0) {
        char *header_end = find_header_end(*response);
        long expected_body = parse_content_length(*response);
        if (header_end != NULL && expected_body >= 0) {
            size_t header_bytes = (size_t)(header_end - *response) + 4;
            size_t actual_body = *response_size > header_bytes ?
                                 *response_size - header_bytes : 0;
            if (actual_body < (size_t)expected_body) {
                free(*response);
                *response = NULL;
                *response_size = 0;
                g_http_last_stage = CRTUBE_HTTP_STAGE_RECV;
                g_http_last_errno = EPIPE;
                return CRTUBE_HTTP_ERR_RECV;
            }
        }
    }
    if (g_http_last_status < 0) {
        free(*response);
        *response = NULL;
        *response_size = 0;
        g_http_last_stage = CRTUBE_HTTP_STAGE_PARSE;
        return CRTUBE_HTTP_ERR_PROTOCOL;
    }
    return CRTUBE_HTTP_OK;
}
#endif


int crtube_http_get_url_alloc(const char *url, char **response,
                              size_t *response_size)
{
    char current_url[512];
    char next_url[512];
    int redirects;
    int result;

    reset_http_state();
    if (response == NULL || response_size == NULL)
        return CRTUBE_HTTP_ERR_URL;
    *response = NULL;
    *response_size = 0;
    if (url == NULL || strlen(url) >= sizeof(current_url))
        return CRTUBE_HTTP_ERR_URL;
    strcpy(current_url, url);

    for (redirects = 0; redirects <= 3; redirects++) {
        char host[128], path[384], location[384];
        unsigned short port;
        int is_https = 0;

        result = parse_http_url(current_url, host, sizeof(host), &port,
                                path, sizeof(path), &is_https);
        if (result != CRTUBE_HTTP_OK || !is_https || port != 443) {
            g_http_last_stage = CRTUBE_HTTP_STAGE_PARSE;
            return result != CRTUBE_HTTP_OK ? result :
                   CRTUBE_HTTP_ERR_HTTPS_UNSUPPORTED;
        }
#ifdef CRTUBE_ENABLE_YOUTUBE_PROBE
        result = https_request_once_alloc("GET", host, port, path,
                                          NULL, NULL, response,
                                          response_size);
#else
        result = CRTUBE_HTTP_ERR_HTTPS_UNSUPPORTED;
#endif
        if (result != CRTUBE_HTTP_OK)
            return result;

        if (g_http_last_status != 301 && g_http_last_status != 302 &&
            g_http_last_status != 303 && g_http_last_status != 307 &&
            g_http_last_status != 308)
            return CRTUBE_HTTP_OK;

        if (get_header_value(*response, "Location", location,
                             sizeof(location)) != 0)
            return CRTUBE_HTTP_OK;

        if (redirects == 3) {
            free(*response);
            *response = NULL;
            *response_size = 0;
            g_http_last_stage = CRTUBE_HTTP_STAGE_REDIRECT;
            return CRTUBE_HTTP_ERR_REDIRECT;
        }

        result = build_redirect_url(current_url, location, next_url,
                                    sizeof(next_url));
        if (result != CRTUBE_HTTP_OK) {
            free(*response);
            *response = NULL;
            *response_size = 0;
            g_http_last_stage = CRTUBE_HTTP_STAGE_REDIRECT;
            return result;
        }

        {
            char redirect_host[128], redirect_path[384];
            unsigned short redirect_port;
            int redirect_https = 0;
            result = parse_http_url(next_url, redirect_host,
                                    sizeof(redirect_host), &redirect_port,
                                    redirect_path, sizeof(redirect_path),
                                    &redirect_https);
            if (result != CRTUBE_HTTP_OK || !redirect_https ||
                redirect_port != port ||
                strcasecmp(redirect_host, host) != 0) {
                free(*response);
                *response = NULL;
                *response_size = 0;
                g_http_last_stage = CRTUBE_HTTP_STAGE_REDIRECT;
                return CRTUBE_HTTP_ERR_REDIRECT;
            }
        }

        free(*response);
        *response = NULL;
        *response_size = 0;
        strcpy(current_url, next_url);
        g_http_last_redirects++;
    }

    g_http_last_stage = CRTUBE_HTTP_STAGE_REDIRECT;
    return CRTUBE_HTTP_ERR_REDIRECT;
}

int crtube_http_post_url_alloc(const char *url, const char *content_type,
                               const char *body, char **response,
                               size_t *response_size)
{
    char current_url[512];
    char next_url[512];
    const char *method = "POST";
    const char *request_content_type = content_type;
    const char *request_body = body;
    int redirects;
    int result;

    reset_http_state();
    if (response == NULL || response_size == NULL)
        return CRTUBE_HTTP_ERR_URL;
    *response = NULL;
    *response_size = 0;
    if (url == NULL || content_type == NULL || body == NULL ||
        strlen(url) >= sizeof(current_url) ||
        strchr(content_type, '\r') != NULL || strchr(content_type, '\n') != NULL)
        return CRTUBE_HTTP_ERR_URL;
    strcpy(current_url, url);

    for (redirects = 0; redirects <= 3; redirects++) {
        char host[128], path[384], location[384];
        unsigned short port;
        int is_https = 0;
        result = parse_http_url(current_url, host, sizeof(host), &port,
                                path, sizeof(path), &is_https);
        if (result != CRTUBE_HTTP_OK || !is_https || port != 443) {
            g_http_last_stage = CRTUBE_HTTP_STAGE_PARSE;
            return result != CRTUBE_HTTP_OK ? result : CRTUBE_HTTP_ERR_HTTPS_UNSUPPORTED;
        }
#ifdef CRTUBE_ENABLE_YOUTUBE_PROBE
        result = https_request_once_alloc(method, host, port, path,
                                          request_content_type, request_body,
                                          response, response_size);
#else
        result = CRTUBE_HTTP_ERR_HTTPS_UNSUPPORTED;
#endif
        if (result != CRTUBE_HTTP_OK)
            return result;

        if (g_http_last_status != 301 && g_http_last_status != 302 &&
            g_http_last_status != 303 && g_http_last_status != 307 &&
            g_http_last_status != 308)
            return CRTUBE_HTTP_OK;
        if (get_header_value(*response, "Location", location, sizeof(location)) != 0)
            return CRTUBE_HTTP_OK;
        if (redirects == 3) {
            free(*response); *response = NULL; *response_size = 0;
            g_http_last_stage = CRTUBE_HTTP_STAGE_REDIRECT;
            return CRTUBE_HTTP_ERR_REDIRECT;
        }
        result = build_redirect_url(current_url, location, next_url, sizeof(next_url));
        if (result != CRTUBE_HTTP_OK) {
            free(*response); *response = NULL; *response_size = 0;
            g_http_last_stage = CRTUBE_HTTP_STAGE_REDIRECT;
            return result;
        }
        {
            char redirect_host[128], redirect_path[384];
            unsigned short redirect_port;
            int redirect_https = 0;
            result = parse_http_url(next_url, redirect_host, sizeof(redirect_host),
                                    &redirect_port, redirect_path,
                                    sizeof(redirect_path), &redirect_https);
            if (result != CRTUBE_HTTP_OK || !redirect_https ||
                redirect_port != port || strcasecmp(redirect_host, host) != 0) {
                free(*response); *response = NULL; *response_size = 0;
                g_http_last_stage = CRTUBE_HTTP_STAGE_REDIRECT;
                return CRTUBE_HTTP_ERR_REDIRECT;
            }
        }
        free(*response); *response = NULL; *response_size = 0;
        if (g_http_last_status == 303) {
            method = "GET";
            request_content_type = NULL;
            request_body = NULL;
        }
        strcpy(current_url, next_url);
        g_http_last_redirects++;
    }
    g_http_last_stage = CRTUBE_HTTP_STAGE_REDIRECT;
    return CRTUBE_HTTP_ERR_REDIRECT;
}

int crtube_http_post_url(const char *url, const char *content_type,
                         const char *body, char *response, size_t response_size)
{
    char *dynamic_response = NULL;
    size_t dynamic_size = 0;
    size_t copy_size;
    int result;
    if (response == NULL || response_size < 2)
        return CRTUBE_HTTP_ERR_URL;
    response[0] = '\0';
    result = crtube_http_post_url_alloc(url, content_type, body,
                                        &dynamic_response, &dynamic_size);
    if (dynamic_response != NULL) {
        copy_size = dynamic_size;
        if (copy_size > response_size - 1) {
            copy_size = response_size - 1;
            g_http_last_truncated = 1;
        }
        memcpy(response, dynamic_response, copy_size);
        response[copy_size] = '\0';
        free(dynamic_response);
    }
    return result;
}
