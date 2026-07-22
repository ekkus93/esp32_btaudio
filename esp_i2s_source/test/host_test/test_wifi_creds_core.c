/*
 * test_wifi_creds_core — FIX3 Phase 6: pure Wi-Fi credential validation and
 * NVS-string-exactness logic (6.1/6.2/6.11).
 *
 * Built as two targets from the same source (see CMakeLists.txt), matching
 * the url_policy precedent for a compile-time Kconfig branch:
 *   test_wifi_creds_core                     — default (no hex PSK)
 *   test_wifi_creds_core_hex_psk_allowed      — CONFIG_ESP_I2S_SOURCE_STA_HEX_PSK defined
 * RUN_HEX_PSK_ALLOWED_VARIANT selects which set of expectations main() runs.
 */
#include "unity.h"
#include "wifi_creds_core.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static char *repeat(char *buf, char c, size_t n)
{
    memset(buf, (unsigned char)c, n);
    buf[n] = '\0';
    return buf;
}

/* ---- 6.1: bounded_length ---- */

void test_bounded_length_null_value_rejected(void)
{
    size_t len;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, wifi_creds_bounded_length(NULL, 32, true, &len));
}

void test_bounded_length_null_out_rejected(void)
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, wifi_creds_bounded_length("x", 32, true, NULL));
}

void test_bounded_length_exact_max_accepted(void)
{
    char buf[65];
    size_t len = 0;
    TEST_ASSERT_EQUAL(ESP_OK, wifi_creds_bounded_length(repeat(buf, 'a', 32), 32, false, &len));
    TEST_ASSERT_EQUAL_UINT(32, len);
}

void test_bounded_length_one_over_max_rejected(void)
{
    char buf[65];
    size_t len = 0;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE, wifi_creds_bounded_length(repeat(buf, 'a', 33), 32, false, &len));
}

void test_bounded_length_empty_rejected_when_disallowed(void)
{
    size_t len;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE, wifi_creds_bounded_length("", 32, false, &len));
}

void test_bounded_length_empty_accepted_when_allowed(void)
{
    size_t len = 99;
    TEST_ASSERT_EQUAL(ESP_OK, wifi_creds_bounded_length("", 32, true, &len));
    TEST_ASSERT_EQUAL_UINT(0, len);
}

/* ---- 6.11: SSID boundaries ---- */

void test_ssid_exact_32_bytes_accepted(void)
{
    char buf[64];
    size_t len = 0;
    TEST_ASSERT_EQUAL(ESP_OK, wifi_creds_validate_ssid(repeat(buf, 'a', 32), &len));
    TEST_ASSERT_EQUAL_UINT(32, len);
}

void test_ssid_33_bytes_rejected(void)
{
    char buf[64];
    size_t len = 0;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE, wifi_creds_validate_ssid(repeat(buf, 'a', 33), &len));
}

void test_ssid_empty_rejected(void)
{
    size_t len;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE, wifi_creds_validate_ssid("", &len));
}

void test_ssid_one_byte_accepted(void)
{
    size_t len = 0;
    TEST_ASSERT_EQUAL(ESP_OK, wifi_creds_validate_ssid("a", &len));
    TEST_ASSERT_EQUAL_UINT(1, len);
}

/* ---- 6.11: STA password boundaries ---- */

void test_sta_pass_open_network_accepted(void)
{
    size_t len = 99;
    TEST_ASSERT_EQUAL(ESP_OK, wifi_creds_validate_sta_password("", &len));
    TEST_ASSERT_EQUAL_UINT(0, len);
}

void test_sta_pass_7_bytes_rejected(void)
{
    char buf[16];
    size_t len;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, wifi_creds_validate_sta_password(repeat(buf, 'a', 7), &len));
}

void test_sta_pass_8_bytes_accepted(void)
{
    char buf[16];
    size_t len = 0;
    TEST_ASSERT_EQUAL(ESP_OK, wifi_creds_validate_sta_password(repeat(buf, 'a', 8), &len));
    TEST_ASSERT_EQUAL_UINT(8, len);
}

