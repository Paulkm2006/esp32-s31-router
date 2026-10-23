/* SPDX-License-Identifier: Apache-2.0 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../main/router_auth.h"
#include "../main/router_config.h"

#define CHECK(condition) do { if (!(condition)) { return __LINE__; } } while (0)
static int64_t now = 2000000;
static router_config_t config = {.admin_password = "Test-Admin-Password!"};

int64_t esp_timer_get_time(void) { return now; }
const router_config_t *router_config_get(void) { return &config; }

void esp_fill_random(void *buffer, size_t size)
{
    static uint8_t seed;
    for (size_t i = 0; i < size; ++i) {
        ((uint8_t *)buffer)[i] = ++seed;
    }
}

esp_err_t httpd_resp_set_hdr(httpd_req_t *req, const char *name, const char *value)
{
    if (strcmp(name, "Set-Cookie") != 0) {
        return ESP_FAIL;
    }
    req->set_cookie = value;
    return ESP_OK;
}

esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *req, const char *name, char *out, size_t size)
{
    if (strcmp(name, "Cookie") != 0 || !req->cookie || strlen(req->cookie) >= size) {
        return ESP_FAIL;
    }
    strcpy(out, req->cookie);
    return ESP_OK;
}

int main(void)
{
    httpd_req_t request = {0};
    CHECK(!router_auth_check(&request));
    CHECK(router_auth_login(&request, "wrong") == ESP_ERR_INVALID_ARG);
    CHECK(router_auth_login(&request, config.admin_password) == ESP_ERR_INVALID_STATE);
    now += 1000000;
    CHECK(router_auth_login(&request, config.admin_password) == ESP_OK);
    CHECK(strstr(request.set_cookie, "; HttpOnly; SameSite=Strict; Max-Age=1800") != NULL);
    char token[80], cookie[256];
    memcpy(token, request.set_cookie, 79);
    token[79] = '\0';
    snprintf(cookie, sizeof(cookie), "other=value; %s; last=value", token);
    request.cookie = cookie;
    CHECK(router_auth_check(&request));
    request.cookie = "router_session=short";
    CHECK(!router_auth_check(&request));
    snprintf(cookie, sizeof(cookie), "wrong_%s", token);
    request.cookie = cookie;
    CHECK(!router_auth_check(&request));
    request.cookie = token;
    CHECK(router_auth_check(&request));
    now += 1800LL * 1000000;
    CHECK(!router_auth_check(&request));
    CHECK(router_auth_login(&request, config.admin_password) == ESP_OK);
    CHECK(!router_auth_check(&request));
    memcpy(cookie, request.set_cookie, 79);
    cookie[79] = '\0';
    request.cookie = cookie;
    CHECK(router_auth_check(&request));
    router_auth_logout(&request);
    CHECK(!router_auth_check(&request));
    CHECK(strstr(request.set_cookie, "Max-Age=0") != NULL);
    return 0;
}
