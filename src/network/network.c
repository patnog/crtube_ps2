#include <stdio.h>
#include <fcntl.h>
#include <debug.h>
#include <string.h>

#include <kernel.h>
#include <iopcontrol.h>
#include <iopheap.h>
#include <loadfile.h>
#include <netman.h>
#include <ps2ip.h>
#include <ps2ips.h>
#include <ps2smb.h>
#include <fileXio_rpc.h>
#include <sbv_patches.h>
#include <sifrpc.h>

#include "network.h"
#include "config.h"
#include "screen.h"
#include "debug_log.h"
#include "tcp_probe.h"
#include "http.h"
#ifdef CRTUBE_ENABLE_YOUTUBE_PROBE
#include "youtube_probe.h"
#endif

extern unsigned char DEV9_irx[];
extern unsigned int size_DEV9_irx;
extern unsigned char NETMAN_irx[];
extern unsigned int size_NETMAN_irx;
extern unsigned char SMAP_irx[];
extern unsigned int size_SMAP_irx;
extern unsigned char SMSUTILS_irx[];
extern unsigned int size_SMSUTILS_irx;
extern unsigned char PS2IP_NM_irx[];
extern unsigned int size_PS2IP_NM_irx;
extern unsigned char PS2IPS_irx[];
extern unsigned int size_PS2IPS_irx;
extern unsigned char PS2HTTP_irx[];
extern unsigned int size_PS2HTTP_irx;
extern unsigned char SMBMAN_irx[];
extern unsigned int size_SMBMAN_irx;

static int g_netman_initialized;
static int g_ps2ip_initialized;
static int g_network_ready;
static void (*g_network_progress_callback)(crtube_text_id_t text_id);

void crtube_network_set_progress_callback(
    void (*callback)(crtube_text_id_t text_id))
{
    g_network_progress_callback = callback;
}

void crtube_network_report_progress(crtube_text_id_t text_id)
{
    if (g_network_progress_callback != NULL)
        g_network_progress_callback(text_id);
}

static int g_smb_ready;
static crtube_config_t g_config;
static char g_dns_server[16];
static int g_dev9_module_id = -999;
static int g_dev9_start_result = -999;
static int g_netman_module_id = -999;
static int g_netman_start_result = -999;
static int g_smap_module_id = -999;
static int g_smap_start_result = -999;
static int g_ps2ip_nm_module_id = -999;
static int g_ps2ip_nm_start_result = -999;
static int g_ps2ips_module_id = -999;
static int g_ps2ips_start_result = -999;
static int g_ps2http_module_id = -999;
static int g_ps2http_start_result = -999;
static int g_ps2http_load_result = -999;
static int g_netman_init_result = -999;
static int g_ps2ip_init_result = -999;
static int g_ps2ip_setconfig_result = -999;
static int g_ps2ip_getconfig_result = -999;
static int g_smap_mac_result = -999;
static int g_smap_mac_match = -1;

typedef struct crtube_netman_snapshot {
    int status_result;
    int main_if_result;
    int global_link_state;
    char main_if[NETMAN_NETIF_NAME_MAX_LEN + 1];
    struct NetManEthStatus status;
} crtube_netman_snapshot_t;

static crtube_netman_snapshot_t g_gateway_stats_before;
static crtube_netman_snapshot_t g_gateway_stats_after;
static crtube_tcp_probe_result_t g_gateway_tcp_probe;
static int g_gateway_tcp_probe_result = -999;
static int g_gateway_probe_ran;
static crtube_tcp_probe_result_t g_loopback_tcp_probe;
static int g_loopback_tcp_probe_result = -999;
static crtube_tcp_probe_result_t g_self_tcp_probe;
static int g_self_tcp_probe_result = -999;
static int g_local_tcp_probes_ran;

typedef struct crtube_ps2http_probe {
    const char *label;
    char url[128];
    int open_result;
    int read_result;
    int close_result;
    unsigned char first_byte;
    int ran;
} crtube_ps2http_probe_t;

static crtube_ps2http_probe_t g_ps2http_wan_probe;

static void capture_netman_snapshot(crtube_netman_snapshot_t *snapshot)
{
    if (snapshot == NULL)
        return;

    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->status_result = -1;
    snapshot->main_if_result = NetManQueryMainIF(snapshot->main_if);
    snapshot->global_link_state = NetManGetGlobalNetIFLinkState();
    snapshot->status_result = NetManIoctl(
        NETMAN_NETIF_IOCTL_ETH_GET_STATUS, NULL, 0,
        &snapshot->status, sizeof(snapshot->status));
}

static const char *tcp_probe_error_name(int error)
{
    switch (error) {
        case 0: return "none";
        case 111: return "ECONNREFUSED";
        case 113: return "EHOSTUNREACH";
        case 110: return "ETIMEDOUT";
        case 101: return "ENETUNREACH";
        default: return "other";
    }
}

static void network_wakeup_callback(s32 alarm_id, u16 time, void *common)
{
    (void)alarm_id;
    (void)time;
    iWakeupThread(*(int *)common);
}

static int wait_for_network_state(int (*check_state)(void))
{
    int thread_id = GetThreadId();
    int retry;

    for (retry = 0; retry <= 10; retry++) {
        if (check_state())
            return 0;
        if (retry == 10)
            break;
        SetAlarm(1000 * 16, network_wakeup_callback, &thread_id);
        SleepThread();
    }
    return -1;
}

