#include <errno.h>
#include <stdio.h>
#include <string.h>

/* The application uses the PS2IPS RPC socket implementation on the IOP.
   This header enables the PS2SDK socket aliases for that RPC-backed API. */
#include <ps2ip.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "tcp_probe.h"

/* PS2SDK's PS2IPS libcglue backend does not install inet_ops.
   Consequently inet_addr() may return 0.0.0.0 on the EE.
   Parse dotted IPv4 directly into network-order bytes instead. */
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


int crtube_tcp_probe_ipv4(const char *ip, unsigned short port,
                          crtube_tcp_probe_result_t *result)
{
    int sock;
    int rc;
    int socket_error = 0;
    socklen_t socket_error_size = sizeof(socket_error);
    struct sockaddr_in address;

    if (result == NULL)
        return -1;

    memset(result, 0, sizeof(*result));
    result->socket_result = -1;
    result->connect_result = -1;
    result->so_error_result = -1;
    result->close_result = -1;

    if (ip == NULL)
        return -1;

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (parse_ipv4_address(ip, &address.sin_addr) != 0) {
        result->connect_errno = EINVAL;
        return -1;
    }

    errno = 0;
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    result->socket_result = sock;
    result->socket_errno = errno;
    if (sock < 0)
        return -1;

    errno = 0;
    rc = connect(sock, (struct sockaddr *)&address, sizeof(address));
    result->connect_result = rc;
    result->connect_errno = errno;

    /* Query SO_ERROR on both success and failure. This is the IOP lwIP
       socket's error value, not merely the EE-side RPC errno. */
    errno = 0;
    result->so_error_result = getsockopt(sock, SOL_SOCKET, SO_ERROR,
                                         &socket_error,
                                         &socket_error_size);
    result->so_error = socket_error;
    result->so_error_errno = errno;

    errno = 0;
    result->close_result = close(sock);
    result->close_errno = errno;

    return rc == 0 ? 0 : -1;
}
