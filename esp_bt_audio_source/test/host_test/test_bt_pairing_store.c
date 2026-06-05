/**
 * test_bt_pairing_store.c — unit tests for bt_pairing_store.c functions
 * not already covered by test_pairing_event_notifications.c or test_pairing_pending.c.
 *
 * Covered here:
 *   - bt_pairing_parse_mac()         various MAC string formats
 *   - bt_pairing_prepare_for_initiation()  clears + sets pending addr
 *   - bt_pairing_get_pending_request()     NULL-safety
 *   - bt_pairing_clear_pending()           called when nothing pending
 *   - bt_pairing_confirm() / bt_pairing_submit_pin()  return NOT_SUPPORTED on host
 */

#include <string.h>
#include "unity.h"
#include "bt_manager.h"
#include "bt_pairing_store.h"

/* Test hooks declared in bt_manager.c / bt_manager_test_hooks.c */
void bt_manager_test_reset_pending(void);
bool bt_manager_test_gap_pin_request(const char* mac);
bool bt_manager_test_gap_ssp_confirm(const char* mac, uint32_t passkey);

void setUp(void)
{
    bt_manager_test_reset_pending();
}

void tearDown(void)
{
    bt_manager_test_reset_pending();
}

/* ── bt_pairing_parse_mac ───────────────────────────────────────────────── */

void test_parse_mac_colon_separated(void)
{
    esp_bd_addr_t out = {0};
    bool ok = bt_pairing_parse_mac("AA:BB:CC:DD:EE:FF", out);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_HEX8(0xAA, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0xBB, out[1]);
    TEST_ASSERT_EQUAL_HEX8(0xCC, out[2]);
    TEST_ASSERT_EQUAL_HEX8(0xDD, out[3]);
    TEST_ASSERT_EQUAL_HEX8(0xEE, out[4]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, out[5]);
}

void test_parse_mac_lowercase(void)
{
    esp_bd_addr_t out = {0};
    bool ok = bt_pairing_parse_mac("aa:bb:cc:dd:ee:ff", out);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_HEX8(0xAA, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, out[5]);
}

void test_parse_mac_all_zeros(void)
{
    esp_bd_addr_t out = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    bool ok = bt_pairing_parse_mac("00:00:00:00:00:00", out);
    TEST_ASSERT_TRUE(ok);
    for (int i = 0; i < 6; i++) {
        TEST_ASSERT_EQUAL_HEX8(0x00, out[i]);
    }
}

void test_parse_mac_invalid_too_short(void)
{
    esp_bd_addr_t out = {0};
    bool ok = bt_pairing_parse_mac("AA:BB:CC", out);
    TEST_ASSERT_FALSE(ok);
}

void test_parse_mac_invalid_garbage(void)
{
    esp_bd_addr_t out = {0};
    bool ok = bt_pairing_parse_mac("not-a-mac", out);
    TEST_ASSERT_FALSE(ok);
}

void test_parse_mac_null_input(void)
{
    esp_bd_addr_t out = {0};
    bool ok = bt_pairing_parse_mac(NULL, out);
    TEST_ASSERT_FALSE(ok);
}

void test_parse_mac_empty_string(void)
{
    esp_bd_addr_t out = {0};
    bool ok = bt_pairing_parse_mac("", out);
    TEST_ASSERT_FALSE(ok);
}

/* ── bt_pairing_prepare_for_initiation ─────────────────────────────────── */

void test_prepare_for_initiation_sets_pending_addr(void)
{
    /* Set up an SSP pending first so we verify it gets cleared */
    bt_manager_test_gap_ssp_confirm("11:22:33:44:55:66", 123456);

    bt_pairing_request_info_t info;
    memset(&info, 0, sizeof(info));
    TEST_ASSERT_TRUE(bt_pairing_get_pending_request(&info));
    TEST_ASSERT_TRUE(info.ssp_confirm_pending);

    /* Now initiate a new pairing — should clear the old pending state */
    esp_bd_addr_t bda = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    bt_pairing_prepare_for_initiation(bda);

    /* After prepare_for_initiation, no PIN or SSP flags should be set
     * (the addr is set but the pending flags are cleared for initiation) */
    memset(&info, 0, sizeof(info));
    TEST_ASSERT_FALSE(bt_pairing_get_pending_request(&info));
}

void test_prepare_for_initiation_when_nothing_pending(void)
{
    /* Should not crash when called with nothing pending */
    esp_bd_addr_t bda = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    bt_pairing_prepare_for_initiation(bda);

    bt_pairing_request_info_t info;
    memset(&info, 0, sizeof(info));
    /* No pending flags set (prepare only sets addr, not flags) */
    TEST_ASSERT_FALSE(bt_pairing_get_pending_request(&info));
}

/* ── bt_pairing_get_pending_request NULL safety ─────────────────────────── */

void test_get_pending_request_null_param(void)
{
    /* Should return false without crashing when info is NULL */
    bool result = bt_pairing_get_pending_request(NULL);
    TEST_ASSERT_FALSE(result);
}

void test_get_pending_request_no_pending(void)
{
    bt_pairing_request_info_t info;
    memset(&info, 0xFF, sizeof(info));  /* fill with garbage to detect un-zeroed output */
    bool result = bt_pairing_get_pending_request(&info);
    TEST_ASSERT_FALSE(result);
    /* struct should be zeroed when nothing pending */
    TEST_ASSERT_FALSE(info.pin_request_pending);
    TEST_ASSERT_FALSE(info.ssp_confirm_pending);
}