static int network_link_is_up(void)
{
    return NetManIoctl(NETMAN_NETIF_IOCTL_GET_LINK_STATUS,
                       NULL, 0, NULL, 0) == NETMAN_NETIF_ETH_LINK_STATE_UP;
}

static int load_iop_module(const unsigned char *module, unsigned int size)
{
    return SifExecModuleBuffer((void *)module, size, 0, NULL, NULL);
}

/* Capture both SifExecModuleBuffer's module ID and the IOP module's _start
   return value. A positive module ID alone does not prove _start succeeded. */
static int load_network_module(const char *name,
                               const unsigned char *module,
                               unsigned int size,
                               int *module_id_out,
                               int *start_result)
{
    int iop_result = -999;
    int module_id;

    module_id = SifExecModuleBuffer((void *)module, size, 0, NULL,
                                    &iop_result);
    if (module_id_out != NULL)
        *module_id_out = module_id;
    if (start_result != NULL)
        *start_result = iop_result;

    scr_printf("IOP module %s: module_id=%d start_result=%d\n",
               name, module_id, iop_result);

    if (module_id < 0)
        return module_id;
    if (iop_result < 0)
        return iop_result;
    return 0;
}



static int ps2http_status_from_open_result(int open_result)
{
    /* The PS2SDK driver returns the negated HTTP status for non-200 replies. */
    if (open_result <= -100 && open_result >= -599)
        return -open_result;
    return 0;
}

static void run_ps2http_probe(crtube_ps2http_probe_t *probe,
                              const char *label, const char *url)
{
    int fd;

    if (probe == NULL || url == NULL)
        return;

    memset(probe, 0, sizeof(*probe));
    probe->label = label;
    probe->open_result = -999;
    probe->read_result = -999;
    probe->close_result = -999;
    strncpy(probe->url, url, sizeof(probe->url) - 1);
    probe->ran = 1;

    scr_printf("PS2SDK IOP HTTP probe %s: %s\n", label, url);
    fd = fileXioOpen(url, O_RDONLY);
    probe->open_result = fd;
    if (fd < 0) {
        scr_printf("PS2SDK IOP HTTP open failed: %d (driver HTTP status=%d)\n",
                   fd, ps2http_status_from_open_result(fd));
        return;
    }

    /* Read one byte only: the official PS2SDK driver has no read timeout and
       its read callback may wait until the requested byte count is satisfied. */
    probe->read_result = fileXioRead(fd, &probe->first_byte, 1);
    probe->close_result = fileXioClose(fd);
    scr_printf("PS2SDK IOP HTTP result: fd=%d read=%d first=%02X close=%d\n",
               fd, probe->read_result, (unsigned int)probe->first_byte,
               probe->close_result);
}

static int load_ps2http_driver(void)
{
    scr_printf("Loading official PS2SDK PS2HTTP IOP driver...\n");
    g_ps2http_load_result = load_network_module(
        "PS2HTTP", PS2HTTP_irx, size_PS2HTTP_irx,
        &g_ps2http_module_id, &g_ps2http_start_result);
    if (g_ps2http_load_result < 0) {
        scr_printf("PS2HTTP driver load failed: %d\n",
                   g_ps2http_load_result);
        return g_ps2http_load_result;
    }
    scr_printf("PS2HTTP driver registered as http: (IOP IOMAN)\n");
    return 0;
}

static void format_ipv4(const struct ip4_addr *address, char *out, size_t out_size)
{
    snprintf(out, out_size, "%u.%u.%u.%u",
             (unsigned int)ip4_addr1(address),
             (unsigned int)ip4_addr2(address),
             (unsigned int)ip4_addr3(address),
             (unsigned int)ip4_addr4(address));
}

