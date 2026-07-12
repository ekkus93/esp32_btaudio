#include "test_bluetooth_shared.h"

// Mock variables
bool bt_connected_callback_called = false;
bool bt_disconnected_callback_called = false;
char bt_connected_mac[18] = {0};
char bt_connected_name[32] = {0};
char bt_disconnected_mac[18] = {0};

// Mock callbacks for BT events
void test_bt_connected_cb(const char* mac, const char* name) {
    bt_connected_callback_called = true;
    strncpy(bt_connected_mac, mac, sizeof(bt_connected_mac) - 1);
    strncpy(bt_connected_name, name, sizeof(bt_connected_name) - 1);
    printf("BT connected callback: %s, %s\n", mac, name);
}

void test_bt_disconnected_cb(const char* mac) {
    bt_disconnected_callback_called = true;
    strncpy(bt_disconnected_mac, mac, sizeof(bt_disconnected_mac) - 1);
    printf("BT disconnected callback: %s\n", mac);
}

// Test setup and teardown
void setUp(void) {
    // Reset mock state
    bt_connected_callback_called = false;
    bt_disconnected_callback_called = false;
    memset(bt_connected_mac, 0, sizeof(bt_connected_mac));
    memset(bt_connected_name, 0, sizeof(bt_connected_name));
    memset(bt_disconnected_mac, 0, sizeof(bt_disconnected_mac));
    nvs_storage_mock_reset();
    bt_manager_test_reset_btstate_mock();
    bt_manager_test_reset_autostart_attempts();
    
    // Initialize the BT manager
    bt_manager_init_t config = {
        .device_name = "ESP32_TEST",
        .connected_cb = test_bt_connected_cb,
        .disconnected_cb = test_bt_disconnected_cb
    };
    
    TEST_ASSERT_EQUAL(ESP_OK, bt_manager_init(&config));
}

void tearDown(void) {
    // Clean up BT manager
    TEST_ASSERT_EQUAL(ESP_OK, bt_manager_deinit());
}

int main(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_bt_init_deinit);
    RUN_TEST(test_bt_scanning);
    RUN_TEST(test_bt_connect_disconnect);
    RUN_TEST(test_bt_connect_by_name);
    RUN_TEST(test_bt_audio_operations);
    RUN_TEST(test_bt_scan_hook_counts);
    RUN_TEST(test_bt_scan_requires_init);
    RUN_TEST(test_bt_pairing);
    RUN_TEST(test_bt_scan_ignores_when_not_scanning);
    RUN_TEST(test_bt_pairing_pending_out_of_order);
    RUN_TEST(test_bt_autostart_guard_when_playing);
    RUN_TEST(test_bt_a2dp_connection_respects_autostart_disable);
    RUN_TEST(test_bt_a2dp_connection_autostart_and_forwarding);
    RUN_TEST(test_bt_a2dp_audio_state_forwarding);
    RUN_TEST(test_bt_a2dp_remote_suspend_clears_playing);
    RUN_TEST(test_bt_a2dp_remote_suspend_then_resume);
    RUN_TEST(test_bt_gap_failure_paths_emit_events_and_clear_pending);
    RUN_TEST(test_bt_gap_auth_failure_allows_retry);
    RUN_TEST(test_bt_gap_success_emits_success_and_clears_pending);
    RUN_TEST(test_bt_gap_events_emit_command_events);
    RUN_TEST(test_bt_autostart_resets_between_sessions);
    RUN_TEST(test_bt_a2dp_disconnect_and_stop_clear_playing);
    RUN_TEST(test_bt_disconnect_failure_then_success);
    RUN_TEST(test_bt_start_stop_failure_recovery);
    RUN_TEST(test_bt_init_survives_nvs_failures);
    RUN_TEST(test_bt_init_skips_corrupt_paired_device_entries);
    RUN_TEST(test_bt_stop_failure_then_recovery_on_state_event);
    
    return UNITY_END();
}
