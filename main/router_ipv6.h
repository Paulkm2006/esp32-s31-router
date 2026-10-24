/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "esp_eth.h"
#include "esp_netif.h"

extern const esp_netif_netstack_config_t router_ipv6_eth_stack;
extern const esp_netif_netstack_config_t router_ipv6_ap_stack;

esp_err_t router_ipv6_init(esp_netif_t *ap, esp_netif_t *wan, esp_eth_handle_t eth);
