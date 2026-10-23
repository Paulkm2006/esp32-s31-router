/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "router_config.h"
#include "router_status.h"

typedef struct {
    uint8_t mac[6];
    int64_t since;
    bool connected;
} connection_t;

static esp_netif_t *s_ap, *s_wan;
static esp_eth_handle_t s_eth;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static connection_t s_clients[ESP_WIFI_MAX_CONN_NUM];
static bool s_link, s_ready;

static void track_client(connection_t *clients, const uint8_t *mac, bool connected, int64_t now)
{
    connection_t *empty = NULL;
    for (int i = 0; i < ESP_WIFI_MAX_CONN_NUM; ++i) {
        if (clients[i].connected && memcmp(clients[i].mac, mac, 6) == 0) {
            clients[i].connected = connected;
            clients[i].since = now;
            return;
        }
        if (!clients[i].connected && empty == NULL) {
            empty = &clients[i];
        }
    }
    if (connected && empty != NULL) {
        memcpy(empty->mac, mac, 6);
        empty->connected = true;
        empty->since = now;
    }
}

static void status_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&s_lock);
    if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_AP_STACONNECTED) {
            const wifi_event_ap_staconnected_t *event = data;
            track_client(s_clients, event->mac, true, now);
        } else if (id == WIFI_EVENT_AP_STADISCONNECTED) {
            const wifi_event_ap_stadisconnected_t *event = data;
            track_client(s_clients, event->mac, false, now);
        } else if (id == WIFI_EVENT_AP_STOP) {
            memset(s_clients, 0, sizeof(s_clients));
        }
    } else if (base == ETH_EVENT && data != NULL && *(esp_eth_handle_t *)data == s_eth) {
        if (id == ETHERNET_EVENT_CONNECTED) {
            s_link = true;
        } else if (id == ETHERNET_EVENT_DISCONNECTED || id == ETHERNET_EVENT_STOP) {
            s_link = s_ready = false;
        }
    } else if (base == IP_EVENT &&
               (id == IP_EVENT_ETH_GOT_IP || id == IP_EVENT_ETH_LOST_IP)) {
        const ip_event_got_ip_t *event = data;
        if (event->esp_netif == s_wan) {
            s_ready = id == IP_EVENT_ETH_GOT_IP && event->ip_info.ip.addr != 0 &&
                      event->ip_info.gw.addr != 0;
        }
    }
    portEXIT_CRITICAL(&s_lock);
}

esp_err_t router_status_init(esp_netif_t *ap, esp_netif_t *wan, esp_eth_handle_t eth)
{
    if (ap == NULL || wan == NULL || eth == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_ap != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
#ifndef NDEBUG
    connection_t clients[ESP_WIFI_MAX_CONN_NUM] = {0};
    const uint8_t mac[6] = {1, 2, 3, 4, 5, 6};
    track_client(clients, mac, true, 1000000);
    assert(clients[0].connected && clients[0].since == 1000000);
    track_client(clients, mac, false, 3000000);
    assert(!clients[0].connected);
    track_client(clients, mac, true, 9000000);
    assert(clients[0].connected && clients[0].since == 9000000);
#endif
    s_ap = ap;
    s_wan = wan;
    s_eth = eth;
    esp_err_t err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, status_event, NULL);
    if (err == ESP_OK) {
        err = esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, status_event, NULL);
        if (err == ESP_OK) {
            err = esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, status_event, NULL);
            if (err != ESP_OK) {
                esp_event_handler_unregister(ETH_EVENT, ESP_EVENT_ANY_ID, status_event);
            }
        }
        if (err != ESP_OK) {
            esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, status_event);
        }
    }
    if (err != ESP_OK) {
        s_ap = s_wan = NULL;
        s_eth = NULL;
    }
    return err;
}

static bool add_ip(cJSON *object, const char *key, const esp_ip4_addr_t *ip)
{
    char address[16] = "";
    if (ip->addr != 0) {
        snprintf(address, sizeof(address), IPSTR, IP2STR(ip));
    }
    return cJSON_AddStringToObject(object, key, address) != NULL;
}

static bool add_netif(cJSON *object, esp_netif_t *netif, bool gateway)
{
    esp_netif_ip_info_t info = {0};
    esp_netif_dns_info_t dns = {0};
    if (esp_netif_get_ip_info(netif, &info) != ESP_OK ||
        esp_netif_get_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns) != ESP_OK) {
        return false;
    }
    esp_ip4_addr_t dns_ip = {0};
    if (dns.ip.type == ESP_IPADDR_TYPE_V4) {
        dns_ip = dns.ip.u_addr.ip4;
    }
    return add_ip(object, "ip", &info.ip) && add_ip(object, "netmask", &info.netmask) &&
           add_ip(object, "dns", &dns_ip) && (!gateway || add_ip(object, "gateway", &info.gw));
}

