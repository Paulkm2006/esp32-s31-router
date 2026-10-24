/* SPDX-License-Identifier: Apache-2.0 */
#include <string.h>
#include "nvs.h"
#include "../main/router_config.h"
#include "../main/router_storage_config.h"

#define CHECK(condition) do { if (!(condition)) { return __LINE__; } } while (0)

static unsigned char saved[512], staged[512];
static size_t saved_size, staged_size;
static esp_err_t commit_result;

esp_err_t nvs_open(const char *name, nvs_open_mode_t mode, nvs_handle_t *handle)
{
    (void)name;
    *handle = 1;
    return mode == NVS_READONLY && saved_size == 0 ? ESP_ERR_NVS_NOT_FOUND : ESP_OK;
}

void nvs_close(nvs_handle_t handle) { (void)handle; }

esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out, size_t *size)
{
    (void)handle;
    (void)key;
    if (*size < saved_size) {
        return ESP_ERR_NVS_INVALID_LENGTH;
    }
    memcpy(out, saved, saved_size);
    *size = saved_size;
    return ESP_OK;
}

esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t size)
{
    (void)handle;
    (void)key;
    memcpy(staged, value, size);
    staged_size = size;
    return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t handle)
{
    (void)handle;
    if (commit_result == ESP_OK) {
        memcpy(saved, staged, staged_size);
        saved_size = staged_size;
    }
    return commit_result;
}

int main(void)
{
    CHECK(router_config_load() == ESP_OK);
    router_config_t config = *router_config_get();
    CHECK(router_config_valid(&config));
    CHECK(!router_config_valid(NULL));
    const char *bad_ips[] = {"8.8.8.1", "192.168.4.0", "192.168.4.255", "10.0.0.256",
                             "10.0.0.01", "10.0.0", "10.0.0.1x", " 10.0.0.1"};
    for (unsigned i = 0; i < sizeof(bad_ips) / sizeof(bad_ips[0]); ++i) {
        strcpy(config.gateway, bad_ips[i]);
        CHECK(!router_config_valid(&config));
    }
    config = *router_config_get();
    strcpy(config.netmask, "255.0.255.0");
    CHECK(!router_config_valid(&config));
    strcpy(config.netmask, "255.255.255.248");
    CHECK(!router_config_valid(&config));
    strcpy(config.netmask, "255.0.0.0");
    CHECK(!router_config_valid(&config));
    strcpy(config.gateway, "10.0.0.1");
    CHECK(router_config_valid(&config));
    strcpy(config.netmask, "255.255.255.240");
    CHECK(router_config_valid(&config));
    const char *bad_dns[] = {"0.0.0.0", "0.1.2.3", "127.0.0.1", "224.0.0.1", "255.255.255.255", "1.1.1"};
    for (unsigned i = 0; i < sizeof(bad_dns) / sizeof(bad_dns[0]); ++i) {
        strcpy(config.dns, bad_dns[i]);
        CHECK(!router_config_valid(&config));
    }
    strcpy(config.dns, "9.9.9.9");
    CHECK(router_config_valid(&config));
    config.password[7] = '\0';
    CHECK(!router_config_valid(&config));
    config = *router_config_get();
    memset(config.ssid, 'x', sizeof(config.ssid));
    CHECK(!router_config_valid(&config));
    config = *router_config_get();
    strcpy(config.ssid, "Saved router");
    strcpy(config.password, "Saved-AP-Password!");
    strcpy(config.admin_password, "Saved-Admin-Password!");
    strcpy(config.gateway, "10.8.0.1");
    strcpy(config.netmask, "255.255.0.0");
    strcpy(config.dns, "9.9.9.9");
    CHECK(router_config_save(&config) == ESP_OK);
    CHECK(strcmp(router_config_get()->ssid, config.ssid) != 0);
    CHECK(router_config_load() == ESP_OK);
    CHECK(strcmp(router_config_get()->ssid, config.ssid) == 0);
    CHECK(memcmp(router_config_get(), &config, sizeof(config)) == 0);
    commit_result = ESP_FAIL;
    strcpy(config.ssid, "Failed write");
    CHECK(router_config_save(&config) == ESP_FAIL);
    CHECK(router_config_load() == ESP_OK);
    CHECK(strcmp(router_config_get()->ssid, "Saved router") == 0);
    memset(saved, 0, saved_size);
    CHECK(router_config_load() == ESP_OK);
    CHECK(router_config_valid(router_config_get()));
    CHECK(strcmp(router_config_get()->ssid, "Saved router") != 0);
    saved_size = 0;
    commit_result = ESP_OK;
    router_storage_config_t storage;
    CHECK(router_storage_config_load(&storage) == ESP_OK && !storage.enabled);
    storage.enabled = 2;
    CHECK(!router_storage_config_valid(&storage));
    storage.enabled = true;
    CHECK(!router_storage_config_valid(&storage));
    strcpy(storage.password, "Storage-secret!");
    CHECK(router_storage_config_valid(&storage));
    CHECK(router_storage_config_save(&storage) == ESP_OK);
    router_storage_config_t loaded;
    CHECK(router_storage_config_load(&loaded) == ESP_OK);
    CHECK(memcmp(&loaded, &storage, sizeof(loaded)) == 0);
    strcpy(storage.username, "../admin");
    CHECK(router_storage_config_save(&storage) == ESP_ERR_INVALID_ARG);
    storage = loaded;
    storage.password[0] = '\n';
    CHECK(!router_storage_config_valid(&storage));
    storage = loaded;
    memset(storage.password, 'x', sizeof(storage.password));
    CHECK(!router_storage_config_valid(&storage));
    storage = loaded;
    strcpy(storage.password, "New-password!");
    commit_result = ESP_FAIL;
    CHECK(router_storage_config_save(&storage) == ESP_FAIL);
    CHECK(router_storage_config_load(&storage) == ESP_OK);
    CHECK(strcmp(storage.password, loaded.password) == 0);
    return 0;
}
