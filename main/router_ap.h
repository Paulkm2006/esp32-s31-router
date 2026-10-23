/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "esp_netif.h"

esp_netif_t *router_ap_init(void);
esp_err_t router_ap_update(esp_netif_t *wan);
