#include <string.h>
#include "unity.h"
#include "bt_manager.h"

void setUp(void)
{
    bt_manager_test_reset_pending();
}

void tearDown(void)
{
    bt_manager_test_reset_pending();
}

static void assert_no_pending(void)
{
    bt_pairing_request_info_t info;
    memset(&info, 0, sizeof(info));
    TEST_ASSERT_FALSE(bt_pairing_get_pending_request(&info));
    TEST_ASSERT_EQUAL_UINT32(0, info.passkey);
    TEST_ASSERT_EQUAL_CHAR('\0', info.mac[0]);
    TEST_ASSERT_FALSE(info.pin_request_pending);
    TEST_ASSERT_FALSE(info.ssp_confirm_pending);
}

void test_pin_request_sets_pending_flags(void)
{
    TEST_ASSERT_TRUE(bt_manager_test_gap_pin_request("11:22:33:44:55:66"));

    bt_pairing_request_info_t info;
    memset(&info, 0, sizeof(info));
    TEST_ASSERT_TRUE(bt_pairing_get_pending_request(&info));
    TEST_ASSERT_TRUE(info.pin_request_pending);
    TEST_ASSERT_FALSE(info.ssp_confirm_pending);
    TEST_ASSERT_EQUAL_STRING("11:22:33:44:55:66", info.mac);
    TEST_ASSERT_EQUAL_UINT32(0, info.passkey);
}

void test_ssp_request_records_passkey(void)
{
    const uint32_t passkey = 123456;
    TEST_ASSERT_TRUE(bt_manager_test_gap_ssp_confirm("AA:BB:CC:DD:EE:FF", passkey));

    bt_pairing_request_info_t info;
    memset(&info, 0, sizeof(info));
    TEST_ASSERT_TRUE(bt_pairing_get_pending_request(&info));
    TEST_ASSERT_FALSE(info.pin_request_pending);
    TEST_ASSERT_TRUE(info.ssp_confirm_pending);
    TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FF", info.mac);
    TEST_ASSERT_EQUAL_UINT32(passkey, info.passkey);
}

void test_auth_complete_clears_pending(void)
{
    TEST_ASSERT_TRUE(bt_manager_test_gap_ssp_confirm("01:23:45:67:89:AB", 654321));

    bt_pairing_request_info_t info;
    memset(&info, 0, sizeof(info));
    TEST_ASSERT_TRUE(bt_pairing_get_pending_request(&info));

    bt_manager_test_gap_auth_complete("01:23:45:67:89:AB", true);

    assert_no_pending();
}

void test_new_event_replaces_previous_target(void)
{
    TEST_ASSERT_TRUE(bt_manager_test_gap_pin_request("10:20:30:40:50:60"));

    bt_pairing_request_info_t info;
    memset(&info, 0, sizeof(info));
    TEST_ASSERT_TRUE(bt_pairing_get_pending_request(&info));
    TEST_ASSERT_EQUAL_STRING("10:20:30:40:50:60", info.mac);

    TEST_ASSERT_TRUE(bt_manager_test_gap_pin_request("AB:CD:EF:01:02:03"));

    memset(&info, 0, sizeof(info));
    TEST_ASSERT_TRUE(bt_pairing_get_pending_request(&info));
    TEST_ASSERT_EQUAL_STRING("AB:CD:EF:01:02:03", info.mac);
    TEST_ASSERT_TRUE(info.pin_request_pending);
    TEST_ASSERT_FALSE(info.ssp_confirm_pending);
    TEST_ASSERT_EQUAL_UINT32(0, info.passkey);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_pin_request_sets_pending_flags);
    RUN_TEST(test_ssp_request_records_passkey);
    RUN_TEST(test_auth_complete_clears_pending);
    RUN_TEST(test_new_event_replaces_previous_target);
    return UNITY_END();
}
