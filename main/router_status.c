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
