/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "esp_netif.h"
typedef int err_t;
struct netif { int unused; };
struct esp_netif_netstack_config {
    struct {
        err_t (*init_fn)(struct netif *);
        esp_err_t (*input_fn)(void *, void *, size_t, void *);
    } lwip;
};
err_t ethernetif_init(struct netif *netif);
err_t wlanif_init_ap(struct netif *netif);
extern const esp_netif_netstack_config_t test_default_eth, test_default_ap;
#define ESP_NETIF_NETSTACK_DEFAULT_ETH (&test_default_eth)
#define ESP_NETIF_NETSTACK_DEFAULT_WIFI_AP (&test_default_ap)
