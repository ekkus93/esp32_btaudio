#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "unity.h"

#include "../../main/bt_source.h"
#include "bt_source_mock.h"
#include "bt_a2dp_test.h"

static const char *TAG = "BT_A2DP_TEST";

// Global variables to track initialization status
static bool bt_controller_initialized = false;
static bool bluedroid_initialized = false;

/**
 * @brief Test that Bluetooth stack initializes successfully
 */
void test_bluetooth_stack_init(void)
{
    ESP_LOGI(TAG, "Testing Bluetooth stack initialization");
    
    // Reset the mock and set expectations
    bt_mock_reset();
    bt_mock_set_init_return(ESP_OK);
    
    // Call the function being tested
    esp_err_t ret = bt_init();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Skip the real ESP-IDF BT controller initialization
    // or make it conditional for integration tests only
    
    // ...existing code...
}

/**
 * @brief Test A2DP starts and stops streaming
 */
void test_a2dp_streaming(void)
{
    ESP_LOGI(TAG, "Testing A2DP streaming start/stop");
    
    // Reset mock
    bt_mock_reset();
    
    // Set up mock to show we are connected
    bt_mock_set_is_connected_return(true);
    bt_mock_set_is_streaming_return(false);
    
    // Start streaming
    TEST_ASSERT_EQUAL(ESP_OK, bt_start_streaming());
    TEST_ASSERT_TRUE(bt_is_streaming());
    
    // Stop streaming
    TEST_ASSERT_EQUAL(ESP_OK, bt_stop_streaming());
    TEST_ASSERT_FALSE(bt_is_streaming());
}

/**
 * @brief Test Bluetooth scan starts successfully
 */
void test_bluetooth_scan_start(void)
{
    ESP_LOGI(TAG, "Testing Bluetooth scan start");
    
    // Reset mock
    bt_mock_reset();
    
    // Set scan return value
    bt_mock_set_scan_start_return(ESP_OK);
    
    // Start scan
    TEST_ASSERT_EQUAL(ESP_OK, bt_scan_start());
    
    // Stop scan
    TEST_ASSERT_EQUAL(ESP_OK, bt_scan_stop());
}

/**
 * @brief Test Bluetooth connection management
 */
void test_bluetooth_connection(void)
{
    ESP_LOGI(TAG, "Testing Bluetooth connection management");
    
    // Reset mock
    bt_mock_reset();
    
    // Set return values
    bt_mock_set_connect_return(ESP_OK);
    
    // Connect to device
    TEST_ASSERT_EQUAL(ESP_OK, bt_connect("00:11:22:33:44:55"));
    TEST_ASSERT_TRUE(bt_is_connected());
    
    // Disconnect
    TEST_ASSERT_EQUAL(ESP_OK, bt_disconnect());
    TEST_ASSERT_FALSE(bt_is_connected());
}

/**
 * @brief Test A2DP remembers paired devices
 */
void test_a2dp_paired_devices(void)
{
    ESP_LOGI(TAG, "Testing A2DP paired devices memory");
    
    // Reset mock
    bt_mock_reset();
    
    // Create a test device
    bt_device_t test_device = {0};
    
    // Use string format addresses
    memcpy(test_device.addr, (uint8_t[]){0x00, 0x11, 0x22, 0x33, 0x44, 0x55}, 6);
    strncpy(test_device.name, "Test Device", sizeof(test_device.name) - 1);
    
    // Set mock to return our device
    bt_mock_set_paired_devices(&test_device, 1);
    
    // Check device count
    TEST_ASSERT_EQUAL(1, bt_get_paired_device_count());
}

/**
 * @brief Test that Bluetooth scan reports discovered devices
 */
