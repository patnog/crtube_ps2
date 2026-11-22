#include <errno.h>
#include <fcntl.h>
#include <delaythread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

#include <libcdvd.h>
#include <ps2ip.h>

#include "http.h"
#include "sntp.h"
#include "bearssl_transport.h"
#include "debug_log.h"

#define SNTP_PACKET_SIZE       48
#define SNTP_PORT              123
#define SNTP_TIMEOUT_MS        5000
#define NTP_UNIX_EPOCH_DELTA   2208988800UL
#define PS2_RTC_UTC_OFFSET     (9L * 60L * 60L)
#define SNTP_MIN_UNIX_TIME     1577836800UL /* 2020-01-01 */
#define SNTP_MAX_UNIX_TIME     2082758399UL /* 2035-12-31 */
#define SNTP_MAX_SERVER_SKEW   120UL

/* getrandom() was verified by the preceding CRTube diagnostic patch. */
extern ssize_t getrandom(void *buffer, size_t length, unsigned int flags);

static uint32_t read_be32(const unsigned char *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static unsigned char to_bcd(unsigned int value)
{
    return (unsigned char)(((value / 10U) << 4) | (value % 10U));
}

static int parse_server_time(const unsigned char *packet,
                             const unsigned char *request_cookie,
                             uint32_t *unix_time)
{
    unsigned int leap;
    unsigned int version;
    unsigned int mode;
    unsigned int stratum;
    uint32_t ntp_seconds;
    uint32_t unix_seconds;

    if (packet == NULL || request_cookie == NULL || unix_time == NULL)
        return -1;

    leap = packet[0] >> 6;
    version = (packet[0] >> 3) & 7U;
    mode = packet[0] & 7U;
    stratum = packet[1];

    if (leap == 3 || version < 3 || mode != 4 ||
        stratum == 0 || stratum > 15)
        return -1;

    /* NTP servers must echo the client's transmit timestamp as originate. */
    if (memcmp(packet + 24, request_cookie, 8) != 0)
        return -1;

    ntp_seconds = read_be32(packet + 40);
    if (ntp_seconds <= NTP_UNIX_EPOCH_DELTA)
        return -1;

    unix_seconds = ntp_seconds - NTP_UNIX_EPOCH_DELTA;
    if (unix_seconds < SNTP_MIN_UNIX_TIME ||
        unix_seconds > SNTP_MAX_UNIX_TIME)
        return -1;

    *unix_time = unix_seconds;
    return 0;
}

static int query_server(const char *hostname, uint32_t *unix_time)
{
    unsigned char request[SNTP_PACKET_SIZE];
    unsigned char response[SNTP_PACKET_SIZE];
    char server_ip[16];
    struct sockaddr_in address;
    int sock = -1;
    int sent;
    int received = -1;
    int waited_ms;
    int result = -1;

    if (hostname == NULL || unix_time == NULL)
        return -1;

    if (crtube_http_resolve_ipv4(hostname, server_ip, sizeof(server_ip)) != 0) {
        crtube_log("SNTP %s: DNS resolution failed.\n", hostname);
        return -1;
    }

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(SNTP_PORT);
    {
        unsigned int a, b, c, d;
        unsigned char *bytes = (unsigned char *)&address.sin_addr.s_addr;
        if (sscanf(server_ip, "%u.%u.%u.%u", &a, &b, &c, &d) != 4 ||
            a > 255 || b > 255 || c > 255 || d > 255)
            return -1;
        bytes[0] = (unsigned char)a;
        bytes[1] = (unsigned char)b;
        bytes[2] = (unsigned char)c;
        bytes[3] = (unsigned char)d;
    }

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        crtube_log("SNTP %s: socket failed errno=%d.\n", hostname, errno);
        return -1;
    }

    if (connect(sock, (struct sockaddr *)&address, sizeof(address)) < 0) {
        crtube_log("SNTP %s: UDP connect failed errno=%d.\n", hostname, errno);
        goto done;
    }

    if (fcntl(sock, F_SETFL, O_NONBLOCK) < 0) {
        crtube_log("SNTP %s: nonblocking setup failed errno=%d.\n",
                   hostname, errno);
        goto done;
    }

    memset(request, 0, sizeof(request));
    request[0] = 0x23; /* LI=0, VN=4, Mode=3 (client). */
    if (getrandom(request + 40, 8, 0) != 8) {
        crtube_log("SNTP %s: secure request cookie unavailable.\n", hostname);
        goto done;
    }

    sent = send(sock, request, sizeof(request), 0);
    if (sent != (int)sizeof(request)) {
        crtube_log("SNTP %s: send failed result=%d errno=%d.\n",
                   hostname, sent, errno);
        goto done;
    }

    for (waited_ms = 0; waited_ms < SNTP_TIMEOUT_MS; waited_ms += 100) {
        errno = 0;
        received = recv(sock, response, sizeof(response), 0);
        if (received >= 0)
            break;
        if (errno != EWOULDBLOCK && errno != EAGAIN) {
            crtube_log("SNTP %s: recv failed errno=%d.\n", hostname, errno);
            goto done;
        }
        DelayThread(100 * 1000);
    }

    if (received < SNTP_PACKET_SIZE) {
        crtube_log("SNTP %s: timeout/short response bytes=%d.\n",
                   hostname, received);
        goto done;
    }

    if (parse_server_time(response, request + 40, unix_time) != 0) {
        crtube_log("SNTP %s: invalid server response.\n", hostname);
        goto done;
    }

    crtube_log("SNTP %s: server=%s unix_time=%lu stratum=%u.\n",
               hostname, server_ip, (unsigned long)*unix_time,
               (unsigned int)response[1]);
    result = 0;

done:
    if (sock >= 0)
        disconnect(sock);
    return result;
}