void test_sta_pass_63_bytes_accepted(void)
{
    char buf[64];
    size_t len = 0;
    TEST_ASSERT_EQUAL(ESP_OK, wifi_creds_validate_sta_password(repeat(buf, 'a', 63), &len));
    TEST_ASSERT_EQUAL_UINT(63, len);
}

void test_sta_pass_65_bytes_rejected(void)
{
    char buf[80];
    size_t len;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE, wifi_creds_validate_sta_password(repeat(buf, 'a', 65), &len));
}

void test_sta_pass_null_rejected(void)
{
    size_t len;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, wifi_creds_validate_sta_password(NULL, &len));
}

#ifdef RUN_HEX_PSK_ALLOWED_VARIANT
void test_sta_pass_64_hex_accepted_when_enabled(void)
{
    char buf[65];
    size_t len = 0;
    TEST_ASSERT_EQUAL(ESP_OK, wifi_creds_validate_sta_password(
        repeat(buf, 'a', 64), &len));
    TEST_ASSERT_EQUAL_UINT(64, len);
}

void test_sta_pass_64_hex_mixed_case_accepted(void)
{
    size_t len = 0;
    TEST_ASSERT_EQUAL(ESP_OK, wifi_creds_validate_sta_password(
        "0123456789abcdefABCDEF0123456789abcdefABCDEF0123456789abcdefABCD", &len));
    TEST_ASSERT_EQUAL_UINT(64, len);
}

void test_sta_pass_64_non_hex_rejected(void)
{
    char buf[65];
    size_t len;
    repeat(buf, 'a', 64);
    buf[10] = 'z';   /* not a hex digit */
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, wifi_creds_validate_sta_password(buf, &len));
}
#else
void test_sta_pass_64_chars_rejected_when_disabled(void)
{
    char buf[65];
    size_t len;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, wifi_creds_validate_sta_password(repeat(buf, 'a', 64), &len));
}
#endif

/* ---- 6.11: AP password boundaries ---- */

void test_ap_pass_open_accepted(void)
{
    size_t len = 99;
    TEST_ASSERT_EQUAL(ESP_OK, wifi_creds_validate_ap_password("", &len));
    TEST_ASSERT_EQUAL_UINT(0, len);
}

void test_ap_pass_null_treated_as_open(void)
{
    size_t len = 99;
    TEST_ASSERT_EQUAL(ESP_OK, wifi_creds_validate_ap_password(NULL, &len));
    TEST_ASSERT_EQUAL_UINT(0, len);
}

void test_ap_pass_7_bytes_rejected(void)
{
    char buf[16];
    size_t len;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, wifi_creds_validate_ap_password(repeat(buf, 'a', 7), &len));
}

void test_ap_pass_63_bytes_accepted(void)
{
    char buf[64];
    size_t len = 0;
    TEST_ASSERT_EQUAL(ESP_OK, wifi_creds_validate_ap_password(repeat(buf, 'a', 63), &len));
    TEST_ASSERT_EQUAL_UINT(63, len);
}

void test_ap_pass_64_bytes_rejected(void)
{
    /* No hex-PSK form for the control AP — 64 exceeds the 63-byte WPA2 cap. */
    char buf[80];
    size_t len;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE, wifi_creds_validate_ap_password(repeat(buf, 'a', 64), &len));
}

/* ---- 6.2/6.11: NVS-returned string exactness ---- */

void test_stored_string_includes_terminator_payload_len_subtracts_one(void)
{
    char dst[16] = "home";  /* "home\0" -> stored_len 5 */
    size_t payload = 0;
    TEST_ASSERT_EQUAL(ESP_OK, wifi_creds_validate_stored_string(dst, 5, sizeof(dst), 32, &payload));
    TEST_ASSERT_EQUAL_UINT(4, payload);
}

void test_stored_string_zero_stored_len_rejected(void)
{
    char dst[16] = {0};
    size_t payload;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE, wifi_creds_validate_stored_string(dst, 0, sizeof(dst), 32, &payload));
}

void test_stored_string_stored_len_exceeds_capacity_rejected(void)
{
    char dst[16] = {0};
    size_t payload;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE, wifi_creds_validate_stored_string(dst, 17, sizeof(dst), 32, &payload));
}

