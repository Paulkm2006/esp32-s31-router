/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "cJSON.h"
#include "esp_netif.h"
esp_err_t router_storage_start(esp_netif_t *ap);
cJSON *router_storage_json(void);
esp_err_t router_storage_request(const cJSON *request);
esp_err_t router_storage_prepare_reboot(void);
void router_storage_cancel_reboot(void);
