/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "esp_netif.h"
typedef void *esp_eth_handle_t;
enum { ETH_CMD_S_PROMISCUOUS, ETH_CMD_S_ALL_MULTICAST,
       ETH_CMD_ADD_MAC_FILTER, ETH_CMD_DEL_MAC_FILTER, ETHERNET_EVENT_CONNECTED,
       ETHERNET_EVENT_DISCONNECTED, ETHERNET_EVENT_STOP };
esp_err_t esp_eth_ioctl(esp_eth_handle_t handle, int cmd, void *data);