/* ── bt_pairing_clear_pending ───────────────────────────────────────────── */

void test_clear_pending_when_nothing_pending(void)
{
    /* Should not crash when called with nothing pending */
    bt_pairing_clear_pending();

    bt_pairing_request_info_t info;
    memset(&info, 0, sizeof(info));
    TEST_ASSERT_FALSE(bt_pairing_get_pending_request(&info));
}

void test_clear_pending_clears_pin_state(void)
{
    bt_manager_test_gap_pin_request("AA:BB:CC:DD:EE:FF");

    bt_pairing_request_info_t info;
    memset(&info, 0, sizeof(info));
    TEST_ASSERT_TRUE(bt_pairing_get_pending_request(&info));
    TEST_ASSERT_TRUE(info.pin_request_pending);

    bt_pairing_clear_pending();

    memset(&info, 0, sizeof(info));
    TEST_ASSERT_FALSE(bt_pairing_get_pending_request(&info));
    TEST_ASSERT_FALSE(info.pin_request_pending);
    TEST_ASSERT_FALSE(info.ssp_confirm_pending);
}

void test_clear_pending_clears_ssp_state(void)
{
    bt_manager_test_gap_ssp_confirm("11:22:33:44:55:66", 999999);

    bt_pairing_request_info_t info;
    memset(&info, 0, sizeof(info));
    TEST_ASSERT_TRUE(bt_pairing_get_pending_request(&info));
    TEST_ASSERT_TRUE(info.ssp_confirm_pending);

    bt_pairing_clear_pending();

    memset(&info, 0, sizeof(info));
    TEST_ASSERT_FALSE(bt_pairing_get_pending_request(&info));
}

/* ── bt_pairing_confirm / bt_pairing_submit_pin host returns ────────────── */

void test_pairing_confirm_returns_not_supported_on_host(void)
{
    /* On non-ESP_PLATFORM builds these always return NOT_SUPPORTED */
    bt_err_t err = bt_pairing_confirm("AA:BB:CC:DD:EE:FF", true);
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED, err);
}

void test_pairing_submit_pin_returns_not_supported_on_host(void)
{
    bt_err_t err = bt_pairing_submit_pin("AA:BB:CC:DD:EE:FF", "1234");
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED, err);
}

void test_pairing_confirm_null_mac_returns_not_supported_on_host(void)
{
    bt_err_t err = bt_pairing_confirm(NULL, false);
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED, err);
}

void test_pairing_submit_pin_null_pin_returns_not_supported_on_host(void)
{
    bt_err_t err = bt_pairing_submit_pin(NULL, NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED, err);
}

/* ── device change during pending pairing ───────────────────────────────── */

void test_second_pin_request_replaces_first(void)
{
    /* PIN request for device A */
    bt_manager_test_gap_pin_request("11:22:33:44:55:66");

    bt_pairing_request_info_t info;
    memset(&info, 0, sizeof(info));
    TEST_ASSERT_TRUE(bt_pairing_get_pending_request(&info));
    TEST_ASSERT_EQUAL_STRING("11:22:33:44:55:66", info.mac);

    /* PIN request for device B — should replace A cleanly */
    bt_manager_test_gap_pin_request("AA:BB:CC:DD:EE:FF");

    memset(&info, 0, sizeof(info));
    TEST_ASSERT_TRUE(bt_pairing_get_pending_request(&info));
    TEST_ASSERT_TRUE(info.pin_request_pending);
    TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FF", info.mac);
}

int main(void)
{
    UNITY_BEGIN();

    /* bt_pairing_parse_mac */
    RUN_TEST(test_parse_mac_colon_separated);
    RUN_TEST(test_parse_mac_lowercase);
    RUN_TEST(test_parse_mac_all_zeros);
    RUN_TEST(test_parse_mac_invalid_too_short);
    RUN_TEST(test_parse_mac_invalid_garbage);
    RUN_TEST(test_parse_mac_null_input);
    RUN_TEST(test_parse_mac_empty_string);

    /* bt_pairing_prepare_for_initiation */
    RUN_TEST(test_prepare_for_initiation_sets_pending_addr);
    RUN_TEST(test_prepare_for_initiation_when_nothing_pending);

    /* bt_pairing_get_pending_request */
    RUN_TEST(test_get_pending_request_null_param);
    RUN_TEST(test_get_pending_request_no_pending);

    /* bt_pairing_clear_pending */
    RUN_TEST(test_clear_pending_when_nothing_pending);
    RUN_TEST(test_clear_pending_clears_pin_state);
    RUN_TEST(test_clear_pending_clears_ssp_state);

    /* host-path returns */
    RUN_TEST(test_pairing_confirm_returns_not_supported_on_host);
    RUN_TEST(test_pairing_submit_pin_returns_not_supported_on_host);
    RUN_TEST(test_pairing_confirm_null_mac_returns_not_supported_on_host);
    RUN_TEST(test_pairing_submit_pin_null_pin_returns_not_supported_on_host);

    /* device change */
    RUN_TEST(test_second_pin_request_replaces_first);

    return UNITY_END();
}
