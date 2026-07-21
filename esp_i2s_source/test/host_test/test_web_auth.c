/* test_web_auth — host tests for web_ui_auth_core.c (FIX3 Phase 2A).
 *
 * Only the pure encode/validate/compare/parse logic is host-tested here.
 * NVS persistence, esp_fill_random, and httpd header retrieval are device
 * glue in web_ui_auth.c (same split as station_store.c vs stations.c,
 * wifi_sm.c vs wifi_mgr.c elsewhere in this codebase) and are exercised on
 * hardware, not here.
 */
#include "unity.h"
#include "web_ui_auth_core.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ---- auth_hex_encode_lower ---- */

void test_hex_encode_known_input(void)
{
    const uint8_t src[4] = {0x00, 0xab, 0xcd, 0xff};
    char dst[9];
    auth_hex_encode_lower(src, sizeof(src), dst, sizeof(dst));
    TEST_ASSERT_EQUAL_STRING("00abcdff", dst);
}

void test_hex_encode_full_token_length(void)
{
    uint8_t src[AUTH_TOKEN_BYTES];
    for (size_t i = 0; i < sizeof(src); ++i) src[i] = (uint8_t)i;
    char dst[AUTH_TOKEN_BUF_LEN];
    auth_hex_encode_lower(src, sizeof(src), dst, sizeof(dst));
    TEST_ASSERT_EQUAL_UINT(AUTH_TOKEN_HEX_LEN, strlen(dst));
    TEST_ASSERT_TRUE(auth_token_is_valid(dst));
}

void test_hex_encode_refuses_undersized_dst(void)
{
    const uint8_t src[2] = {0xff, 0xff};
    char dst[3] = {'X', 'X', 'X'}; /* needs 5 (4 hex + NUL); too small */
    auth_hex_encode_lower(src, sizeof(src), dst, sizeof(dst));
    /* No-op on undersized dst — buffer must be untouched, not partially written. */
    TEST_ASSERT_EQUAL_CHAR('X', dst[0]);
}

/* ---- auth_token_is_valid ---- */

void test_token_valid_all_lowercase_hex(void)
{
    TEST_ASSERT_TRUE(auth_token_is_valid(
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));
}

void test_token_invalid_null(void)
{
    TEST_ASSERT_FALSE(auth_token_is_valid(NULL));
}

void test_token_invalid_empty(void)
{
    TEST_ASSERT_FALSE(auth_token_is_valid(""));
}