void test_bluetooth_scan_discovered_devices(void)
{
    ESP_LOGI(TAG, "Testing Bluetooth scan discovers devices");
    
    // Reset mock
    bt_mock_reset();
    
    // Create some fake discovered devices
    bt_device_t devices[3];
    memset(devices, 0, sizeof(devices));
    
    // First device
    memcpy(devices[0].addr, (uint8_t[]){0x11, 0x22, 0x33, 0x44, 0x55, 0x66}, 6);
    strncpy(devices[0].name, "Test Speaker", sizeof(devices[0].name) - 1);
    devices[0].rssi = -70;
    
    // Second device
    memcpy(devices[1].addr, (uint8_t[]){0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}, 6);
    strncpy(devices[1].name, "Car Audio", sizeof(devices[1].name) - 1);
    devices[1].rssi = -60;
    
    // Mock the discovered devices
    bt_mock_set_discovered_devices(devices, 2);
    
    // Start scan
    TEST_ASSERT_EQUAL(ESP_OK, bt_scan_start());
    
    // Get discovered devices
    bt_device_t found_devices[5];
    uint16_t count = 0;
    TEST_ASSERT_EQUAL(ESP_OK, bt_get_discovered_devices(found_devices, 5, &count));
    
    // Verify device count
    TEST_ASSERT_EQUAL(2, count);
    
    // Verify first device
    TEST_ASSERT_EQUAL_STRING("Test Speaker", found_devices[0].name);
    TEST_ASSERT_EQUAL(-70, found_devices[0].rssi);
    
    // Verify second device
    TEST_ASSERT_EQUAL_STRING("Car Audio", found_devices[1].name);
    TEST_ASSERT_EQUAL(-60, found_devices[1].rssi);
    
    // Stop scan
    TEST_ASSERT_EQUAL(ESP_OK, bt_scan_stop());
}

/**
 * @brief Test that Bluetooth scanning filters by device type
 */
void test_bluetooth_scan_filter_by_type(void)
{
    ESP_LOGI(TAG, "Testing Bluetooth scan filtering by device type");
    
    // Reset mock
    bt_mock_reset();
    
    // Create test devices of different types
    bt_device_t devices[3];
    memset(devices, 0, sizeof(devices));
    
    // A2DP sink device
    memcpy(devices[0].addr, (uint8_t[]){0x11, 0x22, 0x33, 0x44, 0x55, 0x66}, 6);
    strncpy(devices[0].name, "Speaker", sizeof(devices[0].name) - 1);
    devices[0].rssi = -65;
    
    // Classic device
    memcpy(devices[1].addr, (uint8_t[]){0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}, 6);
    strncpy(devices[1].name, "Classic Device", sizeof(devices[1].name) - 1);
    devices[1].rssi = -70;
    
    // Set up mock
    bt_mock_set_devices_by_type(BT_DEVICE_TYPE_CLASSIC, &devices[1], 1);
    
    // Start filtered scan
    TEST_ASSERT_EQUAL(ESP_OK, bt_scan_start_filtered(BT_DEVICE_TYPE_CLASSIC));
    
    // Get discovered devices
    bt_device_t found_devices[5];
    uint16_t count = 0;
    TEST_ASSERT_EQUAL(ESP_OK, bt_get_discovered_devices(found_devices, 5, &count));
    
    // Verify only one device of correct type is found
    TEST_ASSERT_EQUAL(1, count);
    TEST_ASSERT_EQUAL_STRING("Classic Device", found_devices[0].name);
    
    // Stop scan
    TEST_ASSERT_EQUAL(ESP_OK, bt_scan_stop());
}

/**
 * @brief Test basic Bluetooth scanning functionality
 */
void test_bluetooth_scanning_basic(void)
{
    ESP_LOGI(TAG, "Testing basic Bluetooth scanning functionality");
    
    // Reset mock
    bt_mock_reset();
    
    // Start scan
    TEST_ASSERT_EQUAL(ESP_OK, bt_scan_start());
    
    // Verify scan state
    TEST_ASSERT_TRUE(bt_is_scanning());  // We need to add this function to our mock
    
    // Stop scan
    TEST_ASSERT_EQUAL(ESP_OK, bt_scan_stop());
    
    // Verify scan stopped
    TEST_ASSERT_FALSE(bt_is_scanning());
}

/**
 * @brief Test that Bluetooth scan returns detailed device information
 */
