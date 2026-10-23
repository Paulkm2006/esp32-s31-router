/* SPDX-License-Identifier: Apache-2.0 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "esp_random.h"
#include "esp_timer.h"
#include "router_auth.h"
#include "router_config.h"

static char session[65];
static char login_cookie[160];
static int64_t expires_at;
static int64_t next_attempt;

static bool equal_secret(const char *a, const char *b, size_t size)
{
    unsigned difference = 0;
    for (size_t i = 0; i < size; ++i) {
        difference |= (unsigned char)a[i] ^ (unsigned char)b[i];
    }
    return difference == 0;
}

esp_err_t router_auth_login(httpd_req_t *req, const char *password)
{
    int64_t now = esp_timer_get_time();
    if (now < next_attempt) {
        return ESP_ERR_INVALID_STATE;
    }
    next_attempt = now + 1000000;
    char supplied[64] = {0}, expected[64] = {0};
    if (strlen(password) >= sizeof(supplied)) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(supplied, password, strlen(password));
    const char *configured = router_config_get()->admin_password;
    memcpy(expected, configured, strlen(configured));
    if (!equal_secret(supplied, expected, sizeof(supplied))) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t random[32];
    esp_fill_random(random, sizeof(random));
    for (size_t i = 0; i < sizeof(random); ++i) {
        snprintf(session + i * 2, 3, "%02x", random[i]);
    }
    expires_at = now + 1800LL * 1000000;
    snprintf(login_cookie, sizeof(login_cookie), "router_session=%s; Path=/; HttpOnly; SameSite=Strict; Max-Age=1800", session);
    return httpd_resp_set_hdr(req, "Set-Cookie", login_cookie);
}

bool router_auth_check(httpd_req_t *req)
{
    if (!session[0] || esp_timer_get_time() >= expires_at) {
        return false;
    }
    char cookie[256];
    if (httpd_req_get_hdr_value_str(req, "Cookie", cookie, sizeof(cookie)) != ESP_OK) {
        return false;
    }
    char *save = NULL;
    for (char *item = strtok_r(cookie, ";", &save); item; item = strtok_r(NULL, ";", &save)) {
        while (*item == ' ') {
            ++item;
        }
        if (strncmp(item, "router_session=", 15) == 0 && strlen(item + 15) == 64 &&
            equal_secret(item + 15, session, 64)) {
            return true;
        }
    }
    return false;
}

void router_auth_logout(httpd_req_t *req)
{
    memset(session, 0, sizeof(session));
    expires_at = 0;
    httpd_resp_set_hdr(req, "Set-Cookie", "router_session=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0");
}
