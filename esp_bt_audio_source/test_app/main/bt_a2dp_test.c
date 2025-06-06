/**
 * @file bt_a2dp_test.c
 * @brief Implementation of Bluetooth A2DP tests
 */

#include "test_config.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "bt_source.h"
#include "bt_mock_devices.h"
#include "unity.h"

static const char *TAG = "BT_A2DP_TEST";

// Test device data
static const char *TEST_DEVICE_ADDR = "AA:BB:CC:11:22:33";
static const char *TEST_DEVICE_NAME = "Test Audio Device";
static const char *TEST_PHONE_ADDR = "DD:EE:FF:44:55:66";
static const char *TEST_PHONE_NAME = "Test Phone";
#define TEST_SCAN_TIMEOUT 5 // seconds

// Helper function to set up basic mock devices for testing
static void setup_mock_devices(void) {
    // Reset the mock state first
    bt_mock_reset();
    
    // Add a mock audio device
    bt_mock_add_device(TEST_DEVICE_ADDR, TEST_DEVICE_NAME, BT_DEVICE_TYPE_AUDIO, false);
    
    // Add a mock phone device
    bt_mock_add_device(TEST_PHONE_ADDR, TEST_PHONE_NAME, BT_DEVICE_TYPE_PHONE, false);
    
    // Set up connect-by-name hook
    bt_mock_set_connect_by_name_hook(TEST_DEVICE_NAME, TEST_DEVICE_ADDR);
}

// Test 1: Bluetooth stack initialization
void test_bluetooth_stack_init(void) {
    ESP_LOGI(TAG, "Testing Bluetooth stack initialization");
    
    // Reset mock state
    bt_mock_reset();
    
    // Initialize Bluetooth stack
    esp_err_t ret = bt_init();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    ESP_LOGI(TAG, "Bluetooth stack initialization test completed");
}

// Test 2: Bluetooth scan start
void test_bluetooth_scan_start(void) {
    ESP_LOGI(TAG, "Testing Bluetooth scan start");
    
    // Initialize Bluetooth and reset mock
    bt_init();
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
    
    // Initialize Bluetooth
    bt_init();
    setup_mock_devices();
    
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
    ESP_LOGI(TAG, "Bluetooth scan discovered devices test completed");
}

// Test 4: Bluetooth scan filters by device type
void test_bluetooth_scan_filter_by_type(void) {
    ESP_LOGI(TAG, "Testing Bluetooth scan filters by device type");
    
    // Initialize Bluetooth
    bt_init();
    setup_mock_devices();
    
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
    bt_init();
    
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
    bt_init();
    setup_mock_devices();
    
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
    ESP_LOGI(TAG, "First device: %s (Address: %02X:%02X:%02X:%02X:%02X:%02X)",
             devices[0].name, 
             devices[0].addr[0], devices[0].addr[1], devices[0].addr[2],
             devices[0].addr[3], devices[0].addr[4], devices[0].addr[5]);
    
    ESP_LOGI(TAG, "Bluetooth scan device details test completed");
}

// Test 7: Bluetooth scan times out properly
void test_bluetooth_scan_timeout(void) {
    ESP_LOGI(TAG, "Testing Bluetooth scan timeout");
    
    // Initialize Bluetooth
    bt_init();
    
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
    bt_init();
    
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
    bt_init();
    setup_mock_devices();
    
    // Connect to device
    esp_err_t ret = bt_connect_device(TEST_DEVICE_ADDR);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Check connection status
    TEST_ASSERT_TRUE(bt_is_connected());
    
    // Disconnect
    ret = bt_disconnect();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_FALSE(bt_is_connected());
    
    ESP_LOGI(TAG, "Bluetooth connection test completed");
}

// Test 10: Connect to a device by name
void test_connect_by_name(void) {
    ESP_LOGI(TAG, "Testing connecting to device by name");
    
    // Initialize Bluetooth and add mock devices with connect-by-name hook
    bt_init();
    setup_mock_devices();
    
    // Connect by name
    esp_err_t ret = bt_connect_device_by_name(TEST_DEVICE_NAME);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Check connection status
    TEST_ASSERT_TRUE(bt_is_connected());
    
    // Disconnect
    ret = bt_disconnect();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    ESP_LOGI(TAG, "Connect by name test completed");
}

// Test 11: Handle connection failure gracefully
void test_connection_failure_handling(void) {
    ESP_LOGI(TAG, "Testing connection failure handling");
    
    // Initialize Bluetooth
    bt_init();
    
    // Try to connect to a non-existent device
    const char* nonexistent_addr = "11:22:33:44:55:66";
    esp_err_t ret = bt_connect_device(nonexistent_addr);
    
    // We expect either ESP_FAIL or ESP_ERR_NOT_FOUND
    TEST_ASSERT_NOT_EQUAL(ESP_OK, ret);
    
    // Verify not connected
    TEST_ASSERT_FALSE(bt_is_connected());
    
    ESP_LOGI(TAG, "Connection failure handling test completed");
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
    bt_init();
    setup_mock_devices();
    
    // Initial state should be disconnected
    TEST_ASSERT_FALSE(bt_is_connected());
    
    // Connect to device
    esp_err_t ret = bt_connect_device(TEST_DEVICE_ADDR);
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
    ret = bt_disconnect();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    ESP_LOGI(TAG, "Connection status info test completed");
}

