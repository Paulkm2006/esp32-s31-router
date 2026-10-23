/* SPDX-License-Identifier: Apache-2.0 */
#include <stdint.h>
#include <string.h>
#include "nvs.h"
#include "sdkconfig.h"
#include "router_config.h"

#define CONFIG_DEFAULTS { .ssid = CONFIG_ROUTER_AP_SSID, .password = CONFIG_ROUTER_AP_PASSWORD, \
    .gateway = "192.168.4.1", .netmask = "255.255.255.0", .dns = "", \
    .admin_password = CONFIG_ROUTER_ADMIN_PASSWORD }

static router_config_t s_config = CONFIG_DEFAULTS;

static bool ipv4(const char *text, uint32_t *ip)
{
    *ip = 0;
    for (unsigned i = 0; i < 4; ++i) {
        const char *start = text;
        unsigned octet = 0;
        while (*text >= '0' && *text <= '9') {
            octet = octet * 10 + (unsigned)(*text++ - '0');
            if (octet > 255 || text - start > 3) {
                return false;
            }
        }
        if (text == start || (text - start > 1 && *start == '0') ||
            *text != (i == 3 ? '\0' : '.')) {
            return false;
        }
        *ip = (*ip << 8) | octet;
        if (i != 3) {
            ++text;
        }
    }
    return true;
}

static bool private_ip(uint32_t ip)
{
    return (ip & 0xff000000U) == 0x0a000000U || (ip & 0xfff00000U) == 0xac100000U ||
           (ip & 0xffff0000U) == 0xc0a80000U;
}

static bool text_valid(const char *text, size_t size, size_t min)
{
    const char *end = memchr(text, '\0', size);
    if (end == NULL || (size_t)(end - text) < min) {
        return false;
    }
    for (; text < end; ++text) {
        if ((unsigned char)*text < 32 || (unsigned char)*text == 127) {
            return false;
        }
    }
    return true;
}

bool router_config_valid(const router_config_t *config)
{
    if (config == NULL || !text_valid(config->ssid, sizeof(config->ssid), 1) ||
        !text_valid(config->password, sizeof(config->password), 8) ||
        !text_valid(config->admin_password, sizeof(config->admin_password), 8) ||
        !text_valid(config->gateway, sizeof(config->gateway), 1) ||
        !text_valid(config->netmask, sizeof(config->netmask), 1) ||
        !text_valid(config->dns, sizeof(config->dns), 0)) {
        return false;
    }
    uint32_t ip, mask, dns;
    if (!ipv4(config->gateway, &ip) || !ipv4(config->netmask, &mask)) {
        return false;
    }
    uint32_t hosts = ~mask, host = ip & hosts;
    if (mask < 0xff000000U || mask > 0xfffffff0U || (hosts & (hosts + 1)) != 0 ||
        host == 0 || host == hosts || !private_ip(ip & mask) || !private_ip(ip | hosts) ||
        (host - 1 < CONFIG_ROUTER_AP_MAX_CLIENTS && hosts - host - 1 < CONFIG_ROUTER_AP_MAX_CLIENTS)) {
        return false;
    }
    return config->dns[0] == '\0' || (ipv4(config->dns, &dns) && (dns >> 24) != 0 &&
           (dns >> 24) != 127 && (dns >> 24) < 224);
}

const router_config_t *router_config_get(void)
{
    return &s_config;
}

typedef struct {
    uint32_t version;
    router_config_t config;
} stored_config_t;

esp_err_t router_config_load(void)
{
    s_config = (router_config_t)CONFIG_DEFAULTS;
    if (!router_config_valid(&s_config)) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t handle;
    esp_err_t err = nvs_open("router", NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }
    stored_config_t stored = {0};
    size_t size = sizeof(stored);
    err = nvs_get_blob(handle, "config", &stored, &size);
    nvs_close(handle);
    if (err == ESP_OK && size == sizeof(stored) && stored.version == 1 && router_config_valid(&stored.config)) {
        s_config = stored.config;
    }
    return err == ESP_ERR_NVS_NOT_FOUND || err == ESP_ERR_NVS_INVALID_LENGTH ? ESP_OK : err;
}

esp_err_t router_config_save(const router_config_t *config)
{
    if (!router_config_valid(config)) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t handle;
    esp_err_t err = nvs_open("router", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    const stored_config_t stored = {.version = 1, .config = *config};
    err = nvs_set_blob(handle, "config", &stored, sizeof(stored));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}
