#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/time.h>

/* Select libcglue socket aliases backed by the ps2ips IOP RPC stack.
   Include this before sys/socket.h so socket/connect/send/recv/select use PS2IPS. */
#include <ps2ip.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#include "http.h"
#include "debug_log.h"

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

/* libcglue provides select() for PS2IPS, but the PS2SDK socket header does not
   declare it in this SDK snapshot. Its fd_set/timeval types are SDK-defined. */
extern int select(int nfds, fd_set *readfds, fd_set *writefds,
                  fd_set *exceptfds, struct timeval *timeout);

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
    crtube_log("DNS A query %s: entered resolver\n", host != NULL ? host : "(null)");
    unsigned char packet[512];
    unsigned char encoded_name[256];
    struct sockaddr_in dns_address;
    struct sockaddr_in from_address;
    socklen_t from_length;
    size_t name_length = 0;
    size_t packet_length;
    size_t offset;
    unsigned short query_id;
    int sock = -1;
    int sent;
    int ready;
    int received;
    unsigned int questions;
    unsigned int answers;
    unsigned int i;
    int result = -1;

    if (encode_dns_name(host, encoded_name, sizeof(encoded_name), &name_length) != 0) {
        g_http_last_errno = EINVAL;
        crtube_log("DNS A query %s: invalid hostname\n", host);
        return -1;
    }
    crtube_log("DNS A query %s: encoded name length=%u\n",
               host, (unsigned int)name_length);

    memset(packet, 0, sizeof(packet));
    query_id = ++g_dns_query_id;
    packet[0] = (unsigned char)(query_id >> 8);
    packet[1] = (unsigned char)query_id;
    packet[2] = 0x01; /* recursion desired */
    packet[5] = 0x01; /* one question */
    memcpy(packet + 12, encoded_name, name_length);
    packet_length = 12 + name_length;
    packet[packet_length++] = 0x00; packet[packet_length++] = 0x01; /* A */
    packet[packet_length++] = 0x00; packet[packet_length++] = 0x01; /* IN */

    memset(&dns_address, 0, sizeof(dns_address));
    dns_address.sin_family = AF_INET;
    if (parse_ipv4_address(g_http_dns_server, &dns_address.sin_addr) != 0) {
        g_http_last_errno = EHOSTUNREACH;
        return -1;
    }
    dns_address.sin_port = htons(53);

    crtube_log("DNS A query %s: creating UDP socket\n", host);
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    crtube_log("DNS A query %s: socket=%d errno=%d\n", host, sock, errno);
    if (sock < 0) {
        g_http_last_errno = errno;
        return -1;
    }

    crtube_log("DNS A query %s: sending %u bytes to %s:53\n",
               host, (unsigned int)packet_length, g_http_dns_server);
    errno = 0;
    sent = sendto(sock, packet, packet_length, 0,
                  (struct sockaddr *)&dns_address, sizeof(dns_address));
    crtube_log("DNS A query %s: sendto=%d errno=%d\n", host, sent, errno);
    if (sent != (int)packet_length) {
        g_http_last_errno = sent < 0 ? errno : EIO;
        goto done;
    }

    {
        fd_set read_set;
        struct timeval timeout;
        FD_ZERO(&read_set);
        FD_SET(sock, &read_set);
        timeout.tv_sec = 4;
        timeout.tv_usec = 0;
        errno = 0;
        crtube_log("DNS A query %s: waiting for UDP response (4s)\n", host);
        ready = select(sock + 1, &read_set, NULL, NULL, &timeout);
        crtube_log("DNS A query %s: select=%d errno=%d\n", host, ready, errno);
    }
    if (ready <= 0) {
        g_http_last_errno = ready == 0 ? ETIMEDOUT : errno;
        goto done;
    }

    from_length = sizeof(from_address);
    crtube_log("DNS A query %s: receiving UDP response\n", host);
    errno = 0;
    received = recvfrom(sock, packet, sizeof(packet), 0,
                        (struct sockaddr *)&from_address, &from_length);
    crtube_log("DNS A query %s: recvfrom=%d errno=%d\n", host, received, errno);
    if (received < 12) {
        g_http_last_errno = EPROTO;
        goto done;
    }
    if (read_u16(packet) != query_id || (packet[2] & 0x80) == 0 ||
        (packet[3] & 0x0F) != 0) {
        g_http_last_errno = EHOSTUNREACH;
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
    crtube_log("DNS A query %s: closing socket=%d result=%d errno=%d\n",
               host, sock, result, g_http_last_errno);
    close(sock);
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

static int wait_readable(int sock)
{
    fd_set read_set;
    struct timeval timeout;
    int result;

    FD_ZERO(&read_set);
    FD_SET(sock, &read_set);
    timeout.tv_sec = 6;
    timeout.tv_usec = 0;

    errno = 0;
    result = select(sock + 1, &read_set, NULL, NULL, &timeout);
    if (result < 0)
        g_http_last_errno = errno;
    return result;
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
    g_http_last_close_result = close(sock);
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

    response[0] = '\0';
    while (received < response_size - 1) {
        int ready = wait_readable(sock);
        int count;

        if (ready == 0) {
            if (received == 0) {
                g_http_last_stage = CRTUBE_HTTP_STAGE_RECV;
                g_http_last_errno = ETIMEDOUT;
                result = CRTUBE_HTTP_ERR_RECV;
            } else {
                g_http_last_truncated = 1;
            }
            break;
        }
        if (ready < 0) {
            g_http_last_stage = CRTUBE_HTTP_STAGE_RECV;
            result = CRTUBE_HTTP_ERR_RECV;
            break;
        }

        errno = 0;
        count = recv(sock, response + received, response_size - 1 - received, 0);
        if (count == 0)
            break;
        if (count < 0) {
            g_http_last_stage = CRTUBE_HTTP_STAGE_RECV;
            g_http_last_errno = errno;
            result = CRTUBE_HTTP_ERR_RECV;
            break;
        }

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

static int parse_http_url(const char *url, char *host, size_t host_size,
                          unsigned short *port, char *path, size_t path_size)
{
    const char *cursor;
    const char *host_start;
    const char *host_end;
    const char *path_start;
    size_t host_length;
    unsigned long parsed_port = 80;
    char *port_end;

    if (url == NULL || host == NULL || port == NULL || path == NULL)
        return CRTUBE_HTTP_ERR_URL;
    if (strncmp(url, "https://", 8) == 0)
        return CRTUBE_HTTP_ERR_HTTPS_UNSUPPORTED;
    if (strncmp(url, "http://", 7) != 0)
        return CRTUBE_HTTP_ERR_URL;

    cursor = url + 7;
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
        size_t length = fragment != NULL ? (size_t)(fragment - path_start) : strlen(path_start);
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

    if (strncmp(location, "https://", 8) == 0)
        return CRTUBE_HTTP_ERR_HTTPS_UNSUPPORTED;
    if (strncmp(location, "//", 2) == 0) {
        int written = snprintf(next_url, next_url_size, "http:%s", location);
        if (written < 0 || (size_t)written >= next_url_size)
            return CRTUBE_HTTP_ERR_URL;
        return CRTUBE_HTTP_OK;
    }
    if (strncmp(location, "http://", 7) == 0) {
        if (strlen(location) >= next_url_size)
            return CRTUBE_HTTP_ERR_URL;
        strcpy(next_url, location);
        return CRTUBE_HTTP_OK;
    }

    result = parse_http_url(current_url, host, sizeof(host), &port,
                            path, sizeof(path));
    if (result != CRTUBE_HTTP_OK)
        return result;

    if (location[0] == '/') {
        if (port == 80)
            written = snprintf(next_url, next_url_size, "http://%s%s", host, location);
        else
            written = snprintf(next_url, next_url_size, "http://%s:%u%s",
                               host, (unsigned int)port, location);
    } else {
        char *last_slash = strrchr(path, '/');
        size_t directory_length = last_slash != NULL ?
                                  (size_t)(last_slash - path + 1) : 1;
        path[directory_length] = '\0';
        if (port == 80)
            written = snprintf(next_url, next_url_size, "http://%s%.*s%s",
                               host, (int)directory_length, path, location);
        else
            written = snprintf(next_url, next_url_size, "http://%s:%u%.*s%s",
                               host, (unsigned int)port,
                               (int)directory_length, path, location);
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

        crtube_log("HTTP URL request: parsing %s\n", current_url);
        result = parse_http_url(current_url, host, sizeof(host), &port,
                                path, sizeof(path));
        if (result != CRTUBE_HTTP_OK) {
            g_http_last_stage = CRTUBE_HTTP_STAGE_PARSE;
            return result;
        }
        crtube_log("HTTP URL request: host=%s port=%u path=%s\n",
                   host, (unsigned int)port, path);

        result = http_request_once(host, port, path, response, response_size);
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
