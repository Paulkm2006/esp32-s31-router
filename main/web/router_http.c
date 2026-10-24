/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "router_auth.h"
#include "config/router_config.h"
#include "router_http.h"
#include "system/router_log.h"
#include "network/router_status.h"
#include "storage/router_storage.h"

_Static_assert(CONFIG_CJSON_NESTING_LIMIT <= 8, "Set cJSON maximum nesting depth to 8 or less");

static esp_netif_t *s_ap;
static esp_timer_handle_t reboot_timer;
static bool rebooting;
static struct ifreq ap_interface;
extern const char page_start[] asm("_binary_router_web_html_start");
extern const char page_end[] asm("_binary_router_web_html_end");

static esp_err_t reply(httpd_req_t *req, const char *status, const char *body)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    return httpd_resp_sendstr(req, body);
}

static esp_err_t read_logs(httpd_req_t *req)
{
    uint64_t after = 0;
    if (httpd_req_get_url_query_len(req)) {
        char query[64], value[32], *end;
        if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
            httpd_query_key_value(query, "after", value, sizeof(value)) != ESP_OK ||
            !value[0] || strspn(value, "0123456789") != strlen(value)) {
            return reply(req, "400 Bad Request", "{\"error\":\"日志游标无效\"}");
        }
        errno = 0;
        after = strtoull(value, &end, 10);
        if (errno || *end || after > 9007199254740991ULL) {
            return reply(req, "400 Bad Request", "{\"error\":\"日志游标无效\"}");
        }
    }
    cJSON *json = router_log_json(after);
    char *body = json ? cJSON_PrintUnformatted(json) : NULL;
    esp_err_t result = reply(req, body ? "200 OK" : "503 Service Unavailable",
                             body ? body : "{\"error\":\"日志缓存不可用或内存不足\"}");
    free(body);
    cJSON_Delete(json);
    return result;
}

static bool is_route(const httpd_req_t *req, const char *path)
{
    size_t size = strlen(path);
    return strncmp(req->uri, path, size) == 0 && (req->uri[size] == '\0' || req->uri[size] == '?');
}

static bool valid_host(httpd_req_t *req)
{
    char host[32], address[32];
    esp_netif_ip_info_t ip;
    if (httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) != ESP_OK ||
        esp_netif_get_ip_info(s_ap, &ip) != ESP_OK) {
        return false;
    }
    snprintf(address, sizeof(address), IPSTR, IP2STR(&ip.ip));
    if (strcmp(host, address) == 0) {
        return true;
    }
    strlcat(address, ":80", sizeof(address));
    return strcmp(host, address) == 0;
}

static cJSON *read_json(httpd_req_t *req)
{
    char type[48], body[1025];
    if (httpd_req_get_hdr_value_str(req, "Content-Type", type, sizeof(type)) != ESP_OK ||
        (strcmp(type, "application/json") != 0 && strcmp(type, "application/json; charset=utf-8") != 0) ||
        req->content_len == 0 || req->content_len >= sizeof(body)) {
        return NULL;
    }
    size_t received = 0;
    while (received < req->content_len) {
        int count = httpd_req_recv(req, body + received, req->content_len - received);
        if (count <= 0) {
            return NULL;
        }
        received += count;
    }
    body[received] = '\0';
    if (memchr(body, '\0', received) || strstr(body, "\\u0000")) {
        return NULL;
    }
    cJSON *json = cJSON_ParseWithLengthOpts(body, received + 1, NULL, true);
    if (!cJSON_IsObject(json)) {
        cJSON_Delete(json);
        return NULL;
    }
    return json;
}

static bool copy_field(const cJSON *json, const char *name, char *out, size_t capacity, bool keep_empty)
{
    const cJSON *field = cJSON_GetObjectItemCaseSensitive(json, name);
    if (!cJSON_IsString(field) || strlen(field->valuestring) >= capacity) {
        return false;
    }
    if (!keep_empty || field->valuestring[0]) {
        strlcpy(out, field->valuestring, capacity);
    }
    return true;
}

static esp_err_t save_config(httpd_req_t *req, const cJSON *json)
{
    router_config_t config = *router_config_get();
    if (!copy_field(json, "ssid", config.ssid, sizeof(config.ssid), false) ||
        !copy_field(json, "password", config.password, sizeof(config.password), true) ||
        !copy_field(json, "gateway", config.gateway, sizeof(config.gateway), false) ||
        !copy_field(json, "netmask", config.netmask, sizeof(config.netmask), false) ||
        !copy_field(json, "dns", config.dns, sizeof(config.dns), false) ||
        !copy_field(json, "admin_password", config.admin_password, sizeof(config.admin_password), true) ||
        !router_config_valid(&config)) {
        return reply(req, "400 Bad Request", "{\"error\":\"配置无效，请检查名称、密码、私有网段、掩码和 DNS\"}");
    }
    if (router_storage_prepare_reboot() != ESP_OK) {
        router_storage_cancel_reboot();
        return reply(req, "409 Conflict", "{\"error\":\"请等待存储操作完成，或安全卸载磁盘后重试\"}");
    }
    if (router_config_save(&config) != ESP_OK) {
        router_storage_cancel_reboot();
        return reply(req, "500 Internal Server Error", "{\"error\":\"保存失败，原配置保持有效\"}");
    }
    rebooting = true;
    ESP_ERROR_CHECK(esp_timer_start_once(reboot_timer, 2000000));
    char response[96];
    snprintf(response, sizeof(response), "{\"rebooting\":true,\"gateway\":\"%s\"}", config.gateway);
    return reply(req, "200 OK", response);
}

