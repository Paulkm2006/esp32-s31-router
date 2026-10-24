/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t encoded[256];
    size_t size;
    bool required;
} router_spnego_mechs_t;

bool router_spnego_mechlist(router_spnego_mechs_t *state, const uint8_t *token, size_t size);
int router_spnego_auth_result(const router_spnego_mechs_t *state, const uint8_t *token,
                             size_t size, const uint8_t key[16], bool authenticated, void **out);
