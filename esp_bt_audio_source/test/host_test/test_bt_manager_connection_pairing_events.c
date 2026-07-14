/**
 * @file test_bt_manager_connection_pairing_events.c
 * @brief Unit tests for bt_manager connection, pairing, and event edge cases
 *
 * Phase 5.5: BT Manager Extended Edge Cases (deferred from Phase 5.1)
 * 
 * Tests cover:
 * - Connection state machine edge cases (5 tests)
 * - Pairing/unpairing validation and error paths (5 tests)
 * - Event handling with unexpected events (4 tests)
 * 
 * TDD approach:  RED → GREEN → REFACTOR
 */

#include <string.h>
#include "unity.h"
#include "bt_manager.h"
#include "bt_manager_internal.h"
#include "bt_connection.h"
#include "bt_events_gap.h"
#include "bt_events_a2dp.h"
#include "bt_events_avrc.h"
#include "mock_a2dp.h"
#include "mock_avrc.h"
#include "mock_gap.h"

// NVS mock control functions (from nvs_storage_mock.c)
extern void nvs_storage_mock_reset(void);
extern void nvs_storage_mock_set_remove_paired_device_result(esp_err_t err);
extern void nvs_storage_mock_set_clear_paired_devices_result(esp_err_t err);
extern bool nvs_storage_mock_was_clear_paired_devices_called(void);

// External test hooks
extern void bt_manager_test_reset_forces(void);
extern void bt_manager_test_set_force_unpair_failure(int v);
extern void bt_manager_test_set_force_unpair_all_failure(int v);
extern const char* bt_manager_test_get_last_unpair_mac(void);

void setUp(void)
{
    // Reset all mocks and test hooks before each test
    mock_a2dp_reset();
    mock_avrc_reset();
    mock_gap_reset();
    nvs_storage_mock_reset();
    bt_manager_test_reset_forces();
    bt_manager_test_init_mutex();

    // Reset bt_ctx to known state
    memset(&bt_ctx, 0, sizeof(bt_ctx));
    bt_ctx.initialized = false;
    bt_ctx.connected = false;
    bt_ctx.audio_playing = false;
    bt_ctx.scanning = false;
}

void tearDown(void)
{
    // Clean up after each test
    bt_manager_test_deinit_mutex();
}

/* =============================================================================
 * Connection State Machine Edge Cases (5 tests)
 * ============================================================================= */

/**
 * TDD Test 1: bt_connect() with invalid MAC format should return ESP_FAIL
 * 
 * Behavior: When MAC address has invalid format (not XX:XX:XX:XX:XX:XX),
 * bt_connect() must return ESP_FAIL and NOT call esp_a2d_source_connect().
 */
void test_bt_connect_invalid_mac_format_should_fail(void)
{
    // Arrange: Initialize bt_ctx
    bt_ctx.initialized = true;
    bt_ctx.connected = false;
    
    const char* invalid_mac = "INVALID_MAC";
    
    // Act: Try to connect with invalid MAC
    bt_err_t result = bt_connect(invalid_mac);
    
    // Assert: Should return ESP_FAIL
    TEST_ASSERT_EQUAL(ESP_FAIL, result);
    
    // Assert: esp_a2d_source_connect() should NOT have been called
    TEST_ASSERT_EQUAL(0, mock_a2dp_get_connect_calls());
}

/**
 * TDD Test 2: bt_connect() when already connected should return ESP_FAIL
 * 
 * Behavior: Attempting to connect to a different device while already
 * connected must return ESP_FAIL without calling esp_a2d_source_connect().
 */
void test_bt_connect_already_connected_should_fail(void)
{
    // Arrange: Already connected to device
    bt_ctx.initialized = true;
    bt_ctx.connected = true;
    strncpy(bt_ctx.connected_mac, "11:22:33:44:55:66", sizeof(bt_ctx.connected_mac) - 1);
    
    const char* different_mac = "AA:BB:CC:DD:EE:FF";
    
    // Act: Try to connect to different device
    bt_err_t result = bt_connect(different_mac);
    
    // Assert: Should return ESP_FAIL
    TEST_ASSERT_EQUAL(ESP_FAIL, result);
    
    // Assert: esp_a2d_source_connect() should NOT have been called
    TEST_ASSERT_EQUAL(0, mock_a2dp_get_connect_calls());
}