static bool add_clients(cJSON *array, const connection_t *connections, int64_t now)
{
    wifi_sta_list_t stations = {0};
    if (esp_wifi_ap_get_sta_list(&stations) != ESP_OK) {
        return false;
    }
    esp_netif_pair_mac_ip_t leases[ESP_WIFI_MAX_CONN_NUM] = {0};
    for (int i = 0; i < stations.num; ++i) {
        memcpy(leases[i].mac, stations.sta[i].mac, 6);
    }
    if (esp_netif_dhcps_get_clients_by_mac(s_ap, stations.num, leases) != ESP_OK) {
        return false;
    }
    for (int i = 0; i < stations.num; ++i) {
        cJSON *client = cJSON_CreateObject();
        if (client == NULL || !cJSON_AddItemToArray(array, client)) {
            cJSON_Delete(client);
            return false;
        }
        char mac[18];
        snprintf(mac, sizeof(mac), MACSTR, MAC2STR(stations.sta[i].mac));
        int64_t seconds = -1;
        for (int j = 0; j < ESP_WIFI_MAX_CONN_NUM; ++j) {
            if (connections[j].connected && memcmp(connections[j].mac, stations.sta[i].mac, 6) == 0) {
                seconds = (now - connections[j].since) / 1000000;
                break;
            }
        }
        cJSON *duration = seconds < 0 ? cJSON_AddNullToObject(client, "connected_seconds") :
                                       cJSON_AddNumberToObject(client, "connected_seconds", seconds);
        // shortcut: DHCP leases cannot identify static addresses; add ARP discovery if needed.
        if (duration == NULL || !add_ip(client, "ip", &leases[i].ip) ||
            cJSON_AddStringToObject(client, "mac", mac) == NULL) {
            return false;
        }
    }
    return true;
}

cJSON *router_status_json(void)
{
    if (s_ap == NULL) {
        return NULL;
    }
    connection_t clients[ESP_WIFI_MAX_CONN_NUM];
    bool link, ready;
    portENTER_CRITICAL(&s_lock);
    memcpy(clients, s_clients, sizeof(clients));
    link = s_link;
    ready = s_ready && link;
    portEXIT_CRITICAL(&s_lock);

    cJSON *root = cJSON_CreateObject();
    cJSON *wan = cJSON_AddObjectToObject(root, "wan");
    cJSON *ap = cJSON_AddObjectToObject(root, "ap");
    cJSON *config = cJSON_AddObjectToObject(root, "config");
    cJSON *devices = cJSON_AddArrayToObject(root, "clients");
    const router_config_t *saved = router_config_get();
    uint8_t mac[6] = {0};
    char mac_string[18];
    eth_speed_t speed = ETH_SPEED_10M;
    eth_duplex_t duplex = ETH_DUPLEX_HALF;
    int speed_mbps = 0;
    if (link && esp_eth_ioctl(s_eth, ETH_CMD_G_SPEED, &speed) == ESP_OK) {
        speed_mbps = speed == ETH_SPEED_1000M ? 1000 : speed == ETH_SPEED_100M ? 100 : 10;
    }
    bool full_duplex = link && esp_eth_ioctl(s_eth, ETH_CMD_G_DUPLEX_MODE, &duplex) == ESP_OK &&
                       duplex == ETH_DUPLEX_FULL;
    if (esp_netif_get_mac(s_wan, mac) != ESP_OK) {
        cJSON_Delete(root);
        return NULL;
    }
    snprintf(mac_string, sizeof(mac_string), MACSTR, MAC2STR(mac));
    if (root == NULL || wan == NULL || ap == NULL || config == NULL || devices == NULL ||
        !add_netif(wan, s_wan, true) || !add_netif(ap, s_ap, false) ||
        cJSON_AddBoolToObject(wan, "link", link) == NULL ||
        cJSON_AddBoolToObject(wan, "ready", ready) == NULL ||
        cJSON_AddStringToObject(wan, "mac", mac_string) == NULL ||
        cJSON_AddNumberToObject(wan, "speed_mbps", speed_mbps) == NULL ||
        cJSON_AddBoolToObject(wan, "full_duplex", full_duplex) == NULL ||
        cJSON_AddStringToObject(config, "ssid", saved->ssid) == NULL ||
        cJSON_AddStringToObject(config, "gateway", saved->gateway) == NULL ||
        cJSON_AddStringToObject(config, "netmask", saved->netmask) == NULL ||
        cJSON_AddStringToObject(config, "dns", saved->dns) == NULL ||
        !add_clients(devices, clients, esp_timer_get_time())) {
        cJSON_Delete(root);
        return NULL;
    }
    return root;
}
