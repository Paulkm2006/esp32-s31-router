/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "esp_netif.h"
#define ESP_WIFI_MAX_CONN_NUM 10
enum { WIFI_EVENT_AP_START, WIFI_EVENT_AP_STOP,
       WIFI_EVENT_AP_STACONNECTED, WIFI_EVENT_AP_STADISCONNECTED };
typedef struct { uint8_t mac[6], aid; bool is_mesh_child; } wifi_event_ap_staconnected_t;
typedef struct { uint8_t mac[6], aid; bool is_mesh_child; uint16_t reason; } wifi_event_ap_stadisconnected_t;
