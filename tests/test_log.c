/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "../main/router_log.h"

static int64_t now;
static bool no_psram = true;
static char serial[4096];
static vprintf_like_t printer;

int64_t esp_timer_get_time(void) { return now; }
void *heap_caps_malloc(size_t size, unsigned caps)
{
    assert(size == 2 * 1024 * 1024 && caps == 3);
    return no_psram ? NULL : malloc(size);
}
static int serial_print(const char *format, va_list args)
{
    return vsnprintf(serial, sizeof(serial), format, args);
}
vprintf_like_t esp_log_set_vprintf(vprintf_like_t function)
{
    assert(!printer);
    printer = function;
    return serial_print;
}
static int emit(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int result = printer(format, args);
    va_end(args);
    return result;
}
static double number(const cJSON *json, const char *name)
{
    return cJSON_GetObjectItemCaseSensitive(json, name)->valuedouble;
}
static const char *text(const cJSON *json, int index)
{
    return cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(
        cJSON_GetObjectItemCaseSensitive(json, "entries"), index), "text")->valuestring;
}
static int count(const cJSON *json)
{
    return cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(json, "entries"));
}
static int allocations_left, live_allocations;
static void *limited_memory(size_t size)
{
    if (allocations_left-- == 0) return NULL;
    void *memory = malloc(size);
    if (memory) ++live_allocations;
    return memory;
}
static void release_memory(void *memory)
{
    if (memory) --live_allocations;
    free(memory);
}

int main(void)
{
    assert(router_log_start() == ESP_ERR_NO_MEM);
    assert(!printer && !router_log_json(0));
    no_psram = false;
    assert(router_log_start() == ESP_OK && router_log_start() == ESP_OK);
    assert(emit("%s %d\n", "hello", 42) == 9);
    assert(strcmp(serial, "hello 42\n") == 0);
    now = 1000000;
    emit("second\n");
    cJSON *json = router_log_json(0);
    assert(count(json) == 2 && number(json, "next") == 2);
    assert(strcmp(text(json, 0), "hello 42\n") == 0);
    cJSON_Delete(json);
    now = 86400LL * 1000000 - 1;
    json = router_log_json(0);
    assert(count(json) == 2 && number(json, "first") == 0);
    cJSON_Delete(json);
    ++now;
    json = router_log_json(0);
    assert(count(json) == 1 && number(json, "first") == 1);
    assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(json, "reset")));
    cJSON_Delete(json);
    now += 1000000;
    json = router_log_json(2);
    assert(count(json) == 0 && number(json, "first") == 2);
    cJSON_Delete(json);

    for (int i = 0; i < 2049; ++i) emit("%d\n", i);
    uint64_t cursor = 0;
    int total = 0;
    bool more;
    do {
        json = router_log_json(cursor);
        assert(count(json) <= 32 && number(json, "first") == 3);
        if (!cursor) assert(strcmp(text(json, 0), "1\n") == 0);
        cursor = (uint64_t)number(json, "next");
        total += count(json);
        more = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(json, "more"));
        cJSON_Delete(json);
    } while (more);
    assert(total == 2048 && cursor == 2051);
    json = router_log_json(UINT64_MAX);
    assert(count(json) == 32 && number(json, "next") == 35);
    cJSON_Delete(json);

    char long_text[2101];
    for (size_t i = 0; i < 2100; i += 3) memcpy(long_text + i, "\xe6\xb1\x89", 3);
    long_text[2100] = 0;
    assert(emit("%s", long_text) == 2100 && strcmp(serial, long_text) == 0);
    json = router_log_json(cursor);
    const char *message = text(json, 0), *marker = strstr(message, " [truncated]\n");
    assert(marker && (marker - message) % 3 == 0 && strlen(message) < 1016);
    cJSON_Delete(json);
    cJSON_Hooks hooks = {.malloc_fn = limited_memory, .free_fn = release_memory};
    cJSON_InitHooks(&hooks);
    for (int i = 0; i < 20; ++i) {
        allocations_left = i;
        json = router_log_json(cursor);
        if (!i) assert(!json);
        cJSON_Delete(json);
        assert(live_allocations == 0);
    }
    cJSON_InitHooks(NULL);
    json = router_log_json(cursor);
    assert(count(json) == 1);
    cJSON_Delete(json);
    puts("Log checks passed: serial copy, 24-hour expiry, overflow, pagination and truncation.");
    return 0;
}
