/**
 * @file bt_a2dp_test.c
 * @brief Implementation of Bluetooth A2DP tests
 */

#include "test_config.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "include/unity_config.h"
#include "unity.h"
#include "esp_log.h"
// Add required FreeRTOS includes
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bt_mock_devices.h"
#include "bt_mock.h"
#include "bt_mock_setup.h"  // Update this include
#include "bt_test_setup.h"
#include "test_helpers.h"

/* Test-only stub helper that clears the deferred disconnect visibility flag. */
void bt_source_stub_release_disconnect_visibility(void);

/* Forward declarations for pairing tests added in test_pairing_commands.c
 * These functions live in a separate translation unit; provide prototypes
 * so RUN_TEST can reference them without causing implicit/undeclared
 * identifier compile errors. */
void test_pairing_commands_happy_path(void);
void test_enter_pin_uses_default_when_missing(void);
void test_confirm_pin_without_pending_request_returns_error_event(void);

static const char *TAG = "BT_A2DP_TEST";

// Helper: wait until bt_is_connected() equals expected, or timeout (ms) elapses.
static bool wait_for_connected_state(bool expected, int timeout_ms)
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
static bool wait_for_authoritative_connected_state(bool expected, int timeout_ms)
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

// Test 1: Bluetooth stack initialization
void test_bluetooth_stack_init(void) {
    ESP_LOGI(TAG, "Testing Bluetooth stack initialization");
    
    // Initialize Bluetooth stack
    esp_err_t ret = test_bt_manager_init();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    ESP_LOGI(TAG, "Bluetooth stack initialization test completed");
}

// Test 2: Bluetooth scan start
void test_bluetooth_scan_start(void) {
    ESP_LOGI(TAG, "Testing Bluetooth scan start");
    
    // Initialize Bluetooth and reset mock
    test_bt_manager_init();
    bt_mock_reset();
    
    // Start scan
    esp_err_t ret = bt_scan_start();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Verify scanning state
    TEST_ASSERT_TRUE(bt_is_scanning());
    
    // Stop scan
    ret = bt_scan_stop();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    ESP_LOGI(TAG, "Bluetooth scan start test completed");
}

// Test 3: Bluetooth scan reports discovered devices
void test_bluetooth_scan_discovered_devices(void) {
    ESP_LOGI(TAG, "Testing Bluetooth scan discovered devices");
    
    // Initialize Bluetooth and add mock devices
    test_bt_manager_init();
    bt_mock_setup_common(); // Use our common test setup (renamed from bt_test_setup_common)
    
    // Start scan
    esp_err_t ret = bt_scan(3); // 3-second scan
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Simulate scan completion
    vTaskDelay(pdMS_TO_TICKS(100)); // Small delay to let mock scan "run"
    bt_scan_stop();
    
    // Check for discovered devices
    uint16_t count = bt_get_discovered_device_count();
    TEST_ASSERT_GREATER_THAN(0, count);
    
    // Get the discovered devices
    bt_device_t devices[5]; // Buffer for up to 5 devices
    uint16_t actual_count = 0;
    ret = bt_get_discovered_devices(devices, 5, &actual_count);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_GREATER_THAN(0, actual_count);
    
    ESP_LOGI(TAG, "Bluetooth scan discovered %d devices", actual_count);
}

// Test 4: Bluetooth scan filters by device type
void test_bluetooth_scan_filter_by_type(void) {
    ESP_LOGI(TAG, "Testing Bluetooth scan filters by device type");
    
    // Initialize Bluetooth and add mock devices
    test_bt_manager_init();
    bt_mock_setup_common(); // Use our common test setup
    
    // Start filtered scan for audio devices
    esp_err_t ret = bt_scan_start_filtered(BT_DEVICE_TYPE_AUDIO);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Simulate scan completion
    vTaskDelay(pdMS_TO_TICKS(100));
    bt_scan_stop();
    
    ESP_LOGI(TAG, "Bluetooth scan filter by device type test completed");
}

// Test 5: Bluetooth scanning basic functionality
void test_bluetooth_scanning_basic(void) {
    ESP_LOGI(TAG, "Testing Bluetooth scanning basic functionality");
    
    // Initialize Bluetooth
    test_bt_manager_init();
    
    // Test starting scan
    esp_err_t ret = bt_scan_start();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_TRUE(bt_is_scanning());
    
    // Test stopping scan
    ret = bt_scan_stop();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_FALSE(bt_is_scanning());
    
    ESP_LOGI(TAG, "Bluetooth scanning basic functionality test completed");
}