int crtube_network_init(void)
{
    t_ip_info info;
    int result;

    if (g_network_ready)
        return CRTUBE_NETWORK_OK;

    scr_printf("Reading IPCONFIG.DAT from mc0/mc1...\n");
    if (crtube_config_load(&g_config) != 0) {
        scr_printf("IPCONFIG: NOT READ / INVALID\n");
        scr_printf("Expected: mc0:/CRTUBE/IPCONFIG.DAT or mc1:/CRTUBE/IPCONFIG.DAT\n");
        return CRTUBE_NETWORK_ERR_CONFIG;
    }

    scr_printf("Memory card config: loaded\n");
    scr_printf("IPCONFIG: %s\n", g_config.ip_config_source);
    scr_printf("PS2 IP: %s\n", g_config.ip);
    scr_printf("DNS server: %s\n", g_config.dns);
    if (g_config.smb_config_source[0] != '\0') {
        scr_printf("SMBCONFIG: %s\n", g_config.smb_config_source);
        scr_printf("SMB server: %s:%u\n", g_config.smb_server,
                   (unsigned int)g_config.smb_port);
        scr_printf("SMB share: %s\n", g_config.smb_share);
    } else {
        scr_printf("SMBCONFIG: not found / invalid (SMB logs disabled)\n");
    }

    scr_printf("Loading DEV9...\n");
    result = load_network_module("DEV9", DEV9_irx, size_DEV9_irx,
                                 &g_dev9_module_id, &g_dev9_start_result);
    if (result < 0) { result = CRTUBE_NETWORK_ERR_MODULE; goto fail; }

    scr_printf("Loading NETMAN...\n");
    result = load_network_module("NETMAN", NETMAN_irx, size_NETMAN_irx,
                                 &g_netman_module_id, &g_netman_start_result);
    if (result < 0) { result = CRTUBE_NETWORK_ERR_MODULE; goto fail; }

    scr_printf("Loading SMSUTILS...\n");
    result = load_iop_module(SMSUTILS_irx, size_SMSUTILS_irx);
    if (result < 0) { result = CRTUBE_NETWORK_ERR_MODULE; goto fail; }

    /* PS2SDK NETMAN.txt: load NETMAN, then the network interface driver,
       then the IOP-side protocol stack (PS2IP-NM). NetManInit() follows
       PS2IP-NM so its protocol-stack registration is already in place. */
    scr_printf("Loading SMAP Ethernet driver (NETMAN mode)...\n");
    result = load_network_module("SMAP", SMAP_irx, size_SMAP_irx,
                                 &g_smap_module_id, &g_smap_start_result);
    if (result < 0) { result = CRTUBE_NETWORK_ERR_MODULE; goto fail; }

    scr_printf("Loading PS2IP-NM (IOP TCP/IP stack)...\n");
    result = load_network_module("PS2IP-NM", PS2IP_NM_irx,
                                 size_PS2IP_NM_irx,
                                 &g_ps2ip_nm_module_id,
                                 &g_ps2ip_nm_start_result);
    if (result < 0) { result = CRTUBE_NETWORK_ERR_MODULE; goto fail; }

    scr_printf("Initializing NETMAN after PS2IP-NM load...\n");
    g_netman_init_result = NetManInit();
    scr_printf("NetManInit result: %d\n", g_netman_init_result);
    if (g_netman_init_result < 0) {
        result = CRTUBE_NETWORK_ERR_MODULE;
        goto fail;
    }
    g_netman_initialized = 1;

    scr_printf("Loading PS2IPS (EE-to-IOP socket RPC)...\n");
    result = load_network_module("PS2IPS", PS2IPS_irx, size_PS2IPS_irx,
                                 &g_ps2ips_module_id,
                                 &g_ps2ips_start_result);
    if (result < 0) { result = CRTUBE_NETWORK_ERR_MODULE; goto fail; }

    scr_printf("Binding EE socket calls to PS2IPS RPC...\n");
    g_ps2ip_init_result = ps2ip_init();
    result = g_ps2ip_init_result;
    if (result < 0) {
        result = CRTUBE_NETWORK_ERR_CONFIG;
        goto fail;
    }
    g_ps2ip_initialized = 1;

    memset(&info, 0, sizeof(info));
    strncpy(info.netif_name, "sm0", sizeof(info.netif_name) - 1);
    {
        unsigned int a, b, c, d;
        sscanf(g_config.ip, "%u.%u.%u.%u", &a, &b, &c, &d);
        IP4_ADDR((struct ip4_addr *)&info.ipaddr, a, b, c, d);
        sscanf(g_config.netmask, "%u.%u.%u.%u", &a, &b, &c, &d);
        IP4_ADDR((struct ip4_addr *)&info.netmask, a, b, c, d);
        sscanf(g_config.gateway, "%u.%u.%u.%u", &a, &b, &c, &d);
        IP4_ADDR((struct ip4_addr *)&info.gw, a, b, c, d);
    }
    info.dhcp_enabled = 0;

    scr_printf("Applying IP configuration...\n");
    result = ps2ip_setconfig(&info);
    g_ps2ip_setconfig_result = result;
    scr_printf("ps2ip_setconfig RPC result: %d\n", result);
    /* The ps2ips EE wrapper returns the RPC transport status (0 on success),
       not the IOP-side ps2ip_setconfig() return value (1). */
    if (result < 0) {
        result = CRTUBE_NETWORK_ERR_CONFIG;
        goto fail;
    }

    /* PS2SDK's PS2IPS DNS resolver is separate from ps2ip_setconfig().
       The official tcpip_basic sample configures it with dns_setserver(). */
    {
        struct ip4_addr dns_address;
        unsigned int a, b, c, d;
        if (sscanf(g_config.dns, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) {
            result = CRTUBE_NETWORK_ERR_CONFIG;
            goto fail;
        }
        IP4_ADDR(&dns_address, a, b, c, d);
        dns_setserver(0, &dns_address);
        strcpy(g_dns_server, g_config.dns);
        crtube_http_set_dns_server(g_config.dns);
    }

    scr_printf("DNS server configured: %s\n", g_dns_server);
    scr_printf("Waiting for Ethernet link...\n");
    if (wait_for_network_state(network_link_is_up) != 0) {
        result = CRTUBE_NETWORK_ERR_LINK;
        goto fail;
    }

    scr_printf("Checking active network configuration...\n");
    result = ps2ip_getconfig("sm0", &info);
    g_ps2ip_getconfig_result = result;
    scr_printf("ps2ip_getconfig(sm0) RPC result: %d\n", result);
    /* The EE wrapper returns 1 when the RPC completed; the returned
       structure is the authoritative interface configuration. */
    if (result < 0 || info.netif_name[0] != 's' ||
        info.netif_name[1] != 'm') {
        scr_printf("PS2IP interface sm0 was not returned by IOP\n");
        result = CRTUBE_NETWORK_ERR_CONFIG;
        goto fail;
    }
    {
        unsigned char netman_mac[6] = {0};
        char actual_ip[16], actual_mask[16], actual_gateway[16];
        format_ipv4((const struct ip4_addr *)&info.ipaddr,
                    actual_ip, sizeof(actual_ip));
        format_ipv4((const struct ip4_addr *)&info.netmask,
                    actual_mask, sizeof(actual_mask));
        format_ipv4((const struct ip4_addr *)&info.gw,
                    actual_gateway, sizeof(actual_gateway));
        scr_printf("Effective sm0 IP: %s\n", actual_ip);
        scr_printf("Effective sm0 mask: %s\n", actual_mask);
        scr_printf("Effective sm0 gateway: %s\n", actual_gateway);
        scr_printf("PS2IP-NM sm0 MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                   info.hw_addr[0], info.hw_addr[1], info.hw_addr[2],
                   info.hw_addr[3], info.hw_addr[4], info.hw_addr[5]);
        g_smap_mac_result = NetManIoctl(NETMAN_NETIF_IOCTL_ETH_GET_MAC,
                                       NULL, 0, netman_mac,
                                       sizeof(netman_mac));
        g_smap_mac_match =
            (g_smap_mac_result >= 0 &&
             memcmp(info.hw_addr, netman_mac, sizeof(netman_mac)) == 0);
        scr_printf("NETMAN SMAP MAC query=%d MAC=%02X:%02X:%02X:%02X:%02X:%02X\n",
                   g_smap_mac_result, netman_mac[0], netman_mac[1],
                   netman_mac[2], netman_mac[3], netman_mac[4], netman_mac[5]);
        scr_printf("PS2IP-NM sm0 / NETMAN SMAP MAC match: %s\n",
                   g_smap_mac_match ? "YES" : "NO");
        scr_printf("Configured IP match: %s\n",
                   strcmp(actual_ip, g_config.ip) == 0 ? "YES" : "NO");
        scr_printf("Configured mask match: %s\n",
                   strcmp(actual_mask, g_config.netmask) == 0 ? "YES" : "NO");
        scr_printf("Configured gateway match: %s\n",
                   strcmp(actual_gateway, g_config.gateway) == 0 ? "YES" : "NO");
    }


    g_network_ready = 1;
    return CRTUBE_NETWORK_OK;

fail:
    crtube_network_deinit();
    return result;
}


void crtube_network_run_diagnostics(void)
{
    if (!g_network_ready) {
        crtube_log("Network diagnostics skipped: network is not initialized.\n");
        return;
    }

    /* These probes can block in IOP drivers or PS2IPS. They are invoked only
       after an explicit user request from Network Status. The caller attempts
       to open the SMB log first; results are not persisted if SMB is unavailable. */
    crtube_log("Network diagnostics: beginning post-SMB probes.\n");

    /* The official PS2SDK ps2http.irx registers an IOP IOMAN filesystem
       driver named http:. fileXioOpen/read therefore execute the HTTP socket
       operations inside the IOP PS2IP stack, bypassing EE PS2IPS socket RPC.
       Use a numeric address to keep DNS out of this comparison. */
    crtube_network_report_progress(CRTUBE_TEXT_NETWORK_PROGRESS_LOADING_HTTP);
    crtube_log("PS2HTTP diagnostic: driver load starting.\n");
    if (load_ps2http_driver() == 0) {
        crtube_network_report_progress(CRTUBE_TEXT_NETWORK_PROGRESS_HTTP_WAN);
        crtube_log("PS2HTTP diagnostic: opening http://1.1.1.1/\n");
        run_ps2http_probe(&g_ps2http_wan_probe, "WAN-1.1.1.1-port-80",
                          "http://1.1.1.1/");
        crtube_log("PS2HTTP diagnostic: open result=%d read=%d close=%d\n",
                   g_ps2http_wan_probe.open_result,
                   g_ps2http_wan_probe.read_result,
                   g_ps2http_wan_probe.close_result);
    } else {
        crtube_log("PS2HTTP diagnostic: driver load failed=%d\n",
                   g_ps2http_load_result);
    }

    /* These local-only probes isolate PS2IPS/RPC from external routing.
       With no listener on port 65000, ECONNREFUSED is the expected result
       when the local TCP/IP path is functioning. */
    crtube_network_report_progress(CRTUBE_TEXT_NETWORK_PROGRESS_LOOPBACK);
    crtube_log("PS2IPS loopback diagnostic starting: 127.0.0.1:65000\n");
    memset(&g_loopback_tcp_probe, 0, sizeof(g_loopback_tcp_probe));
    g_loopback_tcp_probe_result = crtube_tcp_probe_ipv4(
        "127.0.0.1", 65000, &g_loopback_tcp_probe);
    crtube_network_report_progress(CRTUBE_TEXT_NETWORK_PROGRESS_SELF_IP);
    crtube_log("PS2IPS self-IP diagnostic starting: %s:65000\n", g_config.ip);
    memset(&g_self_tcp_probe, 0, sizeof(g_self_tcp_probe));
    g_self_tcp_probe_result = crtube_tcp_probe_ipv4(
        g_config.ip, 65000, &g_self_tcp_probe);
    g_local_tcp_probes_ran = 1;

    /* A TCP SYN to the configured gateway forces lwIP to resolve the next-hop
       MAC through ARP. No HTTP, DNS, SMBMAN or application payload is used. */
    if (g_config.gateway[0] != '\0') {
        crtube_network_report_progress(CRTUBE_TEXT_NETWORK_PROGRESS_GATEWAY);
        crtube_log("PS2IPS gateway diagnostic starting: %s:80\n", g_config.gateway);
        memset(&g_gateway_tcp_probe, 0, sizeof(g_gateway_tcp_probe));
        capture_netman_snapshot(&g_gateway_stats_before);
        g_gateway_tcp_probe_result = crtube_tcp_probe_ipv4(
            g_config.gateway, 80, &g_gateway_tcp_probe);
        capture_netman_snapshot(&g_gateway_stats_after);
        g_gateway_probe_ran = 1;
        crtube_log("PS2IPS gateway diagnostic finished: result=%d connect=%d so_error=%d\n",
                   g_gateway_tcp_probe_result,
                   g_gateway_tcp_probe.connect_result,
                   g_gateway_tcp_probe.so_error);
    } else {
        g_gateway_probe_ran = 0;
        crtube_log("PS2IPS gateway diagnostic skipped: no gateway configured.\n");
    }


#ifdef CRTUBE_ENABLE_YOUTUBE_PROBE
    /* InnerTube is HTTPS POST and returns search metadata as JSON. */
    crtube_youtube_run_probe();
#else
    crtube_log("YouTube InnerTube probe not built. Rebuild with YOUTUBE_TEST=1.\n");
#endif

    crtube_network_report_progress(CRTUBE_TEXT_NETWORK_PROGRESS_FINISHING);
    crtube_log("Network diagnostics: post-SMB probes completed.\n");
}

void crtube_network_log_diagnostics(void)
{
    t_ip_info info;
    unsigned char netman_mac[6] = {0};
    int config_result = -1;
    int mac_result = -1;
    crtube_netman_snapshot_t current_snapshot;

    memset(&info, 0, sizeof(info));
    memset(&current_snapshot, 0, sizeof(current_snapshot));
    if (g_network_ready) {
        config_result = ps2ip_getconfig("sm0", &info);
        mac_result = NetManIoctl(NETMAN_NETIF_IOCTL_ETH_GET_MAC,
                                 NULL, 0, netman_mac, sizeof(netman_mac));
        capture_netman_snapshot(&current_snapshot);
    }

    crtube_log("Network module sequence: DEV9 -> NETMAN -> SMSUTILS -> SMAP -> PS2IP-NM -> NetManInit -> PS2IPS -> ps2ip_init\n");
    crtube_log("PS2IPS DNS server: %s\n", g_dns_server[0] ? g_dns_server : "not configured");
    crtube_log("IOP module DEV9: module_id=%d start_result=%d\n",
               g_dev9_module_id, g_dev9_start_result);
    crtube_log("IOP module NETMAN: module_id=%d start_result=%d\n",
               g_netman_module_id, g_netman_start_result);
    crtube_log("IOP module SMAP: module_id=%d start_result=%d\n",
               g_smap_module_id, g_smap_start_result);
    crtube_log("NetManInit: result=%d\n", g_netman_init_result);
    crtube_log("IOP module PS2IP-NM: module_id=%d start_result=%d\n",
               g_ps2ip_nm_module_id, g_ps2ip_nm_start_result);
    crtube_log("IOP module PS2IPS: module_id=%d start_result=%d\n",
               g_ps2ips_module_id, g_ps2ips_start_result);
    crtube_log("IOP module PS2HTTP (official PS2SDK driver): module_id=%d start_result=%d load_result=%d\n",
               g_ps2http_module_id, g_ps2http_start_result,
               g_ps2http_load_result);
    crtube_log("PS2SDK IOP HTTP path: fileXioOpen/read -> IOMAN http: driver -> IOP PS2IP; bypasses EE PS2IPS socket RPC.\n");
    if (g_ps2http_wan_probe.ran) {
        crtube_log("PS2SDK IOP HTTP probe %s url=%s open_fd_or_error=%d driver_http_status=%d read=%d first_byte=0x%02X close=%d\n",
                   g_ps2http_wan_probe.label, g_ps2http_wan_probe.url,
                   g_ps2http_wan_probe.open_result,
                   ps2http_status_from_open_result(g_ps2http_wan_probe.open_result),
                   g_ps2http_wan_probe.read_result,
                   (unsigned int)g_ps2http_wan_probe.first_byte,
                   g_ps2http_wan_probe.close_result);
    } else {
        crtube_log("PS2SDK IOP HTTP WAN probe not run.\n");
    }
    if (g_local_tcp_probes_ran) {
        crtube_log("PS2IPS loopback probe target=127.0.0.1:65000 result=%d socket=%d socket_errno=%d connect=%d connect_errno=%d so_rc=%d so_error=%d so_errno=%d close=%d close_errno=%d\n",
                   g_loopback_tcp_probe_result,
                   g_loopback_tcp_probe.socket_result,
                   g_loopback_tcp_probe.socket_errno,
                   g_loopback_tcp_probe.connect_result,
                   g_loopback_tcp_probe.connect_errno,
                   g_loopback_tcp_probe.so_error_result,
                   g_loopback_tcp_probe.so_error,
                   g_loopback_tcp_probe.so_error_errno,
                   g_loopback_tcp_probe.close_result,
                   g_loopback_tcp_probe.close_errno);
        crtube_log("PS2IPS self-IP probe target=%s:65000 result=%d socket=%d socket_errno=%d connect=%d connect_errno=%d so_rc=%d so_error=%d so_errno=%d close=%d close_errno=%d\n",
                   g_config.ip, g_self_tcp_probe_result,
                   g_self_tcp_probe.socket_result,
                   g_self_tcp_probe.socket_errno,
                   g_self_tcp_probe.connect_result,
                   g_self_tcp_probe.connect_errno,
                   g_self_tcp_probe.so_error_result,
                   g_self_tcp_probe.so_error,
                   g_self_tcp_probe.so_error_errno,
                   g_self_tcp_probe.close_result,
                   g_self_tcp_probe.close_errno);
    } else {
        crtube_log("PS2IPS local loopback/self-IP probes not run.\n");
    }
    crtube_log("ps2ip_init RPC result=%d; setconfig RPC result=%d; getconfig(sm0) result=%d\n",
               g_ps2ip_init_result, g_ps2ip_setconfig_result,
               g_ps2ip_getconfig_result);
    crtube_log("PS2IP-NM sm0 query=%d name=%s IP=%u.%u.%u.%u mask=%u.%u.%u.%u gateway=%u.%u.%u.%u MAC=%02X:%02X:%02X:%02X:%02X:%02X\n",
               config_result, info.netif_name,
               (unsigned int)ip4_addr1((struct ip4_addr *)&info.ipaddr),
               (unsigned int)ip4_addr2((struct ip4_addr *)&info.ipaddr),
               (unsigned int)ip4_addr3((struct ip4_addr *)&info.ipaddr),
               (unsigned int)ip4_addr4((struct ip4_addr *)&info.ipaddr),
               (unsigned int)ip4_addr1((struct ip4_addr *)&info.netmask),
               (unsigned int)ip4_addr2((struct ip4_addr *)&info.netmask),
               (unsigned int)ip4_addr3((struct ip4_addr *)&info.netmask),
               (unsigned int)ip4_addr4((struct ip4_addr *)&info.netmask),
               (unsigned int)ip4_addr1((struct ip4_addr *)&info.gw),
               (unsigned int)ip4_addr2((struct ip4_addr *)&info.gw),
               (unsigned int)ip4_addr3((struct ip4_addr *)&info.gw),
               (unsigned int)ip4_addr4((struct ip4_addr *)&info.gw),
               info.hw_addr[0], info.hw_addr[1], info.hw_addr[2],
               info.hw_addr[3], info.hw_addr[4], info.hw_addr[5]);
    crtube_log("NETMAN SMAP MAC query=%d MAC=%02X:%02X:%02X:%02X:%02X:%02X match=%d\n",
               mac_result, netman_mac[0], netman_mac[1], netman_mac[2],
               netman_mac[3], netman_mac[4], netman_mac[5],
               mac_result >= 0 &&
               memcmp(info.hw_addr, netman_mac, sizeof(netman_mac)) == 0);

    crtube_log("NETMAN main interface query=%d name=%s global_link_state=%d\n",
               current_snapshot.main_if_result,
               current_snapshot.main_if_result == 0 ? current_snapshot.main_if : "<none>",
               current_snapshot.global_link_state);
    crtube_log("SMAP GET_STATUS result=%d link_status=%u link_mode=%u\n",
               current_snapshot.status_result,
               (unsigned int)current_snapshot.status.LinkStatus,
               (unsigned int)current_snapshot.status.LinkMode);
    crtube_log("SMAP counters are hardware error/drop counters; PS2SDK does not expose total RX/TX frame counts through this ioctl.\n");
    crtube_log("SMAP counters current: rx_drop=%lu rx_error=%lu rx_overrun=%u rx_bad_len=%u rx_bad_fcs=%u rx_bad_align=%u rx_alloc_fail=%u tx_drop=%lu tx_error=%lu tx_lost_carrier=%u tx_defer=%u tx_collision=%u tx_underrun=%u\n",
               (unsigned long)current_snapshot.status.stats.RxDroppedFrameCount,
               (unsigned long)current_snapshot.status.stats.RxErrorCount,
               (unsigned int)current_snapshot.status.stats.RxFrameOverrunCount,
               (unsigned int)current_snapshot.status.stats.RxFrameBadLengthCount,
               (unsigned int)current_snapshot.status.stats.RxFrameBadFCSCount,
               (unsigned int)current_snapshot.status.stats.RxFrameBadAlignmentCount,
               (unsigned int)current_snapshot.status.stats.RxAllocFail,
               (unsigned long)current_snapshot.status.stats.TxDroppedFrameCount,
               (unsigned long)current_snapshot.status.stats.TxErrorCount,
               (unsigned int)current_snapshot.status.stats.TxFrameLOSSCRCount,
               (unsigned int)current_snapshot.status.stats.TxFrameEDEFERCount,
               (unsigned int)current_snapshot.status.stats.TxFrameCollisionCount,
               (unsigned int)current_snapshot.status.stats.TxFrameUnderrunCount);

    if (g_gateway_probe_ran) {
        crtube_log("Gateway TCP probe target=%s:80 result=%d socket=%d socket_errno=%d connect=%d connect_errno=%d so_rc=%d so_error=%d so_errno=%d close=%d close_errno=%d error_name=%s payload=none\n",
                   g_config.gateway, g_gateway_tcp_probe_result,
                   g_gateway_tcp_probe.socket_result,
                   g_gateway_tcp_probe.socket_errno,
                   g_gateway_tcp_probe.connect_result,
                   g_gateway_tcp_probe.connect_errno,
                   g_gateway_tcp_probe.so_error_result,
                   g_gateway_tcp_probe.so_error,
                   g_gateway_tcp_probe.so_error_errno,
                   g_gateway_tcp_probe.close_result,
                   g_gateway_tcp_probe.close_errno,
                   tcp_probe_error_name(g_gateway_tcp_probe.so_error));
        crtube_log("NETMAN main interface before probe: query=%d name=%s global_link=%d status=%d link=%u mode=%u\n",
                   g_gateway_stats_before.main_if_result,
                   g_gateway_stats_before.main_if_result == 0 ? g_gateway_stats_before.main_if : "<none>",
                   g_gateway_stats_before.global_link_state,
                   g_gateway_stats_before.status_result,
                   (unsigned int)g_gateway_stats_before.status.LinkStatus,
                   (unsigned int)g_gateway_stats_before.status.LinkMode);
        crtube_log("NETMAN main interface after probe: query=%d name=%s global_link=%d status=%d link=%u mode=%u\n",
                   g_gateway_stats_after.main_if_result,
                   g_gateway_stats_after.main_if_result == 0 ? g_gateway_stats_after.main_if : "<none>",
                   g_gateway_stats_after.global_link_state,
                   g_gateway_stats_after.status_result,
                   (unsigned int)g_gateway_stats_after.status.LinkStatus,
                   (unsigned int)g_gateway_stats_after.status.LinkMode);
        if (g_gateway_stats_before.status_result == 0 &&
            g_gateway_stats_after.status_result == 0) {
            crtube_log("SMAP counter deltas during gateway probe: rx_drop=%lu rx_error=%lu tx_drop=%lu tx_error=%lu rx_overrun=%u rx_bad_len=%u rx_bad_fcs=%u rx_bad_align=%u rx_alloc_fail=%u tx_lost_carrier=%u tx_defer=%u tx_collision=%u tx_underrun=%u\n",
                       (unsigned long)(g_gateway_stats_after.status.stats.RxDroppedFrameCount - g_gateway_stats_before.status.stats.RxDroppedFrameCount),
                       (unsigned long)(g_gateway_stats_after.status.stats.RxErrorCount - g_gateway_stats_before.status.stats.RxErrorCount),
                       (unsigned long)(g_gateway_stats_after.status.stats.TxDroppedFrameCount - g_gateway_stats_before.status.stats.TxDroppedFrameCount),
                       (unsigned long)(g_gateway_stats_after.status.stats.TxErrorCount - g_gateway_stats_before.status.stats.TxErrorCount),
                       (unsigned int)(g_gateway_stats_after.status.stats.RxFrameOverrunCount - g_gateway_stats_before.status.stats.RxFrameOverrunCount),
                       (unsigned int)(g_gateway_stats_after.status.stats.RxFrameBadLengthCount - g_gateway_stats_before.status.stats.RxFrameBadLengthCount),
                       (unsigned int)(g_gateway_stats_after.status.stats.RxFrameBadFCSCount - g_gateway_stats_before.status.stats.RxFrameBadFCSCount),
                       (unsigned int)(g_gateway_stats_after.status.stats.RxFrameBadAlignmentCount - g_gateway_stats_before.status.stats.RxFrameBadAlignmentCount),
                       (unsigned int)(g_gateway_stats_after.status.stats.RxAllocFail - g_gateway_stats_before.status.stats.RxAllocFail),
                       (unsigned int)(g_gateway_stats_after.status.stats.TxFrameLOSSCRCount - g_gateway_stats_before.status.stats.TxFrameLOSSCRCount),
                       (unsigned int)(g_gateway_stats_after.status.stats.TxFrameEDEFERCount - g_gateway_stats_before.status.stats.TxFrameEDEFERCount),
                       (unsigned int)(g_gateway_stats_after.status.stats.TxFrameCollisionCount - g_gateway_stats_before.status.stats.TxFrameCollisionCount),
                       (unsigned int)(g_gateway_stats_after.status.stats.TxFrameUnderrunCount - g_gateway_stats_before.status.stats.TxFrameUnderrunCount));
        } else {
            crtube_log("SMAP counter deltas unavailable: GET_STATUS failed before or after probe\n");
        }
        crtube_log("ARP note: PS2SDK public EE API does not expose the lwIP ARP cache; gateway TCP connect forces next-hop resolution. ECONNREFUSED indicates host answered TCP; EHOSTUNREACH may indicate unresolved neighbor or routing failure.\n");
    } else {
        crtube_log("Gateway TCP/ARP probe not run.\n");
    }
}

int crtube_smb_init(void)
{
    smbLogOn_in_t logon;
    smbOpenShare_in_t share;
    int result;

    if (g_smb_ready)
        return 0;
    if (!g_network_ready || g_config.smb_server[0] == '\0' ||
        g_config.smb_share[0] == '\0')
        return -1;

    result = load_iop_module(SMBMAN_irx, size_SMBMAN_irx);
    if (result < 0)
        return -2;

    memset(&logon, 0, sizeof(logon));
    strncpy(logon.serverIP, g_config.smb_server, sizeof(logon.serverIP) - 1);
    logon.serverPort = g_config.smb_port;
    /*
     * The configured format has no USER/PASS fields. Match POPSLoader's
     * guest configuration: an empty username and NO_PASSWORD. Do not send
     * the literal username "guest"; some SMB1 servers reject that account
     * while accepting anonymous guest access.
     */
    logon.PasswordType = NO_PASSWORD;

    scr_printf("SMB LOGON: server=%s port=%u user=<anonymous>\\n",
               g_config.smb_server, (unsigned int)g_config.smb_port);
    result = fileXioDevctl("smb0:", SMB_DEVCTL_LOGON,
                           &logon, sizeof(logon), NULL, 0);
    scr_printf("SMB LOGON raw result: %d (0x%08X)\\n",
               result, (unsigned int)result);
    if (result < 0)
        return -3;
    if (result > 0) {
        scr_printf("SMB LOGON returned positive status; checking share open\\n");
    }

    memset(&share, 0, sizeof(share));
    strncpy(share.ShareName, g_config.smb_share,
            sizeof(share.ShareName) - 1);
    share.PasswordType = NO_PASSWORD;

    scr_printf("SMB OPENSHARE: %s\\n", g_config.smb_share);
    result = fileXioDevctl("smb0:", SMB_DEVCTL_OPENSHARE,
                           &share, sizeof(share), NULL, 0);
    scr_printf("SMB OPENSHARE raw result: %d (0x%08X)\\n",
               result, (unsigned int)result);
    if (result < 0) {
        fileXioDevctl("smb0:", SMB_DEVCTL_LOGOFF, NULL, 0, NULL, 0);
        return -4;
    }
    if (result > 0) {
        scr_printf("SMB OPENSHARE returned positive status\\n");
    }

    g_smb_ready = 1;
    return 0;
}

int crtube_smb_is_ready(void)
{
    return g_smb_ready;
}


int crtube_network_get_details(char *ip, size_t ip_size,
                               char *mask, size_t mask_size,
                               char *gateway, size_t gateway_size,
                               char *mac, size_t mac_size,
                               int *link_status)
{
    t_ip_info info;
    int result;

    if (ip == NULL || mask == NULL || gateway == NULL || mac == NULL ||
        ip_size < 16 || mask_size < 16 || gateway_size < 16 || mac_size < 18)
        return -1;
    ip[0] = mask[0] = gateway[0] = mac[0] = '\0';

    result = ps2ip_getconfig("sm0", &info);
    if (result < 0 || info.netif_name[0] != 's' ||
        info.netif_name[1] != 'm')
        return -2;

    format_ipv4((const struct ip4_addr *)&info.ipaddr, ip, ip_size);
    format_ipv4((const struct ip4_addr *)&info.netmask, mask, mask_size);
    format_ipv4((const struct ip4_addr *)&info.gw, gateway, gateway_size);
    snprintf(mac, mac_size, "%02X:%02X:%02X:%02X:%02X:%02X",
             info.hw_addr[0], info.hw_addr[1], info.hw_addr[2],
             info.hw_addr[3], info.hw_addr[4], info.hw_addr[5]);

    if (link_status != NULL) {
        *link_status = NetManIoctl(NETMAN_NETIF_IOCTL_GET_LINK_STATUS,
                                   NULL, 0, NULL, 0);
    }
    return 0;
}

void crtube_network_deinit(void)
{
    g_network_ready = 0;
    g_smb_ready = 0;

    if (g_ps2ip_initialized) {
        ps2ip_deinit();
        g_ps2ip_initialized = 0;
    }
    if (g_netman_initialized) {
        NetManDeinit();
        g_netman_initialized = 0;
    }
}

int crtube_network_is_ready(void)
{
    return g_network_ready;
}

int crtube_network_get_smb_server_ip(char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0)
        return -1;
    buffer[0] = '\0';
    if (g_config.smb_server[0] == '\0')
        return -2;
    if (strlen(g_config.smb_server) >= buffer_size)
        return -3;
    strcpy(buffer, g_config.smb_server);
    return 0;
}

