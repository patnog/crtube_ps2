#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "config.h"

static int valid_ipv4(const char *text)
{
    unsigned int a, b, c, d;
    char tail;

    if (text == NULL ||
        sscanf(text, "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) != 4)
        return 0;

    return a <= 255 && b <= 255 && c <= 255 && d <= 255;
}

static int read_ip_config(const char *path, crtube_config_t *config)
{
    FILE *file;
    char ip[16], mask[16], gateway[16], dns[16], extra;
    int count;

    file = fopen(path, "r");
    if (file == NULL)
        return -1;

    count = fscanf(file, "%15s %15s %15s %15s %c",
                   ip, mask, gateway, dns, &extra);
    fclose(file);

    if ((count != 3 && count != 4) || !valid_ipv4(ip) ||
        !valid_ipv4(mask) || !valid_ipv4(gateway))
        return -1;

    if (count == 4 && !valid_ipv4(dns))
        return -1;

    strcpy(config->ip, ip);
    strcpy(config->netmask, mask);
    strcpy(config->gateway, gateway);
    /* Backward compatibility: when no DNS is supplied, use the gateway,
       matching the official PS2SDK TCP/IP sample configuration. */
    strcpy(config->dns, count == 4 ? dns : gateway);
    return 0;
}

static int read_smb_config(const char *path, crtube_config_t *config)
{
    FILE *file;
    char endpoint[22], share[128], extra;
    char *colon;
    unsigned long port;
    char *end;
    int count;

    file = fopen(path, "r");
    if (file == NULL)
        return -1;

    count = fscanf(file, "%21s %127s %c", endpoint, share, &extra);
    fclose(file);
    if (count != 2)
        return -1;

    colon = strchr(endpoint, ':');
    if (colon == NULL || strchr(colon + 1, ':') != NULL)
        return -1;

    *colon = '\0';
    if (!valid_ipv4(endpoint) || colon[1] == '\0')
        return -1;

    port = strtoul(colon + 1, &end, 10);
    if (*end != '\0' || port == 0 || port > 65535)
        return -1;

    strcpy(config->smb_server, endpoint);
    config->smb_port = (unsigned short)port;
    strcpy(config->smb_share, share);
    return 0;
}

static int load_one(int (*reader)(const char *, crtube_config_t *),
                    const char *filename, crtube_config_t *config,
                    char *source, size_t source_size)
{
    char path[64];

    snprintf(path, sizeof(path), "mc0:/CRTUBE/%s", filename);
    if (reader(path, config) == 0) {
        snprintf(source, source_size, "%s", path);
        return 0;
    }

    snprintf(path, sizeof(path), "mc1:/CRTUBE/%s", filename);
    if (reader(path, config) == 0) {
        snprintf(source, source_size, "%s", path);
        return 0;
    }

    source[0] = '\0';
    return -1;
}

int crtube_config_load(crtube_config_t *config)
{
    if (config == NULL)
        return -1;

    memset(config, 0, sizeof(*config));

    if (load_one(read_ip_config, "IPCONFIG.DAT", config,
                 config->ip_config_source, sizeof(config->ip_config_source)) != 0)
        return -2;

    /* SMB settings are optional during this first network/configuration step. */
    load_one(read_smb_config, "SMBCONFIG.DAT", config,
             config->smb_config_source, sizeof(config->smb_config_source));
    return 0;
}
