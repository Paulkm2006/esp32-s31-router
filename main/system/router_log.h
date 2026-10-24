/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>
#include "cJSON.h"
#include "esp_err.h"

esp_err_t router_log_start(void);
cJSON *router_log_json(uint64_t after);