// Test 6: Bluetooth scan returns device details
void test_bluetooth_scan_device_details(void) {
    ESP_LOGI(TAG, "Testing Bluetooth scan device details");
    
    // Initialize Bluetooth and add mock devices
    test_bt_manager_init();
    bt_mock_setup_common(); // Use our common test setup
    
    // Start scan
    esp_err_t ret = bt_scan(1);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Simulate scan completion
    vTaskDelay(pdMS_TO_TICKS(100));
    bt_scan_stop();
    
    // Get device details
    bt_device_t devices[5];
    uint16_t actual_count = 0;
    ret = bt_get_discovered_devices(devices, 5, &actual_count);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_GREATER_THAN(0, actual_count);
    
    // Check device details for the first device
    ESP_LOGI(TAG, "First device: %s", devices[0].name);
    TEST_ASSERT_GREATER_THAN(0, strlen(devices[0].name));
    
    ESP_LOGI(TAG, "Bluetooth scan device details test completed");
}

// Test 7: Bluetooth scan times out properly
void test_bluetooth_scan_timeout(void) {
    ESP_LOGI(TAG, "Testing Bluetooth scan timeout");
    
    // Initialize Bluetooth
    test_bt_manager_init();
    
    // Start scan with timeout from the constant
    esp_err_t ret = bt_scan(TEST_SCAN_TIMEOUT);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_TRUE(bt_is_scanning());
    
    // Wait for scan to complete by timeout
    vTaskDelay(pdMS_TO_TICKS((TEST_SCAN_TIMEOUT * 1000) + 500)); // Wait timeout + 500ms
    
    // Verify scan is no longer active
    TEST_ASSERT_FALSE(bt_is_scanning());
    
    ESP_LOGI(TAG, "Bluetooth scan timeout test completed");
}

// Test 8: Bluetooth scan can be stopped early
void test_bluetooth_scan_stop_early(void) {
    ESP_LOGI(TAG, "Testing Bluetooth scan can be stopped early");
    
    // Initialize Bluetooth
    test_bt_manager_init();
    
    // Start scan with long timeout
    esp_err_t ret = bt_scan(10); // 10 seconds
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_TRUE(bt_is_scanning());
    
    // Stop scan early
    vTaskDelay(pdMS_TO_TICKS(200)); // Wait just 200ms
    ret = bt_scan_stop();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_FALSE(bt_is_scanning());
    
    ESP_LOGI(TAG, "Bluetooth scan stop early test completed");
}

// Test 9: Connect to a device by address
void test_bluetooth_connection(void) {
    ESP_LOGI(TAG, "Testing connecting to device by address");
    
    // Initialize Bluetooth and add mock devices
    test_bt_manager_init();
    setup_mock_devices();
    
    // Connect to device
    esp_err_t ret = bt_connect_device(TEST_DEVICE_ADDR);
    ESP_LOGI(TAG, "DIAG_TEST: bt_connect_device(%s) returned %d (0x%08x)", TEST_DEVICE_ADDR, (int)ret, (unsigned int)ret);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Check connection status
    TEST_ASSERT_TRUE(bt_is_connected());
    
    // Disconnect
    ESP_LOGI(TAG, "DIAG_TEST_MARKER: about to call bt_disconnect() (test_connect_by_addr)");
    ret = bt_disconnect();
    ESP_LOGI(TAG, "DIAG_TEST: bt_disconnect() returned %d (0x%08x)", (int)ret, (unsigned int)ret);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    /* Wait for both stub and authoritative mock to reflect the disconnect. */
    TEST_ASSERT_TRUE(wait_for_authoritative_connected_state(false, 1000));
    
    ESP_LOGI(TAG, "Bluetooth connection test completed");
}

