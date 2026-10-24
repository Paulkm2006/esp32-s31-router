/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "spnego_mic.h"

void test_spnego_mic(void)
{
    uint8_t init[] = {0x60, 0x1c, 0x06, 0x06, 0x2b, 0x06, 0x01, 0x05, 0x05, 0x02,
        0xa0, 0x12, 0x30, 0x10, 0xa0, 0x0e, 0x30, 0x0c, 0x06, 0x0a,
        0x2b, 0x06, 0x01, 0x04, 0x01, 0x82, 0x37, 0x02, 0x02, 0x0a};
    uint8_t key[16];
    for (unsigned i = 0; i < sizeof(key); ++i) key[i] = i;
    // Independent Python hashlib/hmac vectors for the two NTLM signing directions.
    uint8_t token[] = {0xa1, 0x1b, 0x30, 0x19, 0xa0, 3, 0x0a, 1, 1, 0xa3, 0x12, 4, 0x10,
        1, 0, 0, 0, 0x49, 0xea, 0x44, 0x2b, 0xeb, 0x10, 0x41, 0x47, 0, 0, 0, 0};
    const uint8_t expected[] = {1, 0, 0, 0, 0x0a, 0xfb, 0x34, 0x90, 0x3a, 0x3e, 0x4e, 0x54, 0, 0, 0, 0};
    router_spnego_mechs_t state;
    for (size_t i = 0; i < sizeof(init); ++i) assert(!router_spnego_mechlist(&state, init, i));
    assert(router_spnego_mechlist(&state, init, sizeof(init)) && !state.required && state.size == 14);
    void *result = NULL;
    assert(router_spnego_auth_result(&state, token, sizeof(token), key, true, &result) == 29);
    assert(!memcmp((uint8_t *)result + 13, expected, sizeof(expected)) && ((uint8_t *)result)[8] == 0);
    free(result);
    for (size_t i = 0; i < sizeof(token); ++i)
        assert(router_spnego_auth_result(&state, token, i, key, true, &result) < 0 && !result);
    token[28] ^= 1;
    assert(router_spnego_auth_result(&state, token, sizeof(token), key, true, &result) < 0 && !result);
    token[28] ^= 1;
    const uint8_t absent[] = {0xa1, 7, 0x30, 5, 0xa0, 3, 0x0a, 1, 1};
    assert(router_spnego_auth_result(&state, absent, sizeof(absent), key, true, &result) == 9);
    free(result);
    state.required = true;
    assert(router_spnego_auth_result(&state, absent, sizeof(absent), key, true, &result) < 0 && !result);
    assert(router_spnego_auth_result(&state, NULL, 0, key, false, &result) == 9);
    assert(((uint8_t *)result)[8] == 2);
    free(result);
    init[1] = 0x80;
    assert(!router_spnego_mechlist(&state, init, sizeof(init)));
}