static esp_err_t handle_request(httpd_req_t *req)
{
    if (!valid_host(req)) {
        return reply(req, "403 Forbidden", "{\"error\":\"请使用 AP 网关 IP 访问管理页面\"}");
    }
    if (req->method == HTTP_GET && is_route(req, "/")) {
        httpd_resp_set_type(req, "text/html; charset=utf-8");
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");
        httpd_resp_set_hdr(req, "Content-Security-Policy", "default-src 'none'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; connect-src 'self'; form-action 'none'; frame-ancestors 'none'; base-uri 'none'");
        return httpd_resp_send(req, page_start, page_end - page_start - 1);
    }
    bool login = is_route(req, "/api/login");
    if (!login && !router_auth_check(req)) {
        return reply(req, "401 Unauthorized", "{\"error\":\"请先登录或重新登录\"}");
    }
    if (rebooting) {
        return reply(req, "503 Service Unavailable", "{\"error\":\"配置已保存，设备正在重启\"}");
    }
    if (req->method == HTTP_GET) {
        if (is_route(req, "/api/logs")) return read_logs(req);
        cJSON *status = router_status_json();
        cJSON *storage = status ? router_storage_json() : NULL;
        if (!storage || !cJSON_AddItemToObject(status, "storage", storage)) {
            cJSON_Delete(storage);
            cJSON_Delete(status);
            status = NULL;
        }
        char *body = status ? cJSON_PrintUnformatted(status) : NULL;
        esp_err_t result = reply(req, body ? "200 OK" : "500 Internal Server Error",
                                 body ? body : "{\"error\":\"读取状态失败，请稍后重试\"}");
        free(body);
        cJSON_Delete(status);
        return result;
    }
    cJSON *json = read_json(req);
    if (!json) {
        httpd_resp_set_hdr(req, "Connection", "close");
        reply(req, "400 Bad Request", "{\"error\":\"请求必须是有效的 JSON（最多 1024 字节）\"}");
        return ESP_FAIL;
    }
    esp_err_t result;
    if (login) {
        const cJSON *password = cJSON_GetObjectItemCaseSensitive(json, "password");
        esp_err_t auth = cJSON_IsString(password) ? router_auth_login(req, password->valuestring) : ESP_ERR_INVALID_ARG;
        result = reply(req, auth == ESP_OK ? "200 OK" : auth == ESP_ERR_INVALID_STATE ? "429 Too Many Requests" : "401 Unauthorized",
                       auth == ESP_OK ? "{}" : "{\"error\":\"密码错误或尝试过快，请稍后重试\"}");
    } else if (is_route(req, "/api/logout")) {
        router_auth_logout(req);
        result = reply(req, "200 OK", "{}");
    } else if (is_route(req, "/api/storage")) {
        esp_err_t err = router_storage_request(json);
        result = reply(req, err == ESP_OK ? "202 Accepted" : err == ESP_ERR_INVALID_ARG ? "400 Bad Request" : "409 Conflict",
                       err == ESP_OK ? "{\"accepted\":true}" :
                       err == ESP_ERR_INVALID_ARG ? "{\"error\":\"存储参数无效；启用 SMB 需设置用户名和 8–63 字节密码，格式化需输入 FORMAT\"}" :
                       "{\"error\":\"存储正在操作、设备已改变或尚未就绪，请刷新后重试\"}");
    } else {
        result = save_config(req, json);
    }
    cJSON_Delete(json);
    return result;
}

static void reboot(void *arg)
{
    esp_restart();
}

esp_err_t router_http_start(esp_netif_t *ap)
{
    s_ap = ap;
    ESP_RETURN_ON_ERROR(esp_netif_get_netif_impl_name(ap, ap_interface.ifr_name), "router_http", "get AP interface");
    ESP_RETURN_ON_FALSE(ap_interface.ifr_name[0] != '\0', ESP_ERR_INVALID_STATE, "router_http", "AP is not started");
    const esp_timer_create_args_t timer = {.callback = reboot, .name = "config_reboot"};
    ESP_RETURN_ON_ERROR(esp_timer_create(&timer, &reboot_timer), "router_http", "create reboot timer");
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.if_name = &ap_interface;
    config.stack_size = 8192;
    config.lru_purge_enable = true;
    config.max_resp_headers = 10;
    config.max_uri_handlers = 8;
    httpd_handle_t server;
    ESP_RETURN_ON_ERROR(httpd_start(&server, &config), "router_http", "start AP management server");
    const httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = handle_request},
        {.uri = "/api/status", .method = HTTP_GET, .handler = handle_request},
        {.uri = "/api/logs", .method = HTTP_GET, .handler = handle_request},
        {.uri = "/api/login", .method = HTTP_POST, .handler = handle_request},
        {.uri = "/api/logout", .method = HTTP_POST, .handler = handle_request},
        {.uri = "/api/config", .method = HTTP_POST, .handler = handle_request},
        {.uri = "/api/storage", .method = HTTP_POST, .handler = handle_request},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &routes[i]), "router_http", "register management route");
    }
    return ESP_OK;
}
