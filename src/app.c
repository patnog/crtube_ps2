#include <debug.h>
#include <kernel.h>
#include <iopcontrol.h>
#include <libpad.h>
#include <string.h>
#include <fileXio_rpc.h>
#include <iopheap.h>
#include <sbv_patches.h>
#include <loadfile.h>
#include <sifrpc.h>

#include "app.h"
#include "network.h"
#include "tcp_probe.h"
#include "http.h"
#include "debug_log.h"
#include "screen.h"

extern unsigned char IOMANX_irx[];
extern unsigned int size_IOMANX_irx;
extern unsigned char FILEXIO_irx[];
extern unsigned int size_FILEXIO_irx;

static unsigned char pad_buffer[256] __attribute__((aligned(64)));
static unsigned char emergency_stack[4096] __attribute__((aligned(16)));
static ee_thread_t emergency_thread;
static int emergency_thread_id = -1;
extern void *_gp;

static int crtube_filexio_load_modules(void)
{
    int result;

    scr_printf("Stage: loading IOMANX.IRX...\n");
    result = SifExecModuleBuffer(IOMANX_irx, size_IOMANX_irx, 0, NULL, NULL);
    if (result < 0) {
        scr_printf("IOMANX load failed: %d\n", result);
        return -1;
    }

    scr_printf("Stage: loading FILEXIO.IRX...\n");
    result = SifExecModuleBuffer(FILEXIO_irx, size_FILEXIO_irx, 0, NULL, NULL);
    if (result < 0) {
        scr_printf("FILEXIO load failed: %d\n", result);
        return -2;
    }
    return 0;
}

static int crtube_filexio_init(void)
{
    int result;

    scr_printf("Stage: fileXioInit...\n");
    result = fileXioInit();
    scr_printf("fileXioInit returned: %d\n", result);
    return result;
}

static int crtube_controller_init(void)
{
    scr_printf("Loading SIO2MAN...\n");
    if (SifLoadModule("rom0:SIO2MAN", 0, NULL) < 0)
        return -1;
    scr_printf("Loading MCMAN...\n");
    if (SifLoadModule("rom0:MCMAN", 0, NULL) < 0)
        return -1;
    scr_printf("Loading MCSERV...\n");
    if (SifLoadModule("rom0:MCSERV", 0, NULL) < 0)
        return -1;

    scr_printf("Loading PADMAN...\n");
    if (SifLoadModule("rom0:PADMAN", 0, NULL) < 0)
        return -1;

    scr_printf("Initializing PAD...\n");
    padInit(0);
    scr_printf("Opening controller port...\n");
    if (padPortOpen(0, 0, pad_buffer) == 0)
        return -1;

    scr_printf("Controller ready.\n");
    return 0;
}

static void crtube_poll_wakeup(s32 alarm_id, u16 time, void *common)
{
    (void)alarm_id;
    (void)time;
    iWakeupThread(*(int *)common);
}

static void crtube_controller_poll(void)
{
    struct padButtonStatus buttons;
    int state;

    state = padGetState(0, 0);
    if (state != PAD_STATE_STABLE && state != PAD_STATE_FINDCTP1)
        return;

    if (padRead(0, 0, &buttons) == 0)
        return;

    /* Pad button bits are active-low. SELECT + START returns to OSDSYS. */
    if ((buttons.btns & PAD_SELECT) == 0 &&
        (buttons.btns & PAD_START) == 0) {
        scr_printf("\nReturning to PS2 system menu...\n");
        LoadExecPS2("rom0:OSDSYS", 0, NULL);
    }
}

static void crtube_emergency_monitor(void *arg)
{
    int thread_id = GetThreadId();

    (void)arg;
    for (;;) {
        crtube_controller_poll();
        SetAlarm(16000, crtube_poll_wakeup, &thread_id);
        SleepThread();
    }
}

static int crtube_start_emergency_monitor(void)
{
    memset(&emergency_thread, 0, sizeof(emergency_thread));
    emergency_thread.func = crtube_emergency_monitor;
    emergency_thread.stack = emergency_stack;
    emergency_thread.stack_size = sizeof(emergency_stack);
    emergency_thread.gp_reg = &_gp;
    emergency_thread.initial_priority = 0x40;
    emergency_thread.attr = 0;
    emergency_thread.option = 0;

    emergency_thread_id = CreateThread(&emergency_thread);
    if (emergency_thread_id < 0)
        return -1;

    if (StartThread(emergency_thread_id, NULL) < 0) {
        DeleteThread(emergency_thread_id);
        emergency_thread_id = -1;
        return -2;
    }
    return 0;
}

