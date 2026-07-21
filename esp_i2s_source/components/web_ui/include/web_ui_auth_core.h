/*
 * web_ui_auth_core — pure, host-testable bearer-token helpers (FIX3 §5).
 * No ESP-IDF dependencies: no NVS, no httpd, no RNG. web_ui_auth.c wraps
 * these with device glue (persistence, header retrieval, RNG).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AUTH_TOKEN_BYTES      32u
#define AUTH_TOKEN_HEX_LEN    (AUTH_TOKEN_BYTES * 2u)   /* 64 */
#define AUTH_TOKEN_BUF_LEN    (AUTH_TOKEN_HEX_LEN + 1u) /* 65, incl. NUL */

#define AUTH_BEARER_PREFIX     "Bearer "
#define AUTH_BEARER_PREFIX_LEN 7u
#define AUTH_HEADER_EXACT_LEN  (AUTH_BEARER_PREFIX_LEN + AUTH_TOKEN_HEX_LEN) /* 71 */

/* Encode src_len raw bytes as lowercase hex into dst (dst_size must be at
 * least src_len*2 + 1). No-op if dst_size is too small. */
void auth_hex_encode_lower(const uint8_t *src, size_t src_len,
                           char *dst, size_t dst_size);

/* True iff token is exactly AUTH_TOKEN_HEX_LEN lowercase hex characters,
 * NUL-terminated at that exact position (not shorter, not longer). */
bool auth_token_is_valid(const char *token);

/* Constant-time comparison of two validated tokens. Returns false (not
 * just "unequal") if either token fails auth_token_is_valid() first —
 * exact-length validation happens before any timing-sensitive compare. */
bool auth_token_equal_exact(const char *candidate, const char *expected);

/* Parse "Bearer <64-hex-token>" out of an already-fetched header value.
 * header_len must be the exact reported length of header_value (as from
 * httpd_req_get_hdr_value_len()) — this rejects undersized, oversized,
 * prefixed, suffixed, or whitespace-extended values by construction: any
 * deviation changes header_len away from AUTH_HEADER_EXACT_LEN. On success,
 * copies the 64-hex-char token (NUL-terminated) into out_token. */
bool auth_header_extract_bearer(const char *header_value, size_t header_len,
                                char out_token[AUTH_TOKEN_BUF_LEN]);

#ifdef __cplusplus
}
#endif