void test_bluetooth_scan_device_details(void)
{
    ESP_LOGI(TAG, "Testing Bluetooth scan device details");
    
    // Reset mock
    bt_mock_reset();
    
    // Create a detailed test device
    bt_device_t device;
    memset(&device, 0, sizeof(device));
    memcpy(device.addr, (uint8_t[]){0x11, 0x22, 0x33, 0x44, 0x55, 0x66}, 6);
    strncpy(device.name, "Test Device", sizeof(device.name) - 1);
    device.rssi = -75;
    device.paired = true;
    device.cod = 0x240404; // Audio device
    
    // Set up mock to return this device
    bt_mock_set_discovered_devices(&device, 1);
    
    // Start scan
    TEST_ASSERT_EQUAL(ESP_OK, bt_scan_start());
    
    // Get discovered devices
    bt_device_t found_device;
    uint16_t count = 0;
    TEST_ASSERT_EQUAL(ESP_OK, bt_get_discovered_devices(&found_device, 1, &count));
    
    // Verify details
    TEST_ASSERT_EQUAL(1, count);
    TEST_ASSERT_EQUAL_STRING("Test Device", found_device.name);
    TEST_ASSERT_EQUAL(-75, found_device.rssi);
    TEST_ASSERT_TRUE(found_device.paired);
    TEST_ASSERT_EQUAL_UINT32(0x240404, found_device.cod);
    
    // Verify device address matches
    for (int i = 0; i < 6; i++) {
        TEST_ASSERT_EQUAL_UINT8(device.addr[i], found_device.addr[i]);
    }
    
    // Stop scan
    TEST_ASSERT_EQUAL(ESP_OK, bt_scan_stop());
}

/**
 * @brief Test that Bluetooth scan times out properly
 */
void test_bluetooth_scan_timeout(void)
{
    ESP_LOGI(TAG, "Testing Bluetooth scan timeout");
    
    // Reset mock
    bt_mock_reset();
    
    // Start scan with timeout
    uint32_t timeout_seconds = 2;
    TEST_ASSERT_EQUAL(ESP_OK, bt_scan(timeout_seconds));
    
    // Mock the timeout behavior
    bt_mock_simulate_timeout();
    
    // Verify scan is no longer running
    TEST_ASSERT_FALSE(bt_is_scanning());
}

/**
 * @brief Test that Bluetooth scan can be stopped early
 */
void test_bluetooth_scan_stop_early(void)
{
    ESP_LOGI(TAG, "Testing stopping Bluetooth scan early");
    
    // Reset mock
    bt_mock_reset();
    
    // Start scan with a long timeout
    uint32_t timeout_seconds = 30;
    TEST_ASSERT_EQUAL(ESP_OK, bt_scan(timeout_seconds));
    
    // Verify scan is running
    TEST_ASSERT_TRUE(bt_is_scanning());
    
    // Stop scan early
    TEST_ASSERT_EQUAL(ESP_OK, bt_scan_stop());
    
    // Verify scan is no longer running
    TEST_ASSERT_FALSE(bt_is_scanning());
}

/**
 * @brief Test connecting to a device by name
 */
void test_connect_by_name(void)
{
    ESP_LOGI(TAG, "Testing connecting by device name");
    
    // Reset mock
    bt_mock_reset();
    
    // Create fake discovered device
    bt_device_t device;
    memset(&device, 0, sizeof(bt_device_t));
    memcpy(device.addr, (uint8_t[]){0x11, 0x22, 0x33, 0x44, 0x55, 0x66}, 6);
    strncpy(device.name, "TestHeadphones", sizeof(device.name) - 1);
    device.rssi = -60;
    
    // Set up discovered devices
    bt_mock_set_discovered_devices(&device, 1);
    
    // Connect by name
    TEST_ASSERT_EQUAL(ESP_OK, bt_connect_by_name("TestHeadphones"));
    TEST_ASSERT_TRUE(bt_is_connected());
    
    // Disconnect
    TEST_ASSERT_EQUAL(ESP_OK, bt_disconnect());
}

/**
 * @brief Test graceful handling of connection failure
 */
void test_connection_failure_handling(void)
{
    ESP_LOGI(TAG, "Testing connection failure handling");
    
    // Reset mock
    bt_mock_reset();
    
    // Set up mock to return error
    bt_mock_set_connect_return(ESP_FAIL);
    
    // Attempt connection
    TEST_ASSERT_EQUAL(ESP_FAIL, bt_connect("00:11:22:33:44:55"));
    
    // Verify we're not connected
    TEST_ASSERT_FALSE(bt_is_connected());
}

