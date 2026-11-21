#ifndef CRTUBE_PS2_TCP_PROBE_H
#define CRTUBE_PS2_TCP_PROBE_H

typedef struct crtube_tcp_probe_result {
    int socket_result;
    int socket_errno;
    int connect_result;
    int connect_errno;
    int so_error_result;
    int so_error;
    int so_error_errno;
    int close_result;
    int close_errno;
} crtube_tcp_probe_result_t;

/* TCP handshake only: no HTTP, DNS, SMB payload or application data. */
int crtube_tcp_probe_ipv4(const char *ip, unsigned short port,
                          crtube_tcp_probe_result_t *result);

#endif
