/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "esp_netif.h"
#include "router_storage_config.h"
esp_err_t router_smb_init(esp_netif_t *ap);
esp_err_t router_smb_start(const router_storage_config_t *config);
esp_err_t router_smb_stop(void);
bool router_smb_running(void);
void router_smb_forget(void);