int crtube_network_get_smb_endpoint(char *ip, size_t ip_size,
                                    unsigned short *port)
{
    size_t length;

    if (ip == NULL || ip_size == 0 || port == NULL)
        return -1;
    ip[0] = '\0';
    *port = 0;
    if (g_config.smb_server[0] == '\0' || g_config.smb_port == 0)
        return -2;
    length = strlen(g_config.smb_server);
    if (length >= ip_size)
        return -3;
    memcpy(ip, g_config.smb_server, length + 1);
    *port = g_config.smb_port;
    return 0;
}

int crtube_network_get_ip(char *buffer, size_t buffer_size)
{
    t_ip_info info;

    if (buffer == NULL || buffer_size == 0)
        return -1;
    buffer[0] = '\0';

    if (!g_network_ready || ps2ip_getconfig("sm0", &info) < 0)
        return -1;

    snprintf(buffer, buffer_size, "%u.%u.%u.%u",
             (unsigned int)ip4_addr1((struct ip4_addr *)&info.ipaddr),
             (unsigned int)ip4_addr2((struct ip4_addr *)&info.ipaddr),
             (unsigned int)ip4_addr3((struct ip4_addr *)&info.ipaddr),
             (unsigned int)ip4_addr4((struct ip4_addr *)&info.ipaddr));
    return 0;
}
