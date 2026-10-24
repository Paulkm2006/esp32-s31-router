/* SPDX-License-Identifier: Apache-2.0 */
#include <string.h>
#include "router_ipv6.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif_net_stack.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "lwip/esp_netif_net_stack.h"
#include "sdkconfig.h"

#if !CONFIG_ETH_TRANSMIT_MUTEX
#error "Enable CONFIG_ETH_TRANSMIT_MUTEX for concurrent IPv4 and IPv6 Ethernet TX"
#endif

static esp_netif_t *s_ap, *s_wan;
static esp_eth_handle_t s_eth;
static uint8_t s_wan_mac[6];
static struct {
    uint8_t mac[6];
    bool connected;
} s_clients[ESP_WIFI_MAX_CONN_NUM];
static bool s_link, s_ap_up;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static bool client_mac(const uint8_t *mac)
{
    for (size_t i = 0; i < ESP_WIFI_MAX_CONN_NUM; ++i) {
        if (s_clients[i].connected && memcmp(mac, s_clients[i].mac, 6) == 0) {
            return true;
        }
    }
    return false;
}

static esp_err_t input(void *h, void *buffer, size_t len, void *eb)
{
    esp_netif_t *from = esp_netif_get_handle_from_netif_impl(h);
    bool wifi = from == s_ap;
    const uint8_t *frame = buffer;
    // shortcut: untagged Ethernet only; add VLAN handling when tagged ports are needed.
    bool ipv6 = frame != NULL && len >= 14 && frame[12] == 0x86 && frame[13] == 0xdd;
    if (ipv6) {
        bool valid = len >= 54 && len <= 1514 && (frame[14] >> 4) == 6 &&
                     (((size_t)frame[18] << 8) | frame[19]) <= len - 54;
        bool forward = false;
        if (valid) {
            portENTER_CRITICAL(&s_lock);
            bool multicast = frame[0] == 0x33 && frame[1] == 0x33;
            forward = s_link && s_ap_up && (wifi ? client_mac(frame + 6) :
                       !client_mac(frame + 6) && (multicast || client_mac(frame)));
            portEXIT_CRITICAL(&s_lock);
        }
        if (forward) {
            // Both default drivers copy TX data before the RX buffer is released.
            esp_err_t err = esp_netif_transmit(wifi ? s_wan : s_ap, buffer, len);
            if (err != ESP_OK) {
                ESP_LOGD("router_ipv6", "IPv6 TX dropped: %s", esp_err_to_name(err));
            }
        }
        esp_netif_free_rx_buffer(from, wifi ? eb : buffer);
        return ESP_OK;
    }
    // Promiscuous Ethernet RX must not feed unrelated unicast into IPv4 routing.
    if (!wifi && frame != NULL && len >= 14 && !(frame[0] & 1) &&
        memcmp(frame, s_wan_mac, 6) != 0) {
        esp_netif_free_rx_buffer(from, buffer);
        return ESP_OK;
    }
    return (wifi ? ESP_NETIF_NETSTACK_DEFAULT_WIFI_AP : ESP_NETIF_NETSTACK_DEFAULT_ETH)
           ->lwip.input_fn(h, buffer, len, eb);
}

const esp_netif_netstack_config_t router_ipv6_eth_stack = {
    .lwip = {.init_fn = ethernetif_init, .input_fn = input},
};
const esp_netif_netstack_config_t router_ipv6_ap_stack = {
    .lwip = {.init_fn = wlanif_init_ap, .input_fn = input},
};

static void event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    portENTER_CRITICAL(&s_lock);
    if (base == ETH_EVENT && data != NULL && *(esp_eth_handle_t *)data == s_eth) {
        if (id == ETHERNET_EVENT_CONNECTED) {
            s_link = true;
        } else if (id == ETHERNET_EVENT_DISCONNECTED || id == ETHERNET_EVENT_STOP) {
            s_link = false;
        }
    } else if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_AP_START || id == WIFI_EVENT_AP_STOP) {
            s_ap_up = id == WIFI_EVENT_AP_START;
            if (!s_ap_up) {
                memset(s_clients, 0, sizeof(s_clients));
            }
        } else if (id == WIFI_EVENT_AP_STACONNECTED) {
            const wifi_event_ap_staconnected_t *client = data;
            if (client != NULL && client->aid > 0 && client->aid <= ESP_WIFI_MAX_CONN_NUM) {
                memcpy(s_clients[client->aid - 1].mac, client->mac, 6);
                s_clients[client->aid - 1].connected = true;
            }
        } else if (id == WIFI_EVENT_AP_STADISCONNECTED) {
            const wifi_event_ap_stadisconnected_t *client = data;
            if (client != NULL && client->aid > 0 && client->aid <= ESP_WIFI_MAX_CONN_NUM) {
                s_clients[client->aid - 1].connected = false;
            }
        }
    }
    portEXIT_CRITICAL(&s_lock);
}

esp_err_t router_ipv6_init(esp_netif_t *ap, esp_netif_t *wan, esp_eth_handle_t eth)
{
    if (ap == NULL || wan == NULL || eth == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_ap != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = esp_netif_get_mac(wan, s_wan_mac);
    if (err != ESP_OK) {
        return err;
    }
    bool promiscuous = true;
    err = esp_eth_ioctl(eth, ETH_CMD_S_PROMISCUOUS, &promiscuous);
    if (err != ESP_OK) {
        return err;
    }
    s_ap = ap;
    s_wan = wan;
    s_eth = eth;
    err = esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, event, NULL);
    if (err == ESP_OK) {
        err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event, NULL);
        if (err != ESP_OK) {
            esp_event_handler_unregister(ETH_EVENT, ESP_EVENT_ANY_ID, event);
        }
    }
    if (err != ESP_OK) {
        promiscuous = false;
        esp_eth_ioctl(eth, ETH_CMD_S_PROMISCUOUS, &promiscuous);
        s_ap = s_wan = NULL;
        s_eth = NULL;
        return err;
    }
    ESP_LOGI("router_ipv6", "IPv6 passthrough enabled (Ethernet <-> Wi-Fi AP)");
    return ESP_OK;
}