static int crtube_tcp_probe_run(const char *label, const char *ip,
                                unsigned short port,
                                crtube_tcp_probe_result_t *probe)
{
    int result = crtube_tcp_probe_ipv4(ip, port, probe);

    scr_printf("TCP %s %s:%u result=%d connect=%d errno=%d so_error=%d\n",
               label, ip, (unsigned int)port, result,
               probe->connect_result, probe->connect_errno,
               probe->so_error);
    return result;
}

static void crtube_tcp_probe_write_log(const char *label, const char *ip,
                                       unsigned short port, int result,
                                       const crtube_tcp_probe_result_t *probe)
{
    crtube_log("TCP probe %s target=%s:%u result=%d socket=%d socket_errno=%d connect=%d connect_errno=%d so_rc=%d so_error=%d so_errno=%d close=%d close_errno=%d payload=none\n",
               label, ip, (unsigned int)port, result,
               probe->socket_result, probe->socket_errno,
               probe->connect_result, probe->connect_errno,
               probe->so_error_result, probe->so_error,
               probe->so_error_errno, probe->close_result,
               probe->close_errno);
}


typedef struct crtube_http_probe_result {
    int result;
    int status;
    int stage;
    int error;
    int socket;
    int connect_result;
    int so_error;
    int bytes_sent;
    int bytes_received;
    int redirects;
    int truncated;
    int close_result;
    int close_errno;
    char status_line[96];
} crtube_http_probe_result_t;

static void crtube_http_probe_run(const char *label, const char *url,
                                  char *response, size_t response_size,
                                  crtube_http_probe_result_t *probe)
{
    const char *line_end;
    size_t line_length;

    memset(probe, 0, sizeof(*probe));
    probe->result = crtube_http_get_url(url, response, response_size);
    probe->status = crtube_http_last_status();
    probe->stage = crtube_http_last_stage();
    probe->error = crtube_http_last_errno();
    probe->socket = crtube_http_last_socket();
    probe->connect_result = crtube_http_last_connect_result();
    probe->so_error = crtube_http_last_so_error();
    probe->bytes_sent = crtube_http_last_bytes_sent();
    probe->bytes_received = crtube_http_last_bytes_received();
    probe->redirects = crtube_http_last_redirects();
    probe->truncated = crtube_http_last_truncated();
    probe->close_result = crtube_http_last_close_result();
    probe->close_errno = crtube_http_last_close_errno();

    line_end = strstr(response, "\r\n");
    line_length = line_end != NULL ? (size_t)(line_end - response) : strlen(response);
    if (line_length >= sizeof(probe->status_line))
        line_length = sizeof(probe->status_line) - 1;
    memcpy(probe->status_line, response, line_length);
    probe->status_line[line_length] = '\0';

    scr_printf("HTTP %s result=%d status=%d stage=%d errno=%d sent=%d received=%d redirects=%d\n",
               label, probe->result, probe->status, probe->stage,
               probe->error, probe->bytes_sent, probe->bytes_received,
               probe->redirects);
}

static void crtube_http_probe_write_log(const char *label, const char *url,
                                        const crtube_http_probe_result_t *probe)
{
    crtube_log("HTTP probe %s url=%s result=%d status=%d stage=%d errno=%d socket=%d connect=%d so_error=%d sent=%d received=%d redirects=%d truncated=%d close=%d close_errno=%d status_line=\"%s\"\n",
               label, url, probe->result, probe->status, probe->stage,
               probe->error, probe->socket, probe->connect_result,
               probe->so_error, probe->bytes_sent, probe->bytes_received,
               probe->redirects, probe->truncated, probe->close_result,
               probe->close_errno, probe->status_line);
}

