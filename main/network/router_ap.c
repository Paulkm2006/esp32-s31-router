/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stddef.h>
#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "sdkconfig.h"
#include "router_ap.h"
#include "config/router_config.h"
#include "router_ipv6.h"
#include "router_subnet.h"

_Static_assert(sizeof(CONFIG_ROUTER_AP_SSID) > 1 && sizeof(CONFIG_ROUTER_AP_SSID) <= 33,
               "AP SSID must contain 1 to 32 bytes");
_Static_assert(sizeof(CONFIG_ROUTER_AP_PASSWORD) >= 9 && sizeof(CONFIG_ROUTER_AP_PASSWORD) <= 64,
               "AP password must contain 8 to 63 bytes");

_Static_assert(CONFIG_ROUTER_AP_MAX_CLIENTS <= CONFIG_LWIP_DHCPS_MAX_STATION_NUM,
               "DHCP lease capacity must cover all AP clients");

static const char *TAG = "router_ap";
static esp_netif_t *s_ap;

static esp_netif_ip_info_t configured_lan(void)
{
    const router_config_t *config = router_config_get();
    esp_netif_ip_info_t lan = {0};
    ESP_ERROR_CHECK(esp_netif_str_to_ip4(config->gateway, &lan.ip));
    ESP_ERROR_CHECK(esp_netif_str_to_ip4(config->netmask, &lan.netmask));
    lan.gw = lan.ip;
    return lan;
}

esp_netif_t *router_ap_init(void)
{
    esp_netif_config_t netif_config = ESP_NETIF_DEFAULT_WIFI_AP();
    netif_config.stack = &router_ipv6_ap_stack;
    s_ap = esp_netif_new(&netif_config);
    assert(s_ap != NULL);
    ESP_ERROR_CHECK(esp_netif_attach_wifi_ap(s_ap));
    ESP_ERROR_CHECK(esp_wifi_set_default_wifi_ap_handlers());
    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_config));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    wifi_config_t config = {
        .ap = {
            .channel = CONFIG_ROUTER_AP_CHANNEL,
            .max_connection = CONFIG_ROUTER_AP_MAX_CLIENTS,
            .authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    const router_config_t *saved = router_config_get();
    config.ap.ssid_len = strlen(saved->ssid);
    memcpy(config.ap.ssid, saved->ssid, config.ap.ssid_len);
    memcpy(config.ap.password, saved->password, strlen(saved->password));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &config));
    esp_err_t err = esp_netif_dhcps_stop(s_ap);
    ESP_ERROR_CHECK(err == ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED ? ESP_OK : err);
    esp_netif_ip_info_t lan = configured_lan();
    ESP_ERROR_CHECK(esp_netif_set_ip_info(s_ap, &lan));
    esp_netif_dns_info_t dns = {.ip.type = ESP_IPADDR_TYPE_V4};
    ESP_ERROR_CHECK(esp_netif_str_to_ip4(saved->dns[0] ? saved->dns : CONFIG_ROUTER_FALLBACK_DNS,
                                       &dns.ip.u_addr.ip4));
    ESP_ERROR_CHECK(esp_netif_set_dns_info(s_ap, ESP_NETIF_DNS_MAIN, &dns));
    uint8_t offer_dns = 1;
    ESP_ERROR_CHECK(esp_netif_dhcps_option(s_ap, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER,
                                         &offer_dns, sizeof(offer_dns)));
    ESP_ERROR_CHECK(esp_netif_dhcps_start(s_ap));
    return s_ap;
}