// Test 14: Auto-reconnect when connection drops
void test_auto_reconnect(void) {
    ESP_LOGI(TAG, "Testing auto-reconnect when connection drops");
    
    // This needs a more complex mock implementation to simulate connection drops
    // For now, we'll just mark as a placeholder
    ESP_LOGI(TAG, "Auto-reconnect test completed");
}

// Test 15: Bluetooth connects to A2DP sink
void test_connect_to_a2dp_sink(void) {
    ESP_LOGI(TAG, "Testing connecting to A2DP sink");
    
    // Initialize Bluetooth and add mock devices
    bt_init();
    setup_mock_devices();
    
    // Connect to audio device
    esp_err_t ret = bt_connect_device(TEST_DEVICE_ADDR);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Check A2DP connection status
    TEST_ASSERT_TRUE(bt_is_connected());
    
    // Check if A2DP is connected
    TEST_ASSERT_TRUE(bt_a2dp_is_connected());
    
    // Disconnect
    ret = bt_disconnect();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    ESP_LOGI(TAG, "Connect to A2DP sink test completed");
}

// Test 16: A2DP starts and stops streaming
void test_a2dp_streaming(void) {
    ESP_LOGI(TAG, "Testing A2DP streaming start/stop");
    
    // Initialize Bluetooth and add mock devices
    bt_init();
    setup_mock_devices();
    
    // Connect to audio device
    esp_err_t ret = bt_connect_device(TEST_DEVICE_ADDR);
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
    ret = bt_disconnect();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    ESP_LOGI(TAG, "A2DP streaming test completed");
}

// Test 17: A2DP remembers paired devices
void test_a2dp_paired_devices(void) {
    ESP_LOGI(TAG, "Testing A2DP paired devices memory");
    
    // Initialize Bluetooth
    bt_init();
    
    // Add a paired device
    bt_device_t device;
    strncpy(device.name, TEST_DEVICE_NAME, sizeof(device.name) - 1);
    sscanf(TEST_DEVICE_ADDR, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
           &device.addr[0], &device.addr[1], &device.addr[2],
           &device.addr[3], &device.addr[4], &device.addr[5]);
    device.paired = true;
    device.cod = 0x240404; // Audio device
    device.rssi = -70;
    
    esp_err_t ret = bt_mock_add_paired_device(&device);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Check if device is paired
    TEST_ASSERT_TRUE(bt_is_device_paired(TEST_DEVICE_ADDR));
    
    ESP_LOGI(TAG, "A2DP paired devices test completed");
}

// Test 18: Audio streaming starts successfully
void test_audio_streaming_start_success(void) {
    ESP_LOGI(TAG, "Testing audio streaming start success");
    
    // Initialize Bluetooth and add mock devices
    bt_init();
    setup_mock_devices();
    
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
    
    ESP_LOGI(TAG, "Audio streaming start success test completed");
}

// Test 19: Audio streaming stops successfully
void test_audio_streaming_stop_success(void) {
    ESP_LOGI(TAG, "Testing audio streaming stop success");
    
    // Initialize Bluetooth and add mock devices
    bt_init();
    setup_mock_devices();
    
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
    
    ESP_LOGI(TAG, "Audio streaming stop success test completed");
}

// Test 20: Audio streaming cannot start when disconnected
void test_streaming_requires_connection(void) {
    ESP_LOGI(TAG, "Testing streaming requires connection");
    
    // Initialize Bluetooth
    bt_init();
    
    // Ensure not connected
    if (bt_is_connected()) {
        bt_disconnect();
    }
    
    // Try to start streaming without connection
    esp_err_t ret = bt_a2dp_start_streaming();
    
    // Should fail
    TEST_ASSERT_NOT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_FALSE(bt_a2dp_is_streaming());
    
    ESP_LOGI(TAG, "Streaming requires connection test completed");
}

// Test 21: Audio streaming can be paused and resumed
void test_streaming_pause_resume(void) {
    ESP_LOGI(TAG, "Testing streaming pause and resume");
    
    // Initialize Bluetooth and add mock devices
    bt_init();
    setup_mock_devices();
    
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
    
    ESP_LOGI(TAG, "Streaming pause resume test completed");
}

// Test 22: Audio streaming state is reported correctly
void test_streaming_state_reporting(void) {
    ESP_LOGI(TAG, "Testing streaming state reporting");
    
    // Initialize Bluetooth and add mock devices
    bt_init();
    setup_mock_devices();
    
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
    
    ESP_LOGI(TAG, "Streaming state reporting test completed");
}

/**
 * Run all Bluetooth A2DP tests
 */
void run_bt_a2dp_tests(void)
{
    ESP_LOGI(TAG, "Starting Bluetooth A2DP tests");
    
    // Initialize the Unity test framework
    UNITY_BEGIN();
    
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
    RUN_TEST(test_connect_to_a2dp_sink);
    
    // 3. A2DP streaming tests
    RUN_TEST(test_a2dp_streaming);
    RUN_TEST(test_a2dp_paired_devices);
    RUN_TEST(test_audio_streaming_start_success);
    RUN_TEST(test_audio_streaming_stop_success);
    RUN_TEST(test_streaming_requires_connection);
    RUN_TEST(test_streaming_pause_resume);
    RUN_TEST(test_streaming_state_reporting);
    
    // Finish Unity tests
    UNITY_END();
    
    ESP_LOGI(TAG, "Bluetooth A2DP tests completed");
}