/* SPDX-License-Identifier: Apache-2.0 */
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "router_log.h"

#define LOG_COUNT 2048
#define PAGE_COUNT 32
#define RETENTION_US (86400LL * 1000000)

typedef struct {
    int64_t timestamp;
    char text[1016];
} log_entry_t;
_Static_assert(sizeof(log_entry_t) == 1024, "Log cache must occupy 2 MiB");

static log_entry_t *entries;
static StaticSemaphore_t mutex_storage;
static SemaphoreHandle_t lock;
static uint64_t first, next;
static vprintf_like_t serial_print = vprintf;

static void expire(int64_t now)
{
    while (first < next && now - entries[first % LOG_COUNT].timestamp >= RETENTION_US) {
        ++first;
    }
}

static int capture(const char *format, va_list args)
{
    va_list copy;
    va_copy(copy, args);
    int result = serial_print(format, args);
    char text[sizeof(entries[0].text)];
    int length = vsnprintf(text, sizeof(text), format, copy);
    va_end(copy);
    if (length <= 0) return result;
    // shortcut: entries hold about 1 KiB each; enlarge slots if longer messages are needed.
    if ((size_t)length >= sizeof(text)) {
        const char marker[] = " [truncated]\n";
        size_t end = sizeof(text) - sizeof(marker);
        while (end && ((unsigned char)text[end] & 0xc0) == 0x80) --end;
        memcpy(text + end, marker, sizeof(marker));
        length = (int)(end + sizeof(marker) - 1);
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    int64_t now = esp_timer_get_time();
    expire(now);
    if (next - first == LOG_COUNT) ++first;
    log_entry_t *entry = &entries[next++ % LOG_COUNT];
    entry->timestamp = now;
    memcpy(entry->text, text, length + 1);
    xSemaphoreGive(lock);
    return result;
}

esp_err_t router_log_start(void)
{
    if (entries) return ESP_OK;
    entries = heap_caps_malloc(LOG_COUNT * sizeof(*entries), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!entries) return ESP_ERR_NO_MEM;
    lock = xSemaphoreCreateMutexStatic(&mutex_storage);
    serial_print = esp_log_set_vprintf(capture);
    return ESP_OK;
}

cJSON *router_log_json(uint64_t after)
{
    if (!entries) return NULL;
    cJSON *json = cJSON_CreateObject();
    cJSON *page = json ? cJSON_AddArrayToObject(json, "entries") : NULL;
    if (!page) {
        cJSON_Delete(json);
        return NULL;
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    expire(esp_timer_get_time());
    bool reset = after < first || after > next;
    uint64_t cursor = reset ? first : after;
    uint64_t end = next - cursor > PAGE_COUNT ? cursor + PAGE_COUNT : next;
    bool ok = cJSON_AddNumberToObject(json, "first", (double)first) &&
              cJSON_AddNumberToObject(json, "next", (double)end) &&
              cJSON_AddBoolToObject(json, "reset", reset) &&
              cJSON_AddBoolToObject(json, "more", end < next);
    while (ok && cursor < end) {
        cJSON *item = cJSON_CreateObject();
        if (!item || !cJSON_AddItemToArray(page, item)) {
            cJSON_Delete(item);
            ok = false;
            break;
        }
        ok = cJSON_AddNumberToObject(item, "id", (double)cursor) &&
             cJSON_AddStringToObject(item, "text", entries[cursor % LOG_COUNT].text);
        ++cursor;
    }
    xSemaphoreGive(lock);
    if (!ok) {
        cJSON_Delete(json);
        return NULL;
    }
    return json;
}
