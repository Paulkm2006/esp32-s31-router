/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <net/if.h>
typedef int esp_err_t;
typedef void esp_netif_t;
typedef void *msc_host_device_handle_t;
typedef void *SemaphoreHandle_t;
typedef int BaseType_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_TIMEOUT 3
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define portMAX_DELAY 0xffffffffU
#define pdTRUE 1
#define pdPASS 1
#define pdMS_TO_TICKS(x) (x)
void *heap_caps_calloc(size_t n, size_t size, unsigned caps);
void *heap_caps_malloc(size_t size, unsigned caps);
void esp_fill_random(void *data, size_t size);
SemaphoreHandle_t xSemaphoreCreateBinary(void);
int xSemaphoreTake(SemaphoreHandle_t semaphore, unsigned ticks);
int xSemaphoreGive(SemaphoreHandle_t semaphore);
int xTaskCreate(void (*fn)(void *), const char *name, unsigned stack, void *arg, unsigned priority, void *handle);
void vTaskDelete(void *handle);
void vTaskDelay(unsigned ticks);
int esp_netif_get_netif_impl_name(esp_netif_t *ap, char *name);
