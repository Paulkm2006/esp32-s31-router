/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    uint8_t enabled;
    char share[33];
    char username[33];
    char password[64];
} router_storage_config_t;

bool router_storage_config_valid(const router_storage_config_t *config);
esp_err_t router_storage_config_load(router_storage_config_t *config);
esp_err_t router_storage_config_save(const router_storage_config_t *config);