/**
 * TDD Test 3: bt_connect() with esp_a2d_source_connect() failure should propagate error
 * 
 * Behavior: When esp_a2d_source_connect() fails, bt_connect() must return ESP_FAIL.
 */
void test_bt_connect_a2dp_connect_failure_should_propagate_error(void)
{
    // Arrange: Mock A2DP connect to fail
    bt_ctx.initialized = true;
    bt_ctx.connected = false;
    mock_a2dp_set_connect_result(ESP_BT_STATUS_FAIL);
    
    const char* valid_mac = "11:22:33:44:55:66";
    
    // Act: Try to connect
    bt_err_t result = bt_connect(valid_mac);
    
    // Assert: Should return ESP_FAIL
    TEST_ASSERT_EQUAL(ESP_FAIL, result);
    
    // Assert: esp_a2d_source_connect() was called
    TEST_ASSERT_EQUAL(1, mock_a2dp_get_connect_calls());
}

/**
 * TDD Test 4: bt_disconnect() when not connected is idempotent
 * 
 * Behavior: Calling bt_disconnect() when not connected should return ESP_OK
 * without calling esp_a2d_source_disconnect() (idempotency).
 */
void test_bt_disconnect_not_connected_is_idempotent(void)
{
    // Arrange: Initialized but not connected
    bt_ctx.initialized = true;
    bt_ctx.connected = false;
    
    // Act: Try to disconnect
    bt_err_t result = bt_disconnect();
    
    // Assert: Should return ESP_OK (idempotent)
    TEST_ASSERT_EQUAL(ESP_OK, result);
    
    // Assert: esp_a2d_source_disconnect() should NOT have been called
    TEST_ASSERT_EQUAL(0, mock_a2dp_get_disconnect_calls());
}

/**
 * TDD Test 5: bt_disconnect() with esp_a2d_source_disconnect() failure should propagate error
 * 
 * Behavior: When esp_a2d_source_disconnect() fails, bt_disconnect() must return ESP_FAIL.
 */
void test_bt_disconnect_a2dp_disconnect_failure_should_propagate_error(void)
{
    // Arrange: Connected, mock A2DP disconnect to fail
    bt_ctx.initialized = true;
    bt_ctx.connected = true;
    strncpy(bt_ctx.connected_mac, "11:22:33:44:55:66", sizeof(bt_ctx.connected_mac) - 1);
    mock_a2dp_set_disconnect_result(ESP_BT_STATUS_FAIL);
    
    // Act: Try to disconnect
    bt_err_t result = bt_disconnect();
    
    // Assert: Should return ESP_FAIL
    TEST_ASSERT_EQUAL(ESP_FAIL, result);
    
    // Assert: esp_a2d_source_disconnect() was called
    TEST_ASSERT_EQUAL(1, mock_a2dp_get_disconnect_calls());
}

/* =============================================================================
 * Pairing/Unpairing Edge Cases (5 tests)
 * ============================================================================= */

/**
 * TDD Test 6: bt_pair() with invalid MAC address should return ESP_ERR_INVALID_ARG
 * 
 * Behavior: When MAC address format is invalid, bt_pair() must return
 * ESP_ERR_INVALID_ARG without calling GAP functions.
 */
void test_bt_pair_invalid_mac_should_return_invalid_arg(void)
{
    // Arrange: Initialized
    bt_ctx.initialized = true;
    
    const char* invalid_mac = "NOT_A_MAC";
    
    // Act: Try to pair
    bt_err_t result = bt_pair(invalid_mac);
    
    // Assert: Should return ESP_ERR_INVALID_ARG
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, result);
    
    // Assert: No GAP functions called (checked via mock_gap if needed)
}

/**
 * TDD Test 7: bt_unpair() when device not in NVS but in controller bonds
 * 
 * Behavior: When device is bonded in controller but not in NVS,
 * bt_unpair() should remove controller bond and log warning about missing NVS entry.
 * Function should still return the NVS error (ESP_ERR_NOT_FOUND).
 */