static int write_ps2_rtc(uint32_t unix_time)
{
    time_t ps2_rtc_time;
    struct tm *utc;
    sceCdCLOCK clock;
    int init_result;
    int write_result;

    /* PS2SDK's ps2time() interprets the hardware RTC as Japan Standard Time.
       Store UTC+9 so libc time() subsequently reports the correct UTC epoch. */
    ps2_rtc_time = (time_t)(unix_time + (uint32_t)PS2_RTC_UTC_OFFSET);
    utc = gmtime(&ps2_rtc_time);
    if (utc == NULL || utc->tm_year < 120 || utc->tm_year > 135)
        return -1;

    memset(&clock, 0, sizeof(clock));
    clock.second = to_bcd((unsigned int)utc->tm_sec);
    clock.minute = to_bcd((unsigned int)utc->tm_min);
    clock.hour = to_bcd((unsigned int)utc->tm_hour);
    clock.day = to_bcd((unsigned int)utc->tm_mday);
    clock.month = to_bcd((unsigned int)(utc->tm_mon + 1));
    clock.year = to_bcd((unsigned int)(utc->tm_year - 100));

    init_result = sceCdInit(SCECdINIT);
    if (init_result == 0) {
        crtube_log("SNTP RTC: sceCdInit failed.\n");
        return -1;
    }

    write_result = sceCdWriteClock(&clock);
    if (write_result == 0) {
        crtube_log("SNTP RTC: sceCdWriteClock failed.\n");
        return -1;
    }

    return 0;
}

int crtube_sntp_sync(void)
{
    uint32_t first_time = 0;
    uint32_t second_time = 0;
    uint32_t selected_time;
    uint32_t difference;

    crtube_log("SNTP: starting network time synchronization.\n");

    if (query_server("pool.ntp.org", &first_time) != 0)
        return -1;
    if (query_server("time.cloudflare.com", &second_time) != 0)
        return -1;

    difference = first_time > second_time ?
        first_time - second_time : second_time - first_time;
    if (difference > SNTP_MAX_SERVER_SKEW) {
        crtube_log("SNTP: servers disagree; skew=%lu seconds. RTC unchanged.\n",
                   (unsigned long)difference);
        return -1;
    }

    /* Both sources agree. Use the later timestamp to avoid setting time back. */
    selected_time = first_time > second_time ? first_time : second_time;

    /* The CDVD RTC write may not immediately update newlib time().
       BearSSL therefore receives the validated SNTP epoch directly. */
    if (write_ps2_rtc(selected_time) != 0) {
        crtube_log("SNTP: hardware RTC write failed; continuing with validated SNTP time.\\n");
    }

    if (crtube_bearssl_set_clock_time(selected_time) != 0) {
        crtube_log("SNTP: could not install validated time for BearSSL.\\n");
        return -1;
    }

    crtube_log("SNTP: validated UTC time installed for BearSSL; unix_time=%lu, server_skew=%lu.\\n",
               (unsigned long)selected_time, (unsigned long)difference);
    crtube_log("SNTP: RTC is volatile when the console loses power; it will be synchronized again at next startup.\n");
    return 0;
}
