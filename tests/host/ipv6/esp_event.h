/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "esp_netif.h"
typedef const char *esp_event_base_t;
typedef void (*esp_event_handler_t)(void *, esp_event_base_t, int32_t, void *);
extern const char ETH_EVENT[], WIFI_EVENT[];
#define ESP_EVENT_ANY_ID -1
esp_err_t esp_event_handler_register(esp_event_base_t base, int32_t id,
                                     esp_event_handler_t handler, void *arg);
esp_err_t esp_event_handler_unregister(esp_event_base_t base, int32_t id,
                                       esp_event_handler_t handler);