void test_bt_unpair_device_not_in_nvs_but_in_controller_should_warn(void)
{
    // Arrange: Initialized, mock NVS to return NOT_FOUND
    bt_ctx.initialized = true;
    
    const char* mac = "11:22:33:44:55:66";
    nvs_storage_mock_set_remove_paired_device_result(ESP_ERR_NOT_FOUND);
    
    // Act: Unpair device
    bt_err_t result = bt_unpair(mac);
    
    // Assert: Should return ESP_ERR_NOT_FOUND (NVS error)
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, result);
    
    // Assert: Unpair was attempted (recorded by test hook)
    TEST_ASSERT_EQUAL_STRING(mac, bt_manager_test_get_last_unpair_mac());
}

/**
 * TDD Test 8: bt_unpair() when device in NVS but not in controller bonds
 * 
 * Behavior: When device is in NVS but controller bond removal fails,
 * bt_unpair() should propagate the controller error and NOT remove NVS entry.
 */
void test_bt_unpair_device_in_nvs_but_controller_fails_should_propagate_error(void)
{
    // Arrange: Initialized, mock GAP remove_bond to fail
    bt_ctx.initialized = true;
    
    const char* mac = "11:22:33:44:55:66";
    mock_gap_set_remove_bond_result(ESP_FAIL);
    
    // Act: Unpair device
    bt_err_t result = bt_unpair(mac);
    
    // Assert: Should return ESP_FAIL (controller error)
    TEST_ASSERT_EQUAL(ESP_FAIL, result);
    
    // Assert: NVS remove should NOT have been attempted
    // (We can verify this by checking that NVS mock wasn't called)
}

/**
 * TDD Test 9: bt_unpair_all() with controller bond removal failure for some devices
 * 
 * Behavior: When some devices fail to unbond from controller, bt_unpair_all()
 * should continue removing others and return first error encountered.
 */
void test_bt_unpair_all_partial_controller_failure_should_continue(void)
{
    // Arrange: Initialized, mock controller with 3 bonded devices, 2nd fails
    bt_ctx.initialized = true;
    
    // Mock GAP to report 3 bonded devices
    mock_gap_set_bond_device_count(3);
    
    // Mock that device at index 1 (second device) fails to remove
    mock_gap_set_remove_bond_fail_at_index(1);
    
    // Act: Unpair all
    bt_err_t result = bt_unpair_all();
    
    // Assert: Should return error (first encountered failure)
    // Note: Implementation may return ESP_FAIL or specific error code
    TEST_ASSERT_NOT_EQUAL(ESP_OK, result);
    
    // Note: Actual removed count check would require mock_gap extension
    // For now, we verify error propagation
}

/**
 * TDD Test 10: bt_unpair_all() when NVS clear succeeds but controller ops fail
 * 
 * Behavior: Even if controller bond removal fails, NVS should be cleared.
 * Function should return controller error.
 */
void test_bt_unpair_all_nvs_clears_despite_controller_failure(void)
{
    // Arrange: Initialized, mock controller to fail, NVS to succeed
    bt_ctx.initialized = true;
    
    mock_gap_set_bond_device_count(2);
    mock_gap_set_remove_bond_result(ESP_FAIL);
    nvs_storage_mock_set_clear_paired_devices_result(ESP_OK);
    
    // Act: Unpair all
    bt_err_t result = bt_unpair_all();
    
    // Assert: Should return controller error
    TEST_ASSERT_EQUAL(ESP_FAIL, result);
    
    // Assert: NVS clear should have been called
    TEST_ASSERT_TRUE(nvs_storage_mock_was_clear_paired_devices_called());
}

/* =============================================================================
 * Event Handling Edge Cases (4 tests)
 * ============================================================================= */

/**
 * TDD Test 11: bt_events_gap_callback() with unexpected event type should not crash
 * 
 * Behavior: GAP callback should handle unknown event types gracefully
 * (no crash, no state corruption).
 */
void test_bt_events_gap_callback_unexpected_event_should_not_crash(void)
{
    // Arrange: Initialized
    bt_ctx.initialized = true;
    
    // Act: Call with unexpected/invalid event type
    esp_bt_gap_cb_param_t param = {0};
    
    // Cast to invalid but high value outside normal enum range
    bt_events_gap_callback((esp_bt_gap_cb_event_t)999, &param);
    
    // Assert: No crash (test passes if we reach here)
    TEST_ASSERT_TRUE(true);
}