void test_stored_string_max_nvs_string_does_not_overflow(void)
{
    /* dst_capacity == max_payload+1 exactly, stored_len == dst_capacity
     * (the terminator fills the very last byte): must be accepted, and the
     * payload length must be dst_capacity-1, never read past dst[]. */
    char dst[33];
    memset(dst, 'a', 32);
    dst[32] = '\0';
    size_t payload = 0;
    TEST_ASSERT_EQUAL(ESP_OK, wifi_creds_validate_stored_string(dst, 33, sizeof(dst), 32, &payload));
    TEST_ASSERT_EQUAL_UINT(32, payload);
}

void test_stored_string_missing_terminator_rejected(void)
{
    char dst[16] = {0};
    memset(dst, 'a', 5);   /* no NUL within the claimed stored_len */
    size_t payload;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_CRC, wifi_creds_validate_stored_string(dst, 5, sizeof(dst), 32, &payload));
}

void test_stored_string_payload_exceeds_max_payload_rejected(void)
{
    char dst[40];
    memset(dst, 'a', 33);
    dst[33] = '\0';   /* payload_len 33 > max_payload 32 */
    size_t payload;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE, wifi_creds_validate_stored_string(dst, 34, sizeof(dst), 32, &payload));
}

void test_stored_string_null_args_rejected(void)
{
    char dst[16];
    size_t payload;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, wifi_creds_validate_stored_string(NULL, 5, sizeof(dst), 32, &payload));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, wifi_creds_validate_stored_string(dst, 5, sizeof(dst), 32, NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, wifi_creds_validate_stored_string(dst, 5, 0, 32, &payload));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_bounded_length_null_value_rejected);
    RUN_TEST(test_bounded_length_null_out_rejected);
    RUN_TEST(test_bounded_length_exact_max_accepted);
    RUN_TEST(test_bounded_length_one_over_max_rejected);
    RUN_TEST(test_bounded_length_empty_rejected_when_disallowed);
    RUN_TEST(test_bounded_length_empty_accepted_when_allowed);

    RUN_TEST(test_ssid_exact_32_bytes_accepted);
    RUN_TEST(test_ssid_33_bytes_rejected);
    RUN_TEST(test_ssid_empty_rejected);
    RUN_TEST(test_ssid_one_byte_accepted);

    RUN_TEST(test_sta_pass_open_network_accepted);
    RUN_TEST(test_sta_pass_7_bytes_rejected);
    RUN_TEST(test_sta_pass_8_bytes_accepted);
    RUN_TEST(test_sta_pass_63_bytes_accepted);
    RUN_TEST(test_sta_pass_65_bytes_rejected);
    RUN_TEST(test_sta_pass_null_rejected);
#ifdef RUN_HEX_PSK_ALLOWED_VARIANT
    RUN_TEST(test_sta_pass_64_hex_accepted_when_enabled);
    RUN_TEST(test_sta_pass_64_hex_mixed_case_accepted);
    RUN_TEST(test_sta_pass_64_non_hex_rejected);
#else
    RUN_TEST(test_sta_pass_64_chars_rejected_when_disabled);
#endif

    RUN_TEST(test_ap_pass_open_accepted);
    RUN_TEST(test_ap_pass_null_treated_as_open);
    RUN_TEST(test_ap_pass_7_bytes_rejected);
    RUN_TEST(test_ap_pass_63_bytes_accepted);
    RUN_TEST(test_ap_pass_64_bytes_rejected);

    RUN_TEST(test_stored_string_includes_terminator_payload_len_subtracts_one);
    RUN_TEST(test_stored_string_zero_stored_len_rejected);
    RUN_TEST(test_stored_string_stored_len_exceeds_capacity_rejected);
    RUN_TEST(test_stored_string_max_nvs_string_does_not_overflow);
    RUN_TEST(test_stored_string_missing_terminator_rejected);
    RUN_TEST(test_stored_string_payload_exceeds_max_payload_rejected);
    RUN_TEST(test_stored_string_null_args_rejected);

    return UNITY_END();
}
