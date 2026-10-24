/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct { uint8_t mac[6]; } esp_netif_t;
typedef struct esp_netif_netstack_config esp_netif_netstack_config_t;
esp_netif_t *esp_netif_get_handle_from_netif_impl(void *impl);
esp_err_t esp_netif_get_mac(esp_netif_t *netif, uint8_t *mac);
esp_err_t esp_netif_transmit(esp_netif_t *netif, void *data, size_t len);
void esp_netif_free_rx_buffer(void *netif, void *buffer);