void crtube_app_run(void)
{
    char ip_address[16];
    char effective_mask[16];
    char effective_gateway[16];
    char ethernet_mac[18];
    char smb_server_ip[16];
    int ethernet_link_status = -1;
    int net_result;
    unsigned short smb_server_port = 0;
    int smb_endpoint_result = -1;
    crtube_tcp_probe_result_t tcp_lan_server;
    crtube_tcp_probe_result_t tcp_lan_80;
    crtube_tcp_probe_result_t tcp_lan_8080;
    crtube_tcp_probe_result_t tcp_wan;
    int tcp_lan_server_result = -999;
    int tcp_lan_80_result = -999;
    int tcp_lan_8080_result = -999;
    int tcp_wan_result = -999;
    char http_response[1024];
    crtube_http_probe_result_t http_ip_probe;
    crtube_http_probe_result_t http_dns_probe;
    int http_log_ready = 0;

    init_scr();
    crtube_screen_clear();

    scr_printf("CRTube for PlayStation 2\n");
    scr_printf("========================\n\n");
    scr_printf("PS2 application bootstrap\n\n");
    scr_printf("Initializing Ethernet / static IP...\n");

    scr_printf("Stage: initializing SIF RPC...\n");
    sceSifInitRpc(0);

    /*
     * Start from a known IOP state. FILEXIO's EE client waits for the
     * FILEXIO RPC server; if OPL or a previous program left a different
     * IOP module set active, fileXioInit() can wait forever for that server.
     */
    scr_printf("Stage: resetting IOP...\n");
    while (!SifIopReset(NULL, 0)) {
        /* Retry until the IOP accepts the reset request. */
    }

    scr_printf("Stage: waiting for IOP sync...\n");
    while (!SifIopSync()) {
        /* Wait until the reset IOP is ready for RPC. */
    }

    scr_printf("Stage: reinitializing SIF RPC...\n");
    sceSifInitRpc(0);
    scr_printf("Stage: initializing IOP heap...\n");
    SifInitIopHeap();
    scr_printf("Stage: initializing loadfile RPC...\n");
    SifLoadFileInit();
    scr_printf("Stage: enabling module load patch...\n");
    sbv_patch_enable_lmb();

    scr_printf("Stage: loading FileXio IOP modules...\n");
    if (crtube_filexio_load_modules() != 0) {
        scr_printf("FileXio IOP modules failed to load\n");
        net_result = CRTUBE_NETWORK_ERR_CONFIG;
    } else if (crtube_controller_init() != 0) {
        scr_printf("Controller / memory-card initialization failed\n");
        net_result = CRTUBE_NETWORK_ERR_CONFIG;
    } else {
        scr_printf("Stage: starting SELECT+START emergency monitor...\n");
        if (crtube_start_emergency_monitor() != 0) {
            scr_printf("WARNING: emergency monitor could not start\n");
        } else {
            scr_printf("Emergency exit active: SELECT + START\n");
        }

        scr_printf("Stage: initializing FileXio...\n");
        if (crtube_filexio_init() != 0) {
            scr_printf("FileXio initialization failed; filesystem is unavailable\n");
            net_result = CRTUBE_NETWORK_ERR_CONFIG;
        } else {
            scr_printf("FileXio ready.\n");
            scr_printf("Stage: entering crtube_network_init...\n");
            net_result = crtube_network_init();
        }
    }

    if (net_result == CRTUBE_NETWORK_OK) {
        if (crtube_network_get_ip(ip_address, sizeof(ip_address)) == 0) {
            scr_printf("Network: ready\nIP: %s\n", ip_address);
        } else {
            scr_printf("Network: ready (IP unavailable)\n");
        }

        /* Read the configured SMB endpoint for diagnostics only. The SMB
           implementation and its FileXio/SMBMAN session are left untouched. */
        smb_server_ip[0] = '\0';
        smb_endpoint_result = crtube_network_get_smb_endpoint(
            smb_server_ip, sizeof(smb_server_ip), &smb_server_port);
        scr_printf("\nSMB endpoint from SMBCONFIG: %s:%u (config=%d)\n",
                   smb_endpoint_result == 0 ? smb_server_ip : "unavailable",
                   (unsigned int)smb_server_port, smb_endpoint_result);

        /* Mount SMB and open the persistent log before running any network
           probes. Earlier tests established that SMB mounting succeeds and
           that these probes can block, so diagnostics must never precede the
           only persistent logging channel. */
        {
            int smb_result;

            scr_printf("\nStage: initializing SMB before network probes...\n");
            smb_result = crtube_smb_init();
            if (smb_result == 0) {
                scr_printf("SMB mounted. Opening diagnostic log...\n");
                if (crtube_log_init(NULL) == 0) {
                    crtube_log("CRTube startup\n");
                    crtube_log("SMB share mounted\n");
                    crtube_log("SMB logs enabled; all network probes are deferred until after this marker.\n");
                    http_log_ready = 1;
                    scr_printf("SMB logs: enabled\n");
                    crtube_network_log_diagnostics();
                    crtube_network_run_diagnostics();
                    crtube_network_log_diagnostics();
                } else {
                    scr_printf("SMB mounted, but log file could not be opened\n");
                }
            } else {
                scr_printf("SMB unavailable (%d); continuing without file logs\n", smb_result);
            }
        }

        if (http_log_ready) {
            /* Numeric-IP HTTP avoids DNS. Its start marker is flushed to SMB
               before entering the HTTP client, so a blocking call is identifiable. */
            scr_printf("\nHTTP numeric-IP probe after SMB initialization:\n");
            crtube_log("HTTP probe WAN-numeric-IP starting: http://1.1.1.1/\n");
            crtube_http_probe_run("WAN-numeric-IP", "http://1.1.1.1/",
                                  http_response, sizeof(http_response),
                                  &http_ip_probe);
            crtube_http_probe_write_log("WAN-numeric-IP", "http://1.1.1.1/",
                                        &http_ip_probe);

            /* All diagnostic TCP probes now run after SMB is mounted and, when
               available, after the log file has been opened and flushed. */
            scr_printf("\nRaw TCP diagnostics after SMB initialization:\n");
            if (smb_endpoint_result == 0) {
                crtube_log("TCP probe LAN-configured-host-port starting: %s:%u\n",
                           smb_server_ip, (unsigned int)smb_server_port);
                tcp_lan_server_result = crtube_tcp_probe_run(
                    "LAN-configured-host-port", smb_server_ip, smb_server_port,
                    &tcp_lan_server);
                crtube_tcp_probe_write_log("LAN-configured-host-port",
                                           smb_server_ip, smb_server_port,
                                           tcp_lan_server_result, &tcp_lan_server);

                crtube_log("TCP probe LAN-port-80 starting: %s:80\n", smb_server_ip);
                tcp_lan_80_result = crtube_tcp_probe_run(
                    "LAN-port-80", smb_server_ip, 80, &tcp_lan_80);
                crtube_tcp_probe_write_log("LAN-port-80", smb_server_ip, 80,
                                           tcp_lan_80_result, &tcp_lan_80);

                crtube_log("TCP probe LAN-port-8080 starting: %s:8080\n", smb_server_ip);
                tcp_lan_8080_result = crtube_tcp_probe_run(
                    "LAN-port-8080", smb_server_ip, 8080, &tcp_lan_8080);
                crtube_tcp_probe_write_log("LAN-port-8080", smb_server_ip, 8080,
                                           tcp_lan_8080_result, &tcp_lan_8080);
            } else {
                scr_printf("LAN TCP probes skipped: server IP unavailable\n");
                crtube_log("LAN TCP probes skipped: SMB server IP unavailable.\n");
            }

            crtube_log("TCP probe WAN-1.1.1.1-port-80 starting: 1.1.1.1:80\n");
            tcp_wan_result = crtube_tcp_probe_run(
                "WAN-1.1.1.1-port-80", "1.1.1.1", 80, &tcp_wan);
            crtube_tcp_probe_write_log("WAN-1.1.1.1-port-80", "1.1.1.1", 80,
                                       tcp_wan_result, &tcp_wan);

            /* DNS probe runs last. Its resolver has a bounded UDP wait, and the
               per-stage messages are now persisted before each blocking operation. */
            scr_printf("\nHTTP DNS-hostname probe after SMB initialization:\n");
            crtube_log("HTTP probe WAN-DNS-hostname starting: http://example.com/\n");
            crtube_http_probe_run("WAN-DNS-hostname", "http://example.com/",
                                  http_response, sizeof(http_response),
                                  &http_dns_probe);
            crtube_http_probe_write_log("WAN-DNS-hostname", "http://example.com/",
                                        &http_dns_probe);

        } else {
            scr_printf("Network probes skipped: SMB diagnostic log unavailable\n");
        }

        crtube_log("SMB endpoint from SMBCONFIG: %s:%u (config=%d)\n",
                   smb_endpoint_result == 0 ? smb_server_ip : "unavailable",
                   (unsigned int)smb_server_port, smb_endpoint_result);
        crtube_log("Network IP: %s\n", ip_address);
        if (crtube_network_get_details(ip_address, sizeof(ip_address),
                                       effective_mask, sizeof(effective_mask),
                                       effective_gateway, sizeof(effective_gateway),
                                       ethernet_mac, sizeof(ethernet_mac),
                                       &ethernet_link_status) == 0) {
            crtube_log("Effective sm0: ip=%s mask=%s gateway=%s mac=%s link_status=%d\n",
                       ip_address, effective_mask, effective_gateway,
                       ethernet_mac, ethernet_link_status);
        } else {
            crtube_log("Effective sm0 configuration: unavailable\n");
        }

        crtube_log("TCP diagnostics use PS2IPS RPC sockets and PS2IP-NM on the IOP.\n");
        crtube_log("All TCP probes ran after SMBMAN initialization. Each performs socket()+connect()+getsockopt(SO_ERROR)+close(); payload=none.\n");
        crtube_log("HTTP client uses PS2IPS sockets, bounded UDP DNS query, HTTP/1.0 GET and bounded select() receive waits. HTTPS/TLS is not implemented.\n");
        crtube_log("DIAGNOSIS NOTE: a successful LAN-configured-host-port TCP connect proves the PS2IPS EE-to-IOP socket path can reach the LAN without sending SMB data.\n");
    } else {
        scr_printf("Network initialization failed: %d\n", net_result);
        crtube_log("Network initialization failed: %d\n", net_result);
        scr_printf("Check Ethernet cable, adapter and IPCONFIG.DAT on mc0 or mc1.\n");
    }

    scr_printf("\nYouTube: not initialized\n");
    scr_printf("Media: not initialized\n");
    scr_printf("\nSELECT + START: return to PS2 menu\n");

    /* SELECT+START is monitored by an independent thread. */
    for (;;)
        SleepThread();
}
