/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "usb/msc_host.h"

#define ROUTER_DISK "0:"
esp_err_t router_disk_open(uint8_t address);
esp_err_t router_disk_mount(void);
esp_err_t router_disk_unmount(void);
esp_err_t router_disk_format(void);
void router_disk_close(void);
bool router_disk_mounted(void);
bool router_disk_is(msc_host_device_handle_t device);
esp_err_t router_disk_space(uint64_t *total, uint64_t *free_bytes);
uint64_t router_disk_capacity(void);
