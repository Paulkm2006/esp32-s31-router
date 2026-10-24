/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "esp_http_server.h"

esp_err_t router_auth_login(httpd_req_t *req, const char *password);
bool router_auth_check(httpd_req_t *req);
void router_auth_logout(httpd_req_t *req);