/**
 * @brief Test connection timeout handling
 */
void test_connection_timeout(void)
{
    ESP_LOGI(TAG, "Testing connection timeout handling");
    
    // Reset mock
    bt_mock_reset();
    
    // Set up mock to return timeout error
    bt_mock_set_connect_timeout_return(ESP_ERR_TIMEOUT);
    
    // Try to connect with timeout
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, bt_connect_with_timeout("00:11:22:33:44:55", 500));
    
    // Verify not connected
    TEST_ASSERT_FALSE(bt_is_connected());
}

/**
 * @brief Test connection status information
 */
void test_connection_status_info(void)
{
    ESP_LOGI(TAG, "Testing connection status information");
    
    // Reset mock
    bt_mock_reset();
    
    // Connect to a device
    bt_mock_set_connect_return(ESP_OK);
    TEST_ASSERT_EQUAL(ESP_OK, bt_connect("00:11:22:33:44:55"));
    TEST_ASSERT_TRUE(bt_is_connected());
    
    // Set up mock connection info
    bt_mock_set_connection_info("00:11:22:33:44:55", "Test Device", -65);
    
    // Get connection info
    bt_connection_info_t info;
    TEST_ASSERT_EQUAL(ESP_OK, bt_get_connection_info(&info));
    
    // Verify data
    TEST_ASSERT_TRUE(info.connected);
    TEST_ASSERT_EQUAL_STRING("00:11:22:33:44:55", info.remote_addr);
    TEST_ASSERT_EQUAL_STRING("Test Device", info.remote_name);
    TEST_ASSERT_EQUAL(-65, info.signal_strength);
}

/**
 * @brief Test auto-reconnection when connection drops
 */
void test_auto_reconnect(void)
{
    ESP_LOGI(TAG, "Testing auto-reconnect functionality");
    
    // Reset mock
    bt_mock_reset();
    
    // Enable auto reconnect
    TEST_ASSERT_EQUAL(ESP_OK, bt_set_auto_reconnect(true));
    
    // Connect to device
    bt_mock_set_connect_return(ESP_OK);
    TEST_ASSERT_EQUAL(ESP_OK, bt_connect("00:11:22:33:44:55"));
    TEST_ASSERT_TRUE(bt_is_connected());
    
    // Simulate connection drop
    bt_mock_simulate_disconnect();
    TEST_ASSERT_FALSE(bt_is_connected());
    
    // Give time for auto-reconnect (mocked)
    bt_mock_simulate_reconnect();
    
    // Verify reconnected
    TEST_ASSERT_TRUE(bt_is_connected());
    
    // Clean up - disable auto reconnect
    TEST_ASSERT_EQUAL(ESP_OK, bt_set_auto_reconnect(false));
}

/**
 * @brief Test that streaming cannot start when disconnected
 */
void test_streaming_requires_connection(void)
{
    ESP_LOGI(TAG, "Testing streaming requires connection");
    
    // Reset mock
    bt_mock_reset();
    
    // Ensure disconnected state
    bt_mock_set_is_connected_return(false);
    
    // Attempt to start streaming - should fail
    TEST_ASSERT_NOT_EQUAL(ESP_OK, bt_start_streaming());
    
    // Verify streaming did not start
    TEST_ASSERT_FALSE(bt_is_streaming());
}

/**
 * @brief Test that streaming can be paused and resumed
 */
void test_streaming_pause_resume(void)
{
    ESP_LOGI(TAG, "Testing streaming pause and resume");
    
    // Reset mock
    bt_mock_reset();
    
    // Set up mock to show we are connected
    bt_mock_set_is_connected_return(true);
    bt_mock_set_is_streaming_return(false);
    
    // Start streaming
    TEST_ASSERT_EQUAL(ESP_OK, bt_start_streaming());
    TEST_ASSERT_TRUE(bt_is_streaming());
    
    // Pause streaming
    TEST_ASSERT_EQUAL(ESP_OK, bt_pause_streaming());
    
    // Verify paused state
    TEST_ASSERT_TRUE(bt_is_paused());
    TEST_ASSERT_FALSE(bt_is_streaming());
    
    // Resume streaming
    TEST_ASSERT_EQUAL(ESP_OK, bt_resume_streaming());
    
    // Verify streaming resumed
    TEST_ASSERT_TRUE(bt_is_streaming());
    TEST_ASSERT_FALSE(bt_is_paused());
    
    // Stop streaming
    TEST_ASSERT_EQUAL(ESP_OK, bt_stop_streaming());
    TEST_ASSERT_FALSE(bt_is_streaming());
}

