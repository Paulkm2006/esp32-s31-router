/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "cJSON.h"
#include "esp_eth.h"
#include "esp_netif.h"

// Register before starting Wi-Fi and Ethernet so connection durations are complete.
esp_err_t router_status_init(esp_netif_t *ap, esp_netif_t *wan, esp_eth_handle_t eth);
cJSON *router_status_json(void);