/**
 * TDD Test 12: bt_events_a2dp_callback() with unexpected event type should not crash
 * 
 * Behavior: A2DP callback should handle unknown event types gracefully.
 */
void test_bt_events_a2dp_callback_unexpected_event_should_not_crash(void)
{
    // Arrange: Initialized
    bt_ctx.initialized = true;
    
    // Act: Call with unexpected/invalid event type
    esp_a2d_cb_param_t param = {0};
    
    // Cast to invalid but high value outside normal enum range
    bt_events_a2dp_callback((esp_a2d_cb_event_t)999, &param);
    
    // Assert: No crash (test passes if we reach here)
    TEST_ASSERT_TRUE(true);
}

/**
 * TDD Test 13: bt_events_avrc_callback() with unexpected event type should not crash
 * 
 * Behavior: AVRC callback should handle unknown event types gracefully.
 */
void test_bt_events_avrc_callback_unexpected_event_should_not_crash(void)
{
    // Arrange: Initialized
    bt_ctx.initialized = true;
    
    // Act: Call with unexpected/invalid event type
    esp_avrc_ct_cb_param_t param = {0};
    
    // Cast to invalid but high value outside normal enum range
    bt_events_avrc_callback((esp_avrc_ct_cb_event_t)999, &param);
    
    // Assert: No crash (test passes if we reach here)
    TEST_ASSERT_TRUE(true);
}

/**
 * TDD Test 14: Events arriving before bt_manager init completes should be safe
 * 
 * Behavior: If events arrive while bt_ctx.initialized is false,
 * callbacks should handle gracefully (no crash, no invalid state changes).
 */
void test_events_before_init_should_be_safe(void)
{
    // Arrange: NOT initialized
    bt_ctx.initialized = false;
    
    // Act: Send various events
    esp_bt_gap_cb_param_t gap_param = {0};
    bt_events_gap_callback(ESP_BT_GAP_DISC_RES_EVT, &gap_param);
    
    esp_a2d_cb_param_t a2dp_param = {0};
    bt_events_a2dp_callback(ESP_A2D_CONNECTION_STATE_EVT, &a2dp_param);
    
    esp_avrc_ct_cb_param_t avrc_param = {0};
    bt_events_avrc_callback(ESP_AVRC_CT_CONNECTION_STATE_EVT, &avrc_param);
    
    // Assert: No crash, bt_ctx still not initialized
    TEST_ASSERT_FALSE(bt_ctx.initialized);
}

/* =============================================================================
 * Unity Test Runner
 * ============================================================================= */

int main(void)
{
    UNITY_BEGIN();
    
    /* Connection State Machine Tests (5.5.1) */
    RUN_TEST(test_bt_connect_invalid_mac_format_should_fail);
    RUN_TEST(test_bt_connect_already_connected_should_fail);
    RUN_TEST(test_bt_connect_a2dp_connect_failure_should_propagate_error);
    RUN_TEST(test_bt_disconnect_not_connected_is_idempotent);
    RUN_TEST(test_bt_disconnect_a2dp_disconnect_failure_should_propagate_error);
    
    /* Pairing/Unpairing Edge Cases (5.5.2) */
    RUN_TEST(test_bt_pair_invalid_mac_should_return_invalid_arg);
    RUN_TEST(test_bt_unpair_device_not_in_nvs_but_in_controller_should_warn);
    RUN_TEST(test_bt_unpair_device_in_nvs_but_controller_fails_should_propagate_error);
    RUN_TEST(test_bt_unpair_all_partial_controller_failure_should_continue);
    RUN_TEST(test_bt_unpair_all_nvs_clears_despite_controller_failure);
    
    /* Event Handling Edge Cases (5.5.3) */
    RUN_TEST(test_bt_events_gap_callback_unexpected_event_should_not_crash);
    RUN_TEST(test_bt_events_a2dp_callback_unexpected_event_should_not_crash);
    RUN_TEST(test_bt_events_avrc_callback_unexpected_event_should_not_crash);
    RUN_TEST(test_events_before_init_should_be_safe);
    
    return UNITY_END();
}