/**
 * @brief Test that streaming state is reported correctly
 */
void test_streaming_state_reporting(void)
{
    ESP_LOGI(TAG, "Testing streaming state reporting");
    
    // Reset mock
    bt_mock_reset();
    
    // Set up mock to show we are connected
    bt_mock_set_is_connected_return(true);
    
    // Initially not streaming
    bt_mock_set_is_streaming_return(false);
    TEST_ASSERT_FALSE(bt_is_streaming());
    TEST_ASSERT_EQUAL(BT_STREAMING_STATE_STOPPED, bt_get_streaming_state());
    
    // Start streaming
    bt_mock_set_is_streaming_return(true);
    bt_mock_set_streaming_state(BT_STREAMING_STATE_PLAYING);
    TEST_ASSERT_TRUE(bt_is_streaming());
    TEST_ASSERT_EQUAL(BT_STREAMING_STATE_PLAYING, bt_get_streaming_state());
    
    // Pause streaming
    bt_mock_set_is_streaming_return(false);
    bt_mock_set_streaming_state(BT_STREAMING_STATE_PAUSED);
    TEST_ASSERT_FALSE(bt_is_streaming());
    TEST_ASSERT_EQUAL(BT_STREAMING_STATE_PAUSED, bt_get_streaming_state());
    
    // Resume streaming
    bt_mock_set_is_streaming_return(true);
    bt_mock_set_streaming_state(BT_STREAMING_STATE_PLAYING);
    TEST_ASSERT_TRUE(bt_is_streaming());
    TEST_ASSERT_EQUAL(BT_STREAMING_STATE_PLAYING, bt_get_streaming_state());
}

/**
 * @brief Test specific connection to A2DP sink device
 */
void test_connect_to_a2dp_sink(void)
{
    ESP_LOGI(TAG, "Testing connection to A2DP sink device");
    
    // Reset mock
    bt_mock_reset();
    
    // Create a device with A2DP sink capability
    bt_device_t device = {0};
    memcpy(device.addr, (uint8_t[]){0x11, 0x22, 0x33, 0x44, 0x55, 0x66}, 6);
    strncpy(device.name, "A2DP Speaker", sizeof(device.name) - 1);
    device.cod = 0x240404; // Audio device with A2DP sink service
    
    // Set up mock
    bt_mock_set_discovered_devices(&device, 1);
    
    // Verify device supports A2DP sink profile
    TEST_ASSERT_TRUE(bt_device_supports_profile(&device, BT_PROFILE_A2DP_SINK));
    
    // Connect to the device
    TEST_ASSERT_EQUAL(ESP_OK, bt_connect("11:22:33:44:55:66"));
    TEST_ASSERT_TRUE(bt_is_connected());
    
    // Verify proper connection through A2DP profile
    bt_connection_info_t info;
    TEST_ASSERT_EQUAL(ESP_OK, bt_get_connection_info(&info));
    
    // Check A2DP profile is active
    TEST_ASSERT_TRUE(bt_mock_get_active_profile() & BT_PROFILE_A2DP_SINK);
    
    // Disconnect
    TEST_ASSERT_EQUAL(ESP_OK, bt_disconnect());
}

/**
 * @brief Test that audio streaming starts successfully
 */
