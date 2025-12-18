#include <string.h>
#include "unity.h"
#include "bt_manager.h"

// Test hooks from bt_manager_test_hooks.c
void bt_manager_test_reset_forces(void);
void bt_manager_test_reset_pending(void);
int bt_manager_test_get_pair_event_count(void);
const char* bt_manager_test_get_last_pair_event_subtype(void);
const char* bt_manager_test_get_last_pair_event_data(void);
bool bt_manager_test_gap_pin_request(const char* mac);
bool bt_manager_test_gap_ssp_confirm(const char* mac, uint32_t passkey);
void bt_manager_test_gap_auth_complete(const char* mac, bool success);
bool bt_pairing_get_pending_request(bt_pairing_request_info_t* info);

void setUp(void) {
    bt_manager_test_reset_forces();
    bt_manager_test_reset_pending();
}

void tearDown(void) {
    bt_manager_test_reset_forces();
    bt_manager_test_reset_pending();
}

static void assert_no_pending(void) {
    bt_pairing_request_info_t info;
    memset(&info, 0, sizeof(info));
    TEST_ASSERT_FALSE(bt_pairing_get_pending_request(&info));
    TEST_ASSERT_EQUAL_UINT32(0, info.passkey);
    TEST_ASSERT_EQUAL_CHAR('\0', info.mac[0]);
    TEST_ASSERT_FALSE(info.pin_request_pending);
    TEST_ASSERT_FALSE(info.ssp_confirm_pending);
}

void test_pin_request_emits_event_and_sets_pending(void) {
    TEST_ASSERT_TRUE(bt_manager_test_gap_pin_request("11:22:33:44:55:66"));
    TEST_ASSERT_EQUAL(1, bt_manager_test_get_pair_event_count());
    TEST_ASSERT_EQUAL_STRING("PIN_REQUEST", bt_manager_test_get_last_pair_event_subtype());
    TEST_ASSERT_EQUAL_STRING("11:22:33:44:55:66", bt_manager_test_get_last_pair_event_data());

    bt_pairing_request_info_t info;
    memset(&info, 0, sizeof(info));
    TEST_ASSERT_TRUE(bt_pairing_get_pending_request(&info));
    TEST_ASSERT_TRUE(info.pin_request_pending);
    TEST_ASSERT_FALSE(info.ssp_confirm_pending);
    TEST_ASSERT_EQUAL_UINT32(0, info.passkey);
    TEST_ASSERT_EQUAL_STRING("11:22:33:44:55:66", info.mac);
}

void test_ssp_confirm_emits_event_and_auth_failure_clears_pending(void) {
    const char* mac = "AA:BB:CC:DD:EE:FF";
    const uint32_t passkey = 123456;

    TEST_ASSERT_TRUE(bt_manager_test_gap_ssp_confirm(mac, passkey));
    TEST_ASSERT_EQUAL(1, bt_manager_test_get_pair_event_count());
    TEST_ASSERT_EQUAL_STRING("CONFIRM", bt_manager_test_get_last_pair_event_subtype());
    TEST_ASSERT_EQUAL_STRING("aa:bb:cc:dd:ee:ff,123456", bt_manager_test_get_last_pair_event_data());

    bt_pairing_request_info_t info;
    memset(&info, 0, sizeof(info));
    TEST_ASSERT_TRUE(bt_pairing_get_pending_request(&info));
    TEST_ASSERT_FALSE(info.pin_request_pending);
    TEST_ASSERT_TRUE(info.ssp_confirm_pending);
    TEST_ASSERT_EQUAL_UINT32(passkey, info.passkey);
    TEST_ASSERT_EQUAL_STRING(mac, info.mac);

    bt_manager_test_gap_auth_complete(mac, false);
    TEST_ASSERT_EQUAL(2, bt_manager_test_get_pair_event_count());
    TEST_ASSERT_EQUAL_STRING("FAILED", bt_manager_test_get_last_pair_event_subtype());
    TEST_ASSERT_EQUAL_STRING("aa:bb:cc:dd:ee:ff", bt_manager_test_get_last_pair_event_data());
    assert_no_pending();
}

void test_auth_success_emits_success_and_clears_pending(void) {
    const char* mac = "00:11:22:33:44:55";
    TEST_ASSERT_TRUE(bt_manager_test_gap_pin_request(mac));
    bt_manager_test_gap_auth_complete(mac, true);

    TEST_ASSERT_EQUAL(2, bt_manager_test_get_pair_event_count());
    TEST_ASSERT_EQUAL_STRING("SUCCESS", bt_manager_test_get_last_pair_event_subtype());
    TEST_ASSERT_EQUAL_STRING(mac, bt_manager_test_get_last_pair_event_data());
    assert_no_pending();
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_pin_request_emits_event_and_sets_pending);
    RUN_TEST(test_ssp_confirm_emits_event_and_auth_failure_clears_pending);
    RUN_TEST(test_auth_success_emits_success_and_clears_pending);
    return UNITY_END();
}
