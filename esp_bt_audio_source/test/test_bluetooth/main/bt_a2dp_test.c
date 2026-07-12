#include "bt_a2dp_test_shared.h"


// Helper: wait until bt_is_connected() equals expected, or timeout (ms) elapses.
bool wait_for_connected_state(bool expected, int timeout_ms)
{
    if (!expected) {
        /* Ensure deferred disconnect visibility is cleared so bt_is_connected()
         * can reflect the true disconnected state before we start polling. */
        bt_source_stub_release_disconnect_visibility();
    }

    int waited = 0;
    const int step_ms = 10;
    while (bt_is_connected() != expected && waited < timeout_ms) {
        vTaskDelay(pdMS_TO_TICKS(step_ms));
        waited += step_ms;
    }
    return bt_is_connected() == expected;
}

// Wait until both the stub-local and component mock report the expected
// connection state. This is slightly more conservative and reduces flakiness
// when multiple mock layers are present.
bool wait_for_authoritative_connected_state(bool expected, int timeout_ms)
{
    if (!expected) {
        /* Allow bt_is_connected() to report the true disconnected state during
         * the subsequent polling loop by clearing any deferred visibility
         * overrides the stub enabled while servicing bt_disconnect(). */
        bt_source_stub_release_disconnect_visibility();
    }

    int waited = 0;
    const int step_ms = 10;
    bool last_stub_state = false;
    bool last_mock_state = false;
    while (waited < timeout_ms) {
        last_stub_state = bt_is_connected();
        last_mock_state = bt_mock_is_connected();
        if (last_stub_state == expected && last_mock_state == expected) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(step_ms));
        waited += step_ms;
    }
    ESP_LOGW(TAG,
             "wait_for_authoritative_connected_state timeout: expected=%d stub=%d mock=%d waited_ms=%d",
             expected,
             last_stub_state,
             last_mock_state,
             waited);
    return false;
}

void parse_test_addr(esp_bd_addr_t out)
{
    unsigned int b0, b1, b2, b3, b4, b5;
    if (sscanf(TEST_DEVICE_ADDR, "%02x:%02x:%02x:%02x:%02x:%02x", &b0, &b1, &b2, &b3, &b4, &b5) == 6) {
        out[0] = (uint8_t)b0;
        out[1] = (uint8_t)b1;
        out[2] = (uint8_t)b2;
        out[3] = (uint8_t)b3;
        out[4] = (uint8_t)b4;
        out[5] = (uint8_t)b5;
    } else {
        memset(out, 0, ESP_BD_ADDR_LEN);
    }
}

// Test 1: Bluetooth stack initialization

// Test 2: Bluetooth scan start

// Test 3: Bluetooth scan reports discovered devices

// Test 4: Bluetooth scan filters by device type

// Test 5: Bluetooth scanning basic functionality

// Test 6: Bluetooth scan returns device details

// Test 7: Bluetooth scan times out properly

// Test 8: Bluetooth scan can be stopped early

// Test 9: Connect to a device by address

// Test 10: Connect to a device by name

// Test 11: Handle connection failure gracefully

// Test 12: Handle connection timeout

// Test 13: Get connection status information

// Test 14: Auto-reconnect when connection drops


// Test 15: Bluetooth connects to A2DP sink

// Test 16: A2DP starts and stops streaming

// Test 17: A2DP remembers paired devices

// Test 18: Audio streaming starts successfully

// Test 19: Audio streaming stops successfully

// Test 20: Audio streaming cannot start when disconnected

// Test 21: Audio streaming can be paused and resumed

// Test 22: Audio streaming state is reported correctly

/* Ensure remote suspend clears streaming and a resumed START restores it. */

/* Disconnect mid-stream should clear streaming and auto-reconnect with delay. */

/**
 * Run all Bluetooth A2DP tests
 */
void run_bt_a2dp_tests(void)
{
    ESP_LOGI(TAG, "Starting Bluetooth A2DP tests");
    
    unity_set_setup_function(bt_manager_test_setup);
    
    // 1. Basic Bluetooth initialization and scanning tests
    RUN_TEST(test_bluetooth_stack_init);
    RUN_TEST(test_bluetooth_scan_start);
    RUN_TEST(test_bluetooth_scan_discovered_devices);
    RUN_TEST(test_bluetooth_scan_filter_by_type);
    RUN_TEST(test_bluetooth_scanning_basic);
    RUN_TEST(test_bluetooth_scan_device_details);
    RUN_TEST(test_bluetooth_scan_timeout);
    RUN_TEST(test_bluetooth_scan_stop_early);
    
    // 2. Bluetooth connection tests
    RUN_TEST(test_bluetooth_connection);
    RUN_TEST(test_connect_by_name);
    RUN_TEST(test_connection_failure_handling);
    RUN_TEST(test_connection_timeout);
    RUN_TEST(test_connection_status_info);
    RUN_TEST(test_auto_reconnect);
    RUN_TEST(test_auto_reconnect_should_stop_after_failed_attempts);
    RUN_TEST(test_auto_reconnect_should_apply_configured_delay);
    RUN_TEST(test_auto_reconnect_should_stop_after_first_success);
    RUN_TEST(test_auto_reconnect_disabled_should_skip_attempts);
    RUN_TEST(test_auto_reconnect_failures_clear_streaming_state);
    RUN_TEST(test_connect_to_a2dp_sink);

    // 3. A2DP streaming tests
    RUN_TEST(test_a2dp_streaming);
    RUN_TEST(test_a2dp_paired_devices);
    RUN_TEST(test_audio_streaming_start_success);
    RUN_TEST(test_audio_streaming_stop_success);
    RUN_TEST(test_streaming_requires_connection);
    RUN_TEST(test_streaming_pause_resume);
    RUN_TEST(test_streaming_state_reporting);
    RUN_TEST(test_remote_suspend_and_resume_should_toggle_stream_state);
    RUN_TEST(test_disconnect_during_streaming_should_reconnect_and_stop_stream);
    
    unity_set_setup_function(NULL);
    
    ESP_LOGI(TAG, "Bluetooth A2DP tests completed");
}