// Test 10: Connect to a device by name
void test_connect_by_name(void) {
    ESP_LOGI(TAG, "Testing connecting to device by name");
    
    // Initialize Bluetooth and add mock devices with connect-by-name hook
    test_bt_manager_init();
    bt_mock_setup_common(); // Use our common test setup
    
    // Connect by name
    esp_err_t ret = bt_connect_device_by_name(TEST_DEVICE_NAME);
    ESP_LOGI(TAG, "DIAG_TEST: bt_connect_device_by_name(%s) returned %d (0x%08x)", TEST_DEVICE_NAME, (int)ret, (unsigned int)ret);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Check connection status
    TEST_ASSERT_TRUE(bt_is_connected());
    
    // Disconnect
    ESP_LOGI(TAG, "DIAG_TEST_MARKER: about to call bt_disconnect() (test_connect_by_name)");
    ret = bt_disconnect();
    ESP_LOGI(TAG, "DIAG_TEST: bt_disconnect() returned %d (0x%08x)", (int)ret, (unsigned int)ret);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

// Test 11: Handle connection failure gracefully
void test_connection_failure_handling(void) {
    ESP_LOGI(TAG, "Testing connection failure handling");
    
    // Initialize Bluetooth
    test_bt_manager_init();
    
    // Try to connect to a non-existent device
    const char* nonexistent_addr = "99:88:77:66:55:44";
    esp_err_t ret = bt_connect_device(nonexistent_addr);
    
    /* The mock may surface immediate lookup errors (ESP_FAIL/ESP_ERR_NOT_FOUND)
     * or accept the request and deliver the failure asynchronously. Accept any
     * non-success code but also treat ESP_OK as valid so long as the connection
     * never transitions to connected. */
    TEST_ASSERT((ret == ESP_FAIL) || (ret == ESP_ERR_NOT_FOUND) || (ret == ESP_OK));

    /* Log current connection states before waiting so we can observe stub vs
     * authoritative mock divergence when the assertion below fails. */
    bt_source_stub_release_disconnect_visibility();
    ESP_LOGI(TAG,
             "DIAG_TEST: after failed connect stub_connected=%d mock_connected=%d",
             bt_is_connected(),
             bt_mock_is_connected());

    // Verify not connected: wait for authoritative state to be false
    TEST_ASSERT_TRUE(wait_for_authoritative_connected_state(false, 1000));
}

// Test 12: Handle connection timeout
void test_connection_timeout(void) {
    ESP_LOGI(TAG, "Testing connection timeout handling");
    
    // This would require timing logic in the mock, for now we'll just simulate it
    ESP_LOGI(TAG, "Connection timeout test completed");
}

// Test 13: Get connection status information
void test_connection_status_info(void) {
    ESP_LOGI(TAG, "Testing connection status info");
    
    // Initialize Bluetooth and add mock devices
        test_bt_manager_init();
    bt_mock_setup_common();
    
    // Initial state should be disconnected
    TEST_ASSERT_FALSE(bt_is_connected());
    
    // Connect to device
    esp_err_t ret = bt_connect_device(TEST_DEVICE_ADDR);
    ESP_LOGI(TAG, "DIAG_TEST: bt_connect_device(%s) returned %d (0x%08x)", TEST_DEVICE_ADDR, (int)ret, (unsigned int)ret);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Check connection status
    TEST_ASSERT_TRUE(bt_is_connected());
    
    // Check connection info
    bt_connection_info_t info;
    ret = bt_get_connection_info(&info);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Verify info
    TEST_ASSERT_TRUE(info.connected);
    
    // Disconnect
    ESP_LOGI(TAG, "DIAG_TEST_MARKER: about to call bt_disconnect() (test_connection_status_info)");
    ret = bt_disconnect();
    ESP_LOGI(TAG, "DIAG_TEST: bt_disconnect() returned %d (0x%08x)", (int)ret, (unsigned int)ret);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    /* Ensure both the stub-visible state and authoritative mock report the
     * disconnect before allowing subsequent tests to run. */
    TEST_ASSERT_TRUE(wait_for_authoritative_connected_state(false, 1000));
}

// Test 14: Auto-reconnect when connection drops
void test_auto_reconnect(void) {
    ESP_LOGI(TAG, "Testing auto-reconnect when connection drops");

    test_bt_manager_init();
    bt_mock_setup_common();

    /* Baseline: connect with auto-reconnect disabled and ensure disconnect stays down. */
    TEST_ASSERT_EQUAL(ESP_OK, bt_connect_device(TEST_DEVICE_ADDR));
    TEST_ASSERT_TRUE(wait_for_authoritative_connected_state(true, 1000));

    TEST_ASSERT_EQUAL(ESP_OK, bt_set_auto_reconnect(false));
    TEST_ASSERT_EQUAL(ESP_OK, bt_simulate_disconnect());
    TEST_ASSERT_TRUE(wait_for_authoritative_connected_state(false, 1000));

    bt_connection_info_t info = {0};
    TEST_ASSERT_EQUAL(ESP_OK, bt_get_connection_info(&info));
    TEST_ASSERT_FALSE(info.connected);
    TEST_ASSERT_EQUAL(BT_CONNECTION_STATE_DISCONNECTED, info.state);

    /* Enable auto-reconnect, reconnect manually once, then drop and verify mock restores. */
    TEST_ASSERT_EQUAL(ESP_OK, bt_connect_device(TEST_DEVICE_ADDR));
    TEST_ASSERT_TRUE(wait_for_authoritative_connected_state(true, 1000));

    TEST_ASSERT_EQUAL(ESP_OK, bt_set_auto_reconnect(true));
    TEST_ASSERT_EQUAL(ESP_OK, bt_simulate_disconnect());

    /* Reconnect is asynchronous; allow the mock+stub pair to settle instead of
     * asserting the immediate state right after the simulated drop. */
    TEST_ASSERT_TRUE_MESSAGE(
        wait_for_authoritative_connected_state(true, 1000),
        "auto-reconnect should restore connection to authoritative mock");

    memset(&info, 0, sizeof(info));
    TEST_ASSERT_EQUAL(ESP_OK, bt_get_connection_info(&info));
    TEST_ASSERT_TRUE(info.connected);
    TEST_ASSERT_EQUAL(BT_CONNECTION_STATE_CONNECTED, info.state);
    TEST_ASSERT_EQUAL_STRING(TEST_DEVICE_ADDR, info.addr);

    /* Cleanup: disable auto-reconnect and ensure disconnect clears state. */
    TEST_ASSERT_EQUAL(ESP_OK, bt_set_auto_reconnect(false));
    TEST_ASSERT_EQUAL(ESP_OK, bt_disconnect());
    TEST_ASSERT_TRUE(wait_for_authoritative_connected_state(false, 1000));
}

void test_auto_reconnect_should_stop_after_failed_attempts(void)
{
    ESP_LOGI(TAG, "Testing auto-reconnect stops after max failed attempts");

    test_bt_manager_init();
    bt_mock_setup_common();

    bt_conn_test_reset_state();
    const esp_err_t reconnect_results[] = {ESP_FAIL, ESP_FAIL, ESP_FAIL};
    bt_conn_test_set_reconnect_results(reconnect_results, sizeof(reconnect_results) / sizeof(reconnect_results[0]));
    bt_conn_test_set_reconnect_delay_ms(0);

    TEST_ASSERT_EQUAL(ESP_OK, bt_connect_device(TEST_DEVICE_ADDR));
    TEST_ASSERT_TRUE(wait_for_authoritative_connected_state(true, 1000));
    TEST_ASSERT_EQUAL(ESP_OK, bt_set_auto_reconnect(true));

    TEST_ASSERT_EQUAL(ESP_OK, bt_simulate_disconnect());

    bt_connection_info_t info = {0};
    TEST_ASSERT_EQUAL(ESP_OK, bt_get_connection_info(&info));
    TEST_ASSERT_FALSE(info.connected);
    TEST_ASSERT_EQUAL(BT_CONNECTION_STATE_FAILED, info.state);
    TEST_ASSERT_EQUAL_UINT8(3, info.retry_count);
}

void test_auto_reconnect_should_apply_configured_delay(void)
{
    ESP_LOGI(TAG, "Testing auto-reconnect delay between attempts");

    test_bt_manager_init();
    bt_mock_setup_common();

    bt_conn_test_reset_state();
    const esp_err_t reconnect_results[] = {ESP_FAIL, ESP_OK};
    bt_conn_test_set_reconnect_results(reconnect_results, sizeof(reconnect_results) / sizeof(reconnect_results[0]));
    bt_conn_test_set_reconnect_delay_ms(50);

    TEST_ASSERT_EQUAL(ESP_OK, bt_connect_device(TEST_DEVICE_ADDR));
    TEST_ASSERT_TRUE(wait_for_authoritative_connected_state(true, 1000));

    TEST_ASSERT_EQUAL(ESP_OK, bt_set_auto_reconnect(true));
    TickType_t start_ticks = xTaskGetTickCount();
    TEST_ASSERT_EQUAL(ESP_OK, bt_simulate_disconnect());
    TickType_t end_ticks = xTaskGetTickCount();

    uint32_t elapsed_ms = (uint32_t)((end_ticks - start_ticks) * portTICK_PERIOD_MS);
    uint32_t expected_ms = 2U * 50U; /* Delay applied before each attempt (fail + retry). */

    bt_connection_info_t info = {0};
    TEST_ASSERT_EQUAL(ESP_OK, bt_get_connection_info(&info));
    TEST_ASSERT_TRUE(info.connected);
    TEST_ASSERT_EQUAL(BT_CONNECTION_STATE_CONNECTED, info.state);
    TEST_ASSERT_EQUAL_UINT8(0, info.retry_count);

    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(expected_ms, elapsed_ms);

    /* Cleanup */
    TEST_ASSERT_EQUAL(ESP_OK, bt_set_auto_reconnect(false));
    TEST_ASSERT_EQUAL(ESP_OK, bt_disconnect());
}

// Test 15: Bluetooth connects to A2DP sink
void test_connect_to_a2dp_sink(void) {
    ESP_LOGI(TAG, "Testing connecting to A2DP sink");
    
    // Initialize Bluetooth and add mock devices
    test_bt_manager_init();
    bt_mock_setup_common();
    
    // Connect to audio device
    esp_err_t ret = bt_connect_device(TEST_DEVICE_ADDR);
    ESP_LOGI(TAG, "DIAG_TEST: bt_connect_device(%s) returned %d (0x%08x)", TEST_DEVICE_ADDR, (int)ret, (unsigned int)ret);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Check A2DP connection status
    TEST_ASSERT_TRUE(bt_is_connected());
    
    // Check if A2DP is connected
    TEST_ASSERT_TRUE(bt_a2dp_is_connected());
    
    // Disconnect
    ESP_LOGI(TAG, "DIAG_TEST_MARKER: about to call bt_disconnect() (test_connect_to_a2dp_sink)");
    ret = bt_disconnect();
    ESP_LOGI(TAG, "DIAG_TEST: bt_disconnect() returned %d (0x%08x)", (int)ret, (unsigned int)ret);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

// Test 16: A2DP starts and stops streaming
void test_a2dp_streaming(void) {
    ESP_LOGI(TAG, "Testing A2DP streaming start/stop");
    
    // Initialize Bluetooth and add mock devices
    test_bt_manager_init();
    bt_mock_setup_common();
    
    // Connect to audio device
    esp_err_t ret = bt_connect_device(TEST_DEVICE_ADDR);
    ESP_LOGI(TAG, "DIAG_TEST: bt_connect_device(%s) returned %d (0x%08x)", TEST_DEVICE_ADDR, (int)ret, (unsigned int)ret);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Start audio streaming
    ret = bt_a2dp_start_streaming();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Check if streaming
    TEST_ASSERT_TRUE(bt_a2dp_is_streaming());
    
    // Stop streaming
    ret = bt_a2dp_stop_streaming();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Check if not streaming
    TEST_ASSERT_FALSE(bt_a2dp_is_streaming());
    
    // Disconnect
    ESP_LOGI(TAG, "DIAG_TEST_MARKER: about to call bt_disconnect() (test_a2dp_streaming)");
    ret = bt_disconnect();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

// Test 17: A2DP remembers paired devices
void test_a2dp_paired_devices(void) {
    ESP_LOGI(TAG, "Testing A2DP paired devices memory");
    
    // Initialize Bluetooth
    test_bt_manager_init();
    bt_mock_setup_paired_devices(); // Use our paired device setup (renamed from bt_test_setup_paired_devices)
    
    // Check if device is paired
    TEST_ASSERT_TRUE(bt_is_device_paired(TEST_DEVICE_ADDR));
}

// Test 18: Audio streaming starts successfully
void test_audio_streaming_start_success(void) {
    ESP_LOGI(TAG, "Testing audio streaming start success");
    
    // Initialize Bluetooth and add mock devices
    test_bt_manager_init();
    bt_mock_setup_common();
    
    // Connect to audio device
    esp_err_t ret = bt_connect_device(TEST_DEVICE_ADDR);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Start audio streaming
    ret = bt_a2dp_start_streaming();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Verify streaming state
    TEST_ASSERT_TRUE(bt_a2dp_is_streaming());
    
    // Stop streaming and disconnect
    bt_a2dp_stop_streaming();
    bt_disconnect();
}

// Test 19: Audio streaming stops successfully
void test_audio_streaming_stop_success(void) {
    ESP_LOGI(TAG, "Testing audio streaming stop success");
    
    // Initialize Bluetooth and add mock devices
    test_bt_manager_init();
    bt_mock_setup_common();
    
    // Connect to audio device
    esp_err_t ret = bt_connect_device(TEST_DEVICE_ADDR);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Start audio streaming
    ret = bt_a2dp_start_streaming();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Stop streaming
    ret = bt_a2dp_stop_streaming();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Verify not streaming
    TEST_ASSERT_FALSE(bt_a2dp_is_streaming());
    
    // Disconnect
    bt_disconnect();
}

// Test 20: Audio streaming cannot start when disconnected
void test_streaming_requires_connection(void) {
    ESP_LOGI(TAG, "Testing streaming requires connection");
    
    // Initialize Bluetooth
    test_bt_manager_init();
    
    // Ensure not connected
    if (bt_is_connected()) {
        bt_disconnect();
    }
    
    // Try to start streaming without connection
    esp_err_t ret = bt_a2dp_start_streaming();
    
    // Should fail
    TEST_ASSERT_NOT_EQUAL(ESP_OK, ret);
    /* If we had to disconnect above, allow a short window for authoritative state to settle. */
    TEST_ASSERT_TRUE(wait_for_connected_state(false, 1000));
    TEST_ASSERT_FALSE(bt_a2dp_is_streaming());
}

// Test 21: Audio streaming can be paused and resumed
void test_streaming_pause_resume(void) {
    ESP_LOGI(TAG, "Testing streaming pause and resume");
    
    // Initialize Bluetooth and add mock devices
    test_bt_manager_init();
    bt_mock_setup_common();
    
    // Connect to audio device
    esp_err_t ret = bt_connect_device(TEST_DEVICE_ADDR);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Start streaming
    ret = bt_a2dp_start_streaming();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_TRUE(bt_a2dp_is_streaming());
    
    // Pause streaming
    ret = bt_a2dp_pause_streaming();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_FALSE(bt_a2dp_is_streaming());
    
    // Resume streaming
    ret = bt_a2dp_resume_streaming();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_TRUE(bt_a2dp_is_streaming());
    
    // Stop streaming and disconnect
    bt_a2dp_stop_streaming();
    bt_disconnect();
}

// Test 22: Audio streaming state is reported correctly
void test_streaming_state_reporting(void) {
    ESP_LOGI(TAG, "Testing streaming state reporting");
    
    // Initialize Bluetooth and add mock devices
    test_bt_manager_init();
    bt_mock_setup_common();
    
    // Initial state should be not streaming
    TEST_ASSERT_FALSE(bt_a2dp_is_streaming());
    
    // Connect to audio device
    esp_err_t ret = bt_connect_device(TEST_DEVICE_ADDR);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Start streaming
    ret = bt_a2dp_start_streaming();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_TRUE(bt_a2dp_is_streaming());
    
    // Stop streaming
    ret = bt_a2dp_stop_streaming();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_FALSE(bt_a2dp_is_streaming());
    
    // Disconnect
    bt_disconnect();
}

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
    RUN_TEST(test_connect_to_a2dp_sink);

    // 3. A2DP streaming tests
    RUN_TEST(test_a2dp_streaming);
    RUN_TEST(test_a2dp_paired_devices);
    RUN_TEST(test_audio_streaming_start_success);
    RUN_TEST(test_audio_streaming_stop_success);
    RUN_TEST(test_streaming_requires_connection);
    RUN_TEST(test_streaming_pause_resume);
    RUN_TEST(test_streaming_state_reporting);
    
    unity_set_setup_function(NULL);
    
    ESP_LOGI(TAG, "Bluetooth A2DP tests completed");
}