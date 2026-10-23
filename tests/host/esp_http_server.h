/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

typedef struct {
    const char *cookie;
    const char *set_cookie;
} httpd_req_t;

esp_err_t httpd_resp_set_hdr(httpd_req_t *req, const char *name, const char *value);
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *req, const char *name, char *out, size_t size);
