/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <string.h>
#include "esp_event.h"
#include "esp_wifi.h"
#include "lwip/esp_netif_net_stack.h"
#include "../main/network/router_ipv6.h"

const char ETH_EVENT[] = "eth", WIFI_EVENT[] = "wifi";
static esp_netif_t ap = {{2, 0, 0, 0, 0, 1}}, wan = {{2, 0, 0, 0, 0, 2}};
static const uint8_t client[6] = {2, 1, 2, 3, 4, 5}, upstream[6] = {2, 6, 7, 8, 9, 10};
static const uint8_t multicast[6] = {0x33, 0x33, 0, 0, 0, 1};
static esp_event_handler_t handlers[2];
static int registrations, fail_registration, releases, transmissions, local_inputs;
static esp_err_t mac_result, ioctl_result, tx_result;
static bool promiscuous;
static uint8_t frame[1600], expected[1600], wifi_buffer;
static void *expected_free;
static esp_netif_t *expected_target, *expected_source;

esp_netif_t *esp_netif_get_handle_from_netif_impl(void *impl) { return impl; }
esp_err_t esp_netif_get_mac(esp_netif_t *netif, uint8_t *mac)
{
    memcpy(mac, netif->mac, 6);
    return mac_result;
}
esp_err_t esp_eth_ioctl(esp_eth_handle_t handle, int cmd, void *data)
{
    assert(handle == &wan && cmd == ETH_CMD_S_PROMISCUOUS);
    if (ioctl_result == ESP_OK) { promiscuous = *(bool *)data; }
    return ioctl_result;
}
esp_err_t esp_event_handler_register(esp_event_base_t base, int32_t id,
                                     esp_event_handler_t handler, void *arg)
{
    assert(id == ESP_EVENT_ANY_ID && arg == NULL);
    if (++registrations == fail_registration) { return ESP_FAIL; }
    handlers[base == WIFI_EVENT] = handler;
    return ESP_OK;
}
esp_err_t esp_event_handler_unregister(esp_event_base_t base, int32_t id,
                                       esp_event_handler_t handler)
{
    assert(id == ESP_EVENT_ANY_ID && handlers[base == WIFI_EVENT] == handler);
    handlers[base == WIFI_EVENT] = NULL;
    return ESP_OK;
}
void esp_netif_free_rx_buffer(void *netif, void *buffer)
{
    assert(netif == expected_source && buffer == expected_free && releases++ == 0);
}
esp_err_t esp_netif_transmit(esp_netif_t *netif, void *data, size_t len)
{
    assert(netif == expected_target && releases == 0 && transmissions++ == 0);
    assert(data == frame && memcmp(data, expected, len) == 0);
    return tx_result;
}
static esp_err_t local_input(void *h, void *buffer, size_t len, void *eb)
{
    (void)len;
    ++local_inputs;
    esp_netif_free_rx_buffer(h, h == &ap ? eb : buffer);
    return ESP_OK;
}
err_t ethernetif_init(struct netif *netif) { (void)netif; return 0; }
err_t wlanif_init_ap(struct netif *netif) { (void)netif; return 0; }
const esp_netif_netstack_config_t test_default_eth = {.lwip = {.input_fn = local_input}};
const esp_netif_netstack_config_t test_default_ap = {.lwip = {.input_fn = local_input}};

static void packet(const uint8_t *dest, const uint8_t *source)
{
    memset(frame, 0, sizeof(frame));
    memcpy(frame, dest, 6);
    memcpy(frame + 6, source, 6);
    frame[12] = 0x86; frame[13] = 0xdd; frame[14] = 0x60;
    frame[19] = 8; frame[20] = 58; frame[21] = 255; frame[54] = 134;
}
static void receive(bool wifi, size_t len, esp_netif_t *target, bool local)
{
    const esp_netif_netstack_config_t *stack = wifi ? &router_ipv6_ap_stack : &router_ipv6_eth_stack;
    expected_source = wifi ? &ap : &wan;
    expected_target = target;
    expected_free = wifi ? (void *)&wifi_buffer : frame;
    memcpy(expected, frame, sizeof(frame));
    releases = transmissions = local_inputs = 0;
    assert(stack->lwip.input_fn(expected_source, frame, len, &wifi_buffer) == ESP_OK);
    assert(releases == 1 && transmissions == (target != NULL) && local_inputs == local);
    assert(memcmp(frame, expected, sizeof(frame)) == 0);
}
static void emit(esp_event_base_t base, int32_t id, void *data)
{
    assert(handlers[base == WIFI_EVENT] != NULL);
    handlers[base == WIFI_EVENT](NULL, base, id, data);
}