esp_err_t router_ap_update(esp_netif_t *wan)
{
    ESP_RETURN_ON_FALSE(s_ap != NULL && wan != NULL, ESP_ERR_INVALID_ARG, TAG, "missing network interface");
    esp_netif_ip_info_t wan_ip, current;
    ESP_RETURN_ON_ERROR(esp_netif_get_ip_info(wan, &wan_ip), TAG, "read WAN address failed");
    ESP_RETURN_ON_ERROR(esp_netif_get_ip_info(s_ap, &current), TAG, "read AP address failed");
    ESP_RETURN_ON_FALSE(wan_ip.ip.addr != 0, ESP_ERR_INVALID_STATE, TAG, "WAN has no IPv4 address");
    esp_netif_ip_info_t lan = configured_lan();
    if (router_subnets_overlap(wan_ip.ip.addr, wan_ip.netmask.addr, lan.ip.addr, lan.netmask.addr)) {
        const uint32_t gateways[] = {
            ESP_IP4TOADDR(192, 168, 4, 1), ESP_IP4TOADDR(10, 42, 0, 1), ESP_IP4TOADDR(172, 20, 0, 1),
        };
        lan.netmask.addr = ESP_IP4TOADDR(255, 255, 255, 0);
        for (size_t i = 0; i < sizeof(gateways) / sizeof(gateways[0]); ++i) {
            lan.ip.addr = gateways[i];
            lan.gw = lan.ip;
            if (!router_subnets_overlap(wan_ip.ip.addr, wan_ip.netmask.addr, lan.ip.addr, lan.netmask.addr)) {
                break;
            }
        }
        ESP_RETURN_ON_FALSE(!router_subnets_overlap(wan_ip.ip.addr, wan_ip.netmask.addr, lan.ip.addr, lan.netmask.addr), ESP_ERR_INVALID_STATE, TAG,
                            "WAN overlaps all available AP subnets");
        ESP_LOGW(TAG, "Configured LAN overlaps WAN; using temporary fallback subnet");
    }
    assert(!router_subnets_overlap(wan_ip.ip.addr, wan_ip.netmask.addr, lan.ip.addr, lan.netmask.addr));

    esp_netif_dns_info_t dns = {0}, old_dns = {0};
    const char *manual_dns = router_config_get()->dns;
    if (manual_dns[0]) {
        dns.ip.type = ESP_IPADDR_TYPE_V4;
        ESP_RETURN_ON_ERROR(esp_netif_str_to_ip4(manual_dns, &dns.ip.u_addr.ip4), TAG, "invalid manual DNS");
    } else {
        ESP_RETURN_ON_ERROR(esp_netif_get_dns_info(wan, ESP_NETIF_DNS_MAIN, &dns), TAG, "read WAN DNS failed");
        if (dns.ip.type != ESP_IPADDR_TYPE_V4 || dns.ip.u_addr.ip4.addr == 0) {
            ESP_RETURN_ON_ERROR(esp_netif_get_dns_info(wan, ESP_NETIF_DNS_BACKUP, &dns), TAG, "read backup DNS failed");
        }
    }
    if (dns.ip.type != ESP_IPADDR_TYPE_V4 || dns.ip.u_addr.ip4.addr == 0) {
        dns.ip.type = ESP_IPADDR_TYPE_V4;
        ESP_RETURN_ON_ERROR(esp_netif_str_to_ip4(CONFIG_ROUTER_FALLBACK_DNS, &dns.ip.u_addr.ip4), TAG,
                            "fallback DNS must be an IPv4 address");
        ESP_RETURN_ON_FALSE(dns.ip.u_addr.ip4.addr != 0, ESP_ERR_INVALID_ARG, TAG, "fallback DNS cannot be zero");
    }
    ESP_RETURN_ON_ERROR(esp_netif_get_dns_info(s_ap, ESP_NETIF_DNS_MAIN, &old_dns), TAG, "read AP DNS failed");
    bool lan_changed = lan.ip.addr != current.ip.addr || lan.netmask.addr != current.netmask.addr ||
                       lan.gw.addr != current.gw.addr;
    bool dns_changed = old_dns.ip.type != dns.ip.type || old_dns.ip.u_addr.ip4.addr != dns.ip.u_addr.ip4.addr;
    if (lan_changed || dns_changed) {
        esp_err_t err = esp_netif_dhcps_stop(s_ap);
        ESP_RETURN_ON_FALSE(err == ESP_OK || err == ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED, err, TAG,
                            "stop AP DHCP failed");
        err = lan_changed ? esp_netif_set_ip_info(s_ap, &lan) : ESP_OK;
        if (err == ESP_OK) {
            err = esp_netif_set_dns_info(s_ap, ESP_NETIF_DNS_MAIN, &dns);
        }
        esp_err_t dhcp_err = esp_netif_dhcps_start(s_ap);
        ESP_RETURN_ON_ERROR(err, TAG, "update AP network failed");
        ESP_RETURN_ON_ERROR(dhcp_err, TAG, "restart AP DHCP failed");
        // Existing clients must renew their DHCP lease after a subnet or DNS change.
        ESP_RETURN_ON_ERROR(esp_wifi_deauth_sta(0), TAG, "reconnect AP clients failed");
    }
    ESP_LOGI(TAG, "AP gateway: " IPSTR ", DNS: " IPSTR, IP2STR(&lan.ip), IP2STR(&dns.ip.u_addr.ip4));
    return ESP_OK;
}