void test_audio_streaming_start_success(void)
{
    ESP_LOGI(TAG, "Testing audio streaming start success");
    
    // Reset mock
    bt_mock_reset();
    
    // Set up mock to show we are connected
    bt_mock_set_is_connected_return(true);
    bt_mock_set_is_streaming_return(false);
    
    // Get initial state
    TEST_ASSERT_FALSE(bt_is_streaming());
    TEST_ASSERT_EQUAL(BT_STREAMING_STATE_STOPPED, bt_get_streaming_state());
    
    // Start streaming - should succeed
    TEST_ASSERT_EQUAL(ESP_OK, bt_start_streaming());
    
    // Check stream started with the right state
    TEST_ASSERT_TRUE(bt_is_streaming());
    TEST_ASSERT_EQUAL(BT_STREAMING_STATE_PLAYING, bt_get_streaming_state());
    
    // Clean up - stop streaming
    TEST_ASSERT_EQUAL(ESP_OK, bt_stop_streaming());
}

/**
 * @brief Test that audio streaming stops successfully
 */
void test_audio_streaming_stop_success(void)
{
    ESP_LOGI(TAG, "Testing audio streaming stop success");
    
    // Reset mock
    bt_mock_reset();
    
    // Set up mock to show we are connected and streaming
    bt_mock_set_is_connected_return(true);
    bt_mock_set_is_streaming_return(true);
    bt_mock_set_streaming_state(BT_STREAMING_STATE_PLAYING);
    
    // Verify initial state is streaming
    TEST_ASSERT_TRUE(bt_is_streaming());
    TEST_ASSERT_EQUAL(BT_STREAMING_STATE_PLAYING, bt_get_streaming_state());
    
    // Stop streaming
    TEST_ASSERT_EQUAL(ESP_OK, bt_stop_streaming());
    
    // Check proper state after stopping
    TEST_ASSERT_FALSE(bt_is_streaming());
    TEST_ASSERT_EQUAL(BT_STREAMING_STATE_STOPPED, bt_get_streaming_state());
}

/**
 * @brief Clean up Bluetooth stack after tests
 */
static void bt_test_cleanup(void)
{
    ESP_LOGI(TAG, "Cleaning up Bluetooth stack");
    
    if (bluedroid_initialized) {
        esp_bluedroid_disable();
        esp_bluedroid_deinit();
        bluedroid_initialized = false;
    }
    
    if (bt_controller_initialized) {
        esp_bt_controller_disable();
        esp_bt_controller_deinit();
        bt_controller_initialized = false;
    }
    
    // Reset mock
    bt_mock_reset();
}

/**
 * @brief Run all Bluetooth A2DP tests
 */
void run_bt_a2dp_tests(void)
{
    ESP_LOGI(TAG, "Starting Bluetooth A2DP tests");
    
    // Initialize the mock framework
    bt_mock_init();
    
    UNITY_BEGIN();
    
    // Run the core Bluetooth tests
    RUN_TEST(test_bluetooth_stack_init);
    RUN_TEST(test_a2dp_streaming);
    RUN_TEST(test_bluetooth_scan_start);
    RUN_TEST(test_bluetooth_scan_discovered_devices);
    RUN_TEST(test_bluetooth_scan_filter_by_type);
    RUN_TEST(test_bluetooth_scanning_basic);
    RUN_TEST(test_bluetooth_scan_device_details);
    RUN_TEST(test_bluetooth_scan_timeout);
    RUN_TEST(test_bluetooth_scan_stop_early);
    RUN_TEST(test_bluetooth_connection);
    RUN_TEST(test_connect_to_a2dp_sink); // Add the new specific A2DP sink test
    RUN_TEST(test_a2dp_paired_devices);
    
    // Connection tests
    RUN_TEST(test_connect_by_name);
    RUN_TEST(test_connection_failure_handling);
    RUN_TEST(test_connection_timeout);
    RUN_TEST(test_connection_status_info);
    RUN_TEST(test_auto_reconnect);
    
    // Streaming tests - run the specific streaming tests
    RUN_TEST(test_audio_streaming_start_success);
    RUN_TEST(test_audio_streaming_stop_success);
    RUN_TEST(test_streaming_requires_connection);
    RUN_TEST(test_streaming_pause_resume);
    RUN_TEST(test_streaming_state_reporting);
    
    int failures = UNITY_END();
    
    // Clean up Bluetooth stack
    bt_test_cleanup();
    
    ESP_LOGI(TAG, "Bluetooth A2DP tests completed with %d failures", failures);
}
