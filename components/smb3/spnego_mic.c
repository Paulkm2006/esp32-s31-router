/* SPDX-License-Identifier: Apache-2.0 */
#include <stdlib.h>
#include <string.h>
#include "hmac-md5.h"
#include "md5.h"
#include "spnego_mic.h"

typedef struct {
    const uint8_t *data;
    size_t size;
} der_t;

static bool take(der_t *input, uint8_t tag, der_t *value)
{
    if (input->size < 2 || input->data[0] != tag) return false;
    size_t header = 2, size = input->data[1];
    if (size & 128) {
        size_t count = size & 127;
        if (!count || count > sizeof(size_t) || count > input->size - header) return false;
        size = 0;
        for (size_t i = 0; i < count; ++i) size = (size << 8) | input->data[header++];
    }
    if (size > input->size - header) return false;
    *value = (der_t){input->data + header, size};
    input->data += header + size;
    input->size -= header + size;
    return true;
}

bool router_spnego_mechlist(router_spnego_mechs_t *state, const uint8_t *token, size_t size)
{
    *state = (router_spnego_mechs_t){0};
    der_t outer = {token, size}, neg, oid, init, sequence, field, list;
    if (size && token[0] == 0x60) {
        if (!take(&outer, 0x60, &neg) || outer.size || !take(&neg, 0x06, &oid) ||
            !take(&neg, 0xa0, &init) || neg.size) return false;
    } else if (!take(&outer, 0xa0, &init) || outer.size) return false;
    if (!take(&init, 0x30, &sequence) || init.size || !take(&sequence, 0xa0, &field)) return false;
    const uint8_t *encoded = field.data;
    size_t bytes = field.size;
    if (!take(&field, 0x30, &list) || field.size || bytes > sizeof(state->encoded)) return false;
    static const uint8_t ntlm[] = {0x06, 0x0a, 0x2b, 0x06, 0x01, 0x04, 0x01, 0x82, 0x37, 0x02, 0x02, 0x0a};
    state->required = list.size < sizeof(ntlm) || memcmp(list.data, ntlm, sizeof(ntlm));
    memcpy(state->encoded, encoded, bytes);
    state->size = bytes;
    return true;
}

static bool find_mic(const uint8_t *token, size_t size, der_t *mic)
{
    der_t outer = {token, size}, response, sequence, field;
    if (!take(&outer, 0xa1, &response) || outer.size || !take(&response, 0x30, &sequence) || response.size) return false;
    unsigned seen = 0;
    *mic = (der_t){0};
    while (sequence.size) {
        uint8_t tag = sequence.data[0];
        if (tag < 0xa0 || tag > 0xa3 || (seen & (1U << (tag - 0xa0))) || !take(&sequence, tag, &field)) return false;
        seen |= 1U << (tag - 0xa0);
        if (tag == 0xa3 && (!take(&field, 0x04, mic) || field.size || mic->size != 16)) return false;
    }
    return true;
}

static void make_mic(const router_spnego_mechs_t *state, const uint8_t key[16],
                     bool server, uint8_t signature[16])
{
    // The pinned NTLM server never negotiates KEY_EXCH, so checksums do not use RC4.
    static const char client_magic[] = "session key to client-to-server signing key magic constant";
    static const char server_magic[] = "session key to server-to-client signing key magic constant";
    const char *magic = server ? server_magic : client_magic;
    struct MD5Context md5;
    uint8_t signing_key[16], digest[16], message[4 + sizeof(state->encoded)] = {0};
    MD5Init(&md5);
    MD5Update(&md5, key, 16);
    MD5Update(&md5, (const uint8_t *)magic, strlen(magic) + 1);
    MD5Final(signing_key, &md5);
    memcpy(message + 4, state->encoded, state->size);
    smb2_hmac_md5(message, state->size + 4, signing_key, sizeof(signing_key), digest);
    memset(signature, 0, 16);
    signature[0] = 1;
    memcpy(signature + 4, digest, 8);
}

int router_spnego_auth_result(const router_spnego_mechs_t *state, const uint8_t *token,
                             size_t size, const uint8_t key[16], bool authenticated, void **out)
{
    *out = NULL;
    der_t mic = {0};
    if (authenticated) {
        if (!find_mic(token, size, &mic) || (state->required && !mic.size)) return -1;
        if (mic.size) {
            if (!state->size || state->size > sizeof(state->encoded)) return -1;
            uint8_t expected[16];
            make_mic(state, key, false, expected);
            unsigned difference = 0;
            for (unsigned i = 0; i < sizeof(expected); ++i) difference |= expected[i] ^ mic.data[i];
            if (difference) return -1;
        }
    }
    size_t length = mic.size ? 29 : 9;
    uint8_t *result = malloc(length);
    if (!result) return -1;
    static const uint8_t prefix[] = {0xa1, 7, 0x30, 5, 0xa0, 3, 0x0a, 1, 0};
    memcpy(result, prefix, sizeof(prefix));
    result[8] = authenticated ? 0 : 2;
    if (mic.size) {
        result[1] = 27; result[3] = 25;
        result[9] = 0xa3; result[10] = 18; result[11] = 0x04; result[12] = 16;
        make_mic(state, key, true, result + 13);
    }
    *out = result;
    return length;
}
