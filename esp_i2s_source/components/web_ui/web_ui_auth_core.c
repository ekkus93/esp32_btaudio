/* web_ui_auth_core — see web_ui_auth_core.h. */
#include "web_ui_auth_core.h"

#include <string.h>

void auth_hex_encode_lower(const uint8_t *src, size_t src_len,
                           char *dst, size_t dst_size)
{
    static const char HEX[] = "0123456789abcdef";
    if (!src || !dst || dst_size < (src_len * 2u) + 1u) {
        return;
    }
    for (size_t i = 0; i < src_len; ++i) {
        dst[i * 2u] = HEX[src[i] >> 4];
        dst[i * 2u + 1u] = HEX[src[i] & 0x0fu];
    }
    dst[src_len * 2u] = '\0';
}

bool auth_token_is_valid(const char *token)
{
    if (!token) {
        return false;
    }
    size_t len = strnlen(token, AUTH_TOKEN_HEX_LEN + 1u);
    if (len != AUTH_TOKEN_HEX_LEN) {
        return false;
    }
    for (size_t i = 0; i < AUTH_TOKEN_HEX_LEN; ++i) {
        char c = token[i];
        bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        if (!ok) {
            return false;
        }
    }
    return true;
}

bool auth_token_equal_exact(const char *candidate, const char *expected)
{
    if (!auth_token_is_valid(candidate) || !auth_token_is_valid(expected)) {
        return false;
    }
    unsigned diff = 0;
    for (size_t i = 0; i < AUTH_TOKEN_HEX_LEN; ++i) {
        diff |= (unsigned)((unsigned char)candidate[i] ^ (unsigned char)expected[i]);
    }
    return diff == 0u;
}

bool auth_header_extract_bearer(const char *header_value, size_t header_len,
                                char out_token[AUTH_TOKEN_BUF_LEN])
{
    if (!header_value || !out_token) {
        return false;
    }
    if (header_len != AUTH_HEADER_EXACT_LEN) {
        return false;
    }
    if (strncmp(header_value, AUTH_BEARER_PREFIX, AUTH_BEARER_PREFIX_LEN) != 0) {
        return false;
    }
    const char *token_part = header_value + AUTH_BEARER_PREFIX_LEN;
    if (!auth_token_is_valid(token_part)) {
        return false;
    }
    memcpy(out_token, token_part, AUTH_TOKEN_HEX_LEN);
    out_token[AUTH_TOKEN_HEX_LEN] = '\0';
    return true;
}
