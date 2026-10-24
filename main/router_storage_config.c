/* SPDX-License-Identifier: Apache-2.0 */
#include <stdint.h>
#include <string.h>
#include "nvs.h"
#include "router_storage_config.h"

bool router_storage_config_valid(const router_storage_config_t *config)
{
    if (!config || !memchr(config->username, 0, sizeof(config->username)) ||
        !memchr(config->password, 0, sizeof(config->password))) {
        return false;
    }
    for (const char *p = config->username; *p; ++p) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') || *p == '_' || *p == '-' || *p == '.')) {
            return false;
        }
    }
    size_t size = strlen(config->password);
    for (size_t i = 0; i < size; ++i) {
        if ((unsigned char)config->password[i] < 32 || (unsigned char)config->password[i] == 127) {
            return false;
        }
    }
    return (!size || size >= 8) && (!config->enabled || (config->username[0] && size >= 8));
}

typedef struct {
    uint32_t version;
    router_storage_config_t config;
} stored_storage_t;

esp_err_t router_storage_config_load(router_storage_config_t *config)
{
    *config = (router_storage_config_t){.username = "router"};
    nvs_handle_t handle;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }
    stored_storage_t stored = {0};
    size_t size = sizeof(stored);
    err = nvs_get_blob(handle, "config", &stored, &size);
    nvs_close(handle);
    if (err == ESP_OK && size == sizeof(stored) && stored.version == 1 &&
        router_storage_config_valid(&stored.config)) {
        *config = stored.config;
    }
    return err == ESP_ERR_NVS_NOT_FOUND || err == ESP_ERR_NVS_INVALID_LENGTH ? ESP_OK : err;
}

esp_err_t router_storage_config_save(const router_storage_config_t *config)
{
    if (!router_storage_config_valid(config)) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    const stored_storage_t stored = {.version = 1, .config = *config};
    err = nvs_set_blob(handle, "config", &stored, sizeof(stored));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}
