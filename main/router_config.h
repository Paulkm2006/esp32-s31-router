/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

typedef struct {
    char ssid[33];
    char password[64];
    char gateway[16];
    char netmask[16];
    char dns[16];
    char admin_password[64];
} router_config_t;

esp_err_t router_config_load(void);
const router_config_t *router_config_get(void);
bool router_config_valid(const router_config_t *config);
// Saving affects the next boot; the active configuration remains unchanged.
esp_err_t router_config_save(const router_config_t *config);
