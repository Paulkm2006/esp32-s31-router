/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "ethernet_init.h"
#include "nvs_flash.h"
#include "router_ap.h"
#include "router_config.h"
#include "router_http.h"
#include "router_ipv6.h"
#include "router_status.h"

static const char *TAG = "router";
static esp_netif_t *wan;
static esp_netif_t *ap;
static bool wan_ready;
static bool ap_ready;
static bool napt_enabled;
static bool http_started;

static void update_forwarding(void)
{
    if (napt_enabled) {
        if (esp_netif_is_netif_up(ap)) {
            ESP_ERROR_CHECK(esp_netif_napt_disable(ap));
        }
        napt_enabled = false;
    }
    if (!wan_ready || !ap_ready) {
        ESP_LOGI(TAG, "Internet forwarding disabled");
        return;
    }

    esp_err_t err = router_ap_update(wan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot configure LAN: %s", esp_err_to_name(err));
        return;
    }
    ESP_ERROR_CHECK(esp_netif_set_default_netif(wan));
    ESP_ERROR_CHECK(esp_netif_napt_enable(ap));
    napt_enabled = true;
    ESP_LOGI(TAG, "Internet forwarding ready (Ethernet -> Wi-Fi AP)");
}

static void network_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == ETH_EVENT) {
        if (id == ETHERNET_EVENT_CONNECTED) {
            ESP_LOGI(TAG, "WAN Ethernet link up; waiting for DHCP");
        } else if (id == ETHERNET_EVENT_DISCONNECTED || id == ETHERNET_EVENT_STOP) {
            wan_ready = false;
            update_forwarding();
            ESP_LOGI(TAG, "WAN Ethernet offline; AP remains available");
        }
    } else if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_AP_START || id == WIFI_EVENT_AP_STOP) {
            ap_ready = id == WIFI_EVENT_AP_START;
            if (ap_ready && !http_started) {
                ESP_ERROR_CHECK(router_http_start(ap));
                http_started = true;
            }
            update_forwarding();
        }
    } else if (base == IP_EVENT) {
        if (id == IP_EVENT_ETH_GOT_IP) {
            const ip_event_got_ip_t *event = data;
            if (event->esp_netif != wan) {
                return;
            }
            ESP_LOGI(TAG, "WAN IP: " IPSTR " mask: " IPSTR " gateway: " IPSTR,
                     IP2STR(&event->ip_info.ip), IP2STR(&event->ip_info.netmask),
                     IP2STR(&event->ip_info.gw));
            wan_ready = event->ip_info.gw.addr != 0;
            if (!wan_ready) {
                ESP_LOGW(TAG, "DHCP supplied no default gateway");
            }
            update_forwarding();
        } else if (id == IP_EVENT_ETH_LOST_IP) {
            wan_ready = false;
            update_forwarding();
        } else if (id == IP_EVENT_ASSIGNED_IP_TO_CLIENT) {
            const ip_event_assigned_ip_to_client_t *event = data;
            ESP_LOGI(TAG, "LAN DHCP lease: " IPSTR, IP2STR(&event->ip));
        }
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(router_config_load());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_eth_handle_t *handles = NULL;
    uint8_t count = 0;
    ESP_ERROR_CHECK(ethernet_init_all(&handles, &count));
    ESP_ERROR_CHECK(count == 1 ? ESP_OK : ESP_ERR_INVALID_STATE);
    esp_netif_config_t config = ESP_NETIF_DEFAULT_ETH();
    config.stack = &router_ipv6_eth_stack;
    wan = esp_netif_new(&config);
    assert(wan != NULL);
    esp_eth_netif_glue_handle_t glue = esp_eth_new_netif_glue(handles[0]);
    assert(glue != NULL);
    ESP_ERROR_CHECK(esp_netif_attach(wan, glue));
    ESP_ERROR_CHECK(esp_netif_set_hostname(wan, "esp32-s31-router"));

    ap = router_ap_init();
    ESP_ERROR_CHECK(router_ipv6_init(ap, wan, handles[0]));
    ESP_ERROR_CHECK(router_status_init(ap, wan, handles[0]));
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, network_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, network_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, network_event, NULL));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_eth_start(handles[0]));
    ESP_LOGI(TAG, "AP SSID: %s (WPA2-PSK)", router_config_get()->ssid);
}
