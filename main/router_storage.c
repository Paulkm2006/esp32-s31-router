/* SPDX-License-Identifier: Apache-2.0 */
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "usb/usb_host.h"
#include "router_disk.h"
#include "router_smb.h"
#include "router_storage.h"

typedef enum {MOUNT = 1, UNMOUNT, FORMAT, CONFIGURE, CONNECT, DISCONNECT} action_t;
typedef struct {
    action_t action;
    uint32_t media;
    uint8_t address;
    msc_host_device_handle_t device;
    router_storage_config_t config;
} storage_message_t;
static QueueHandle_t queue;
static SemaphoreHandle_t lock;
static bool connected, mounted, busy, ready;
static volatile bool unplugged;
static uint32_t media;
static uint64_t capacity, total, available;
static char error[160];
static router_storage_config_t config;

static void refresh_state(esp_err_t result)
{
    uint64_t size = 0, free_bytes = 0;
    bool is_mounted = router_disk_mounted();
    if (is_mounted && router_disk_space(&size, &free_bytes) != ESP_OK && result == ESP_OK) result = ESP_FAIL;
    xSemaphoreTake(lock, portMAX_DELAY);
    mounted = is_mounted;
    total = size; available = free_bytes; capacity = router_disk_capacity();
    busy = false;
    if (result != ESP_OK) snprintf(error, sizeof(error), "%s (%s)",
        result == ESP_ERR_NOT_SUPPORTED ? "仅支持 FAT32，可在此格式化磁盘" : "存储操作失败，请重试或重新连接磁盘",
        esp_err_to_name(result));
    else error[0] = 0;
    xSemaphoreGive(lock);
}

static esp_err_t apply_action(const storage_message_t *message)
{
    esp_err_t result;
    if (message->action == CONNECT) {
        if (connected) return ESP_OK;
        result = router_disk_open(message->address);
        if (result != ESP_OK) return result;
        xSemaphoreTake(lock, portMAX_DELAY);
        connected = true; ++media;
        xSemaphoreGive(lock);
        unplugged = false;
        result = router_disk_mount();
    } else if (message->action == DISCONNECT) {
        result = router_smb_stop();
        if (result == ESP_ERR_TIMEOUT) return result;
        router_disk_close();
        router_smb_forget();
        xSemaphoreTake(lock, portMAX_DELAY);
        connected = false; ++media;
        xSemaphoreGive(lock);
        unplugged = false;
        return ESP_OK;
    } else if (message->action == CONFIGURE) {
        result = router_smb_stop();
        if (result != ESP_OK) return result;
        result = router_storage_config_save(&message->config);
        if (result == ESP_OK) {
            xSemaphoreTake(lock, portMAX_DELAY);
            config = message->config;
            xSemaphoreGive(lock);
        }
    } else {
        if (!connected || unplugged || message->media != media) return ESP_ERR_INVALID_STATE;
        result = router_smb_stop();
        if (result != ESP_OK) return result;
        result = message->action == MOUNT ? router_disk_mount() : router_disk_unmount();
        if (result == ESP_OK && message->action == FORMAT) result = router_disk_format();
    }
    if (router_disk_mounted() && config.enabled) {
        esp_err_t start = router_smb_start(&config);
        if (result == ESP_OK) result = start;
    }
    return result;
}

static void storage_worker(void *arg)
{
    (void)arg;
    for (;;) {
        storage_message_t message;
        if (unplugged) {
            message = (storage_message_t){.action = DISCONNECT};
            refresh_state(apply_action(&message));
            vTaskDelay(pdMS_TO_TICKS(10));
        } else if (xQueueReceive(queue, &message, pdMS_TO_TICKS(5000)) == pdTRUE) {
            if (message.action == DISCONNECT && !router_disk_is(message.device)) continue;
            xSemaphoreTake(lock, portMAX_DELAY);
            busy = true;
            xSemaphoreGive(lock);
            refresh_state(apply_action(&message));
            memset(&message, 0, sizeof(message));
        } else {
            uint64_t size, free_bytes;
            if (router_disk_space(&size, &free_bytes) == ESP_OK) {
                xSemaphoreTake(lock, portMAX_DELAY);
                total = size; available = free_bytes;
                xSemaphoreGive(lock);
            }
        }
    }
}

static void usb_events(void *arg)
{
    (void)arg;
    for (;;) {
        uint32_t flags;
        usb_host_lib_handle_events(portMAX_DELAY, &flags);
        if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) usb_host_device_free_all();
    }
}

static void storage_event(const msc_host_event_t *event, void *arg)
{
    (void)arg;
    storage_message_t message = {0};
    if (event->event == MSC_DEVICE_CONNECTED) {
        message.action = CONNECT;
        message.address = event->device.address;
    } else if (event->event == MSC_DEVICE_DISCONNECTED) {
        message.action = DISCONNECT;
        message.device = event->device.handle;
        if (router_disk_is(message.device)) unplugged = true;
    } else return;
    if (xQueueSend(queue, &message, 0) != pdTRUE) ESP_LOGW("storage", "USB event queue full; reconnect the disk");
}

esp_err_t router_storage_start(esp_netif_t *ap)
{
    lock = xSemaphoreCreateMutex();
    queue = xQueueCreate(16, sizeof(storage_message_t));
    if (!lock || !queue) return ESP_ERR_NO_MEM;
    esp_err_t result = router_storage_config_load(&config);
    if (result == ESP_OK) result = router_smb_init(ap);
    const usb_host_config_t host = {.intr_flags = ESP_INTR_FLAG_LEVEL1};
    if (result == ESP_OK) result = usb_host_install(&host);
    if (result == ESP_OK && xTaskCreate(usb_events, "usb_host", 4096, NULL, 4, NULL) != pdPASS) result = ESP_ERR_NO_MEM;
    if (result == ESP_OK && xTaskCreate(storage_worker, "storage", 8192, NULL, 3, NULL) != pdPASS) result = ESP_ERR_NO_MEM;
    const msc_host_driver_config_t msc = {.create_backround_task = true, .task_priority = 5,
        .stack_size = 4096, .core_id = tskNO_AFFINITY, .callback = storage_event};
    if (result == ESP_OK) result = msc_host_install(&msc);
    ready = result == ESP_OK;
    if (!ready) snprintf(error, sizeof(error), "存储初始化失败 (%s)", esp_err_to_name(result));
    return result;
}