void test_token_invalid_63_chars(void)
{
    TEST_ASSERT_FALSE(auth_token_is_valid(
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcde"));
}

void test_token_invalid_65_chars(void)
{
    TEST_ASSERT_FALSE(auth_token_is_valid(
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdefe"));
}

void test_token_invalid_uppercase(void)
{
    TEST_ASSERT_FALSE(auth_token_is_valid(
        "0123456789ABCDEF0123456789abcdef0123456789abcdef0123456789abcdef"));
}

void test_token_invalid_non_hex_char(void)
{
    TEST_ASSERT_FALSE(auth_token_is_valid(
        "012345678gabcdef0123456789abcdef0123456789abcdef0123456789abcdef"));
}

/* ---- auth_token_equal_exact ---- */

void test_equal_exact_matching(void)
{
    const char *t = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    TEST_ASSERT_TRUE(auth_token_equal_exact(t, t));
}

void test_equal_exact_one_char_mismatch(void)
{
    const char *a = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    const char *b = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdee";
    TEST_ASSERT_FALSE(auth_token_equal_exact(a, b));
}

void test_equal_exact_rejects_invalid_candidate(void)
{
    const char *valid = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    TEST_ASSERT_FALSE(auth_token_equal_exact("short", valid));
}

void test_equal_exact_rejects_invalid_expected(void)
{
    const char *valid = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    TEST_ASSERT_FALSE(auth_token_equal_exact(valid, "short"));
}

void test_equal_exact_candidate_plus_suffix_fails(void)
{
    /* A 65-char candidate (valid token + one extra char) must fail
     * auth_token_is_valid() outright, so it can never equal a valid token. */
    const char *valid = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    char with_suffix[AUTH_TOKEN_BUF_LEN + 1];
    snprintf(with_suffix, sizeof(with_suffix), "%sX", valid);
    TEST_ASSERT_FALSE(auth_token_equal_exact(with_suffix, valid));
}

/* ---- auth_header_extract_bearer ---- */

#define VALID_TOKEN "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"

void test_extract_bearer_correct(void)
{
    const char *hdr = "Bearer " VALID_TOKEN;
    char out[AUTH_TOKEN_BUF_LEN];
    TEST_ASSERT_TRUE(auth_header_extract_bearer(hdr, strlen(hdr), out));
    TEST_ASSERT_EQUAL_STRING(VALID_TOKEN, out);
}

void test_extract_bearer_empty(void)
{
    char out[AUTH_TOKEN_BUF_LEN];
    TEST_ASSERT_FALSE(auth_header_extract_bearer("", 0, out));
}

void test_extract_bearer_wrong_length_reported(void)
{
    /* header_len deliberately doesn't match the real string length — this
     * is exactly the case where httpd reports a length that would come
     * from a suffixed/prefixed/whitespace-extended real header value. */
    const char *hdr = "Bearer " VALID_TOKEN "X";
    char out[AUTH_TOKEN_BUF_LEN];
    TEST_ASSERT_FALSE(auth_header_extract_bearer(hdr, strlen(hdr), out));
}

void test_extract_bearer_undersized_63_hex(void)
{
    const char *hdr = "Bearer 0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcde";
    char out[AUTH_TOKEN_BUF_LEN];
    TEST_ASSERT_FALSE(auth_header_extract_bearer(hdr, strlen(hdr), out));
}

void test_extract_bearer_missing_prefix(void)
{
    const char *hdr = VALID_TOKEN "1234567"; /* same total length, no "Bearer " */
    char out[AUTH_TOKEN_BUF_LEN];
    TEST_ASSERT_FALSE(auth_header_extract_bearer(hdr, strlen(hdr), out));
}

void test_extract_bearer_lowercase_prefix_rejected(void)
{
    const char *hdr = "bearer " VALID_TOKEN;
    char out[AUTH_TOKEN_BUF_LEN];
    TEST_ASSERT_FALSE(auth_header_extract_bearer(hdr, strlen(hdr), out));
}

void test_extract_bearer_extra_leading_space(void)
{
    const char *hdr = "Bearer  " VALID_TOKEN; /* two spaces after Bearer */
    char out[AUTH_TOKEN_BUF_LEN];
    /* Extra space shifts total length to 72, not 71 — must be rejected. */
    TEST_ASSERT_FALSE(auth_header_extract_bearer(hdr, strlen(hdr), out));
}

void test_extract_bearer_non_hex_token(void)
{
    const char *hdr = "Bearer 012345678gabcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    char out[AUTH_TOKEN_BUF_LEN];
    TEST_ASSERT_FALSE(auth_header_extract_bearer(hdr, strlen(hdr), out));
}

void test_extract_bearer_null_args(void)
{
    char out[AUTH_TOKEN_BUF_LEN];
    TEST_ASSERT_FALSE(auth_header_extract_bearer(NULL, 71, out));
    const char *hdr = "Bearer " VALID_TOKEN;
    TEST_ASSERT_FALSE(auth_header_extract_bearer(hdr, strlen(hdr), NULL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_hex_encode_known_input);
    RUN_TEST(test_hex_encode_full_token_length);
    RUN_TEST(test_hex_encode_refuses_undersized_dst);
    RUN_TEST(test_token_valid_all_lowercase_hex);
    RUN_TEST(test_token_invalid_null);
    RUN_TEST(test_token_invalid_empty);
    RUN_TEST(test_token_invalid_63_chars);
    RUN_TEST(test_token_invalid_65_chars);
    RUN_TEST(test_token_invalid_uppercase);
    RUN_TEST(test_token_invalid_non_hex_char);
    RUN_TEST(test_equal_exact_matching);
    RUN_TEST(test_equal_exact_one_char_mismatch);
    RUN_TEST(test_equal_exact_rejects_invalid_candidate);
    RUN_TEST(test_equal_exact_rejects_invalid_expected);
    RUN_TEST(test_equal_exact_candidate_plus_suffix_fails);
    RUN_TEST(test_extract_bearer_correct);
    RUN_TEST(test_extract_bearer_empty);
    RUN_TEST(test_extract_bearer_wrong_length_reported);
    RUN_TEST(test_extract_bearer_undersized_63_hex);
    RUN_TEST(test_extract_bearer_missing_prefix);
    RUN_TEST(test_extract_bearer_lowercase_prefix_rejected);
    RUN_TEST(test_extract_bearer_extra_leading_space);
    RUN_TEST(test_extract_bearer_non_hex_token);
    RUN_TEST(test_extract_bearer_null_args);
    return UNITY_END();
}