int main(void)
{
    assert(router_ipv6_init(NULL, &wan, &wan) == ESP_ERR_INVALID_ARG);
    mac_result = ESP_FAIL;
    assert(router_ipv6_init(&ap, &wan, &wan) == ESP_FAIL);
    mac_result = ESP_OK; ioctl_result = ESP_FAIL;
    assert(router_ipv6_init(&ap, &wan, &wan) == ESP_FAIL);
    ioctl_result = ESP_OK;
    for (int failure = 1; failure <= 2; ++failure) {
        registrations = 0; fail_registration = failure;
        assert(router_ipv6_init(&ap, &wan, &wan) == ESP_FAIL);
        assert(!promiscuous && handlers[0] == NULL && handlers[1] == NULL);
    }
    fail_registration = registrations = 0;
    assert(router_ipv6_init(&ap, &wan, &wan) == ESP_OK && promiscuous);
    assert(router_ipv6_init(&ap, &wan, &wan) == ESP_ERR_INVALID_STATE);
    esp_eth_handle_t eth = &wan, other_eth = &ap;
    wifi_event_ap_staconnected_t station = {.aid = 1};
    memcpy(station.mac, client, 6);
    packet(multicast, upstream);
    receive(false, 62, NULL, false);
    emit(WIFI_EVENT, WIFI_EVENT_AP_START, NULL);
    emit(WIFI_EVENT, WIFI_EVENT_AP_STACONNECTED, &station);
    emit(ETH_EVENT, ETHERNET_EVENT_CONNECTED, &other_eth);
    receive(false, 62, NULL, false);
    emit(ETH_EVENT, ETHERNET_EVENT_CONNECTED, &eth);
    receive(false, 62, &ap, false);
    frame[54] = 135; receive(false, 62, &ap, false);
    packet(upstream, client); receive(true, 62, &wan, false);
    packet(client, upstream); receive(false, 62, &ap, false);
    packet(upstream, upstream); receive(false, 62, NULL, false);
    receive(true, 62, NULL, false);
    packet(multicast, client); receive(false, 62, NULL, false);
    tx_result = ESP_FAIL; receive(true, 62, &wan, false); tx_result = ESP_OK;
    for (size_t len = 14; len < 54; ++len) { receive(true, len, NULL, false); }
    receive(true, 1515, NULL, false);
    frame[14] = 0x40; receive(true, 62, NULL, false); frame[14] = 0x60;
    frame[18] = 1; receive(true, 62, NULL, false); frame[18] = 0;
    frame[19] = 9; receive(true, 62, NULL, false); frame[19] = 8;
    emit(ETH_EVENT, ETHERNET_EVENT_DISCONNECTED, &eth); receive(true, 62, NULL, false);
    emit(ETH_EVENT, ETHERNET_EVENT_CONNECTED, &eth); receive(true, 62, &wan, false);
    emit(ETH_EVENT, ETHERNET_EVENT_STOP, &eth); receive(true, 62, NULL, false);
    emit(ETH_EVENT, ETHERNET_EVENT_CONNECTED, &eth);
    wifi_event_ap_stadisconnected_t departed = {.aid = 1};
    memcpy(departed.mac, client, 6);
    emit(WIFI_EVENT, WIFI_EVENT_AP_STADISCONNECTED, &departed); receive(true, 62, NULL, false);
    station.aid = 0; emit(WIFI_EVENT, WIFI_EVENT_AP_STACONNECTED, &station); receive(true, 62, NULL, false);
    station.aid = ESP_WIFI_MAX_CONN_NUM; emit(WIFI_EVENT, WIFI_EVENT_AP_STACONNECTED, &station);
    receive(true, 62, &wan, false);
    emit(WIFI_EVENT, WIFI_EVENT_AP_STOP, NULL); receive(true, 62, NULL, false);
    emit(WIFI_EVENT, WIFI_EVENT_AP_START, NULL); receive(true, 62, NULL, false);
    packet(wan.mac, upstream); frame[12] = 8; frame[13] = 0;
    receive(false, 62, NULL, true); receive(true, 62, NULL, true);
    receive(false, 0, NULL, true); receive(false, 13, NULL, true);
    memcpy(frame, client, 6); receive(false, 62, NULL, false);
    memset(frame, 0xff, 6); frame[13] = 6; receive(false, 62, NULL, true);
    return 0;
}
