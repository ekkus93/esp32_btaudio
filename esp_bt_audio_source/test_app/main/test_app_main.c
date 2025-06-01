#include "unity.h"
#include "bt_source.h"
#include "command_interface.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "test_bt_scan.h"  // Include our new test header

// Forward declaration of callback function
static void test_discovery_callback(bt_device_t* device, void* user_data);

// Additional A2DP Source profile tests

// Test: Bluetooth device scanning
TEST_CASE("Bluetooth scan starts successfully", "[bluetooth][a2dp]") {
    esp_err_t err = bt_scan_start();
    TEST_ASSERT_EQUAL(ESP_OK, err);
    
    // Allow some time for scan to run
    vTaskDelay(pdMS_TO_TICKS(5000));
    
    err = bt_scan_stop();
    TEST_ASSERT_EQUAL(ESP_OK, err);
    
    // Check that we found at least one device
    uint16_t device_count = bt_get_discovered_device_count();
    TEST_ASSERT_GREATER_THAN(0, device_count);
}

// Test: Bluetooth connection to a sink device
TEST_CASE("Bluetooth connects to A2DP sink", "[bluetooth][a2dp]") {
    // Use the first available device or a mock device address
    const char* test_device_addr = "AA:BB:CC:DD:EE:FF";  // Mock address for testing
    
    esp_err_t err = bt_connect(test_device_addr);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    
    // Allow some time for connection to establish
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // Verify connection status
    bool is_connected = bt_is_connected();
    TEST_ASSERT_TRUE(is_connected);
}

// Test: A2DP audio streaming control
TEST_CASE("A2DP starts and stops streaming", "[bluetooth][a2dp]") {
    // Start streaming
    esp_err_t err = bt_start_streaming();
    TEST_ASSERT_EQUAL(ESP_OK, err);
    
    // Check streaming status
    bool is_streaming = bt_is_streaming();
    TEST_ASSERT_TRUE(is_streaming);
    
    // Allow stream to run briefly
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Stop streaming
    err = bt_stop_streaming();
    TEST_ASSERT_EQUAL(ESP_OK, err);
    
    // Verify streaming stopped
    is_streaming = bt_is_streaming();
    TEST_ASSERT_FALSE(is_streaming);
}

// Test: Bluetooth disconnection
TEST_CASE("Bluetooth disconnects properly", "[bluetooth][a2dp]") {
    // First ensure we're connected
    bool is_connected = bt_is_connected();
    
    if (!is_connected) {
        // Connect to test device if not connected
        const char* test_device_addr = "AA:BB:CC:DD:EE:FF";
        esp_err_t connect_err = bt_connect(test_device_addr);
        TEST_ASSERT_EQUAL(ESP_OK, connect_err);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    
    // Now disconnect
    esp_err_t err = bt_disconnect();
    TEST_ASSERT_EQUAL(ESP_OK, err);
    
    // Allow time for disconnection
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Verify disconnected status
    is_connected = bt_is_connected();
    TEST_ASSERT_FALSE(is_connected);
}

// Test: A2DP device management
TEST_CASE("A2DP remembers paired devices", "[bluetooth][a2dp]") {
    // Get number of paired devices
    uint16_t paired_count = bt_get_paired_device_count();
    
    // Add a mock device for testing
    bt_device_t test_device = {
        .addr = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF},
        .name = "Test A2DP Device"
    };
    
    // Save the device as paired
    esp_err_t err = bt_add_paired_device(&test_device);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    
    // Verify paired count increased
    uint16_t new_paired_count = bt_get_paired_device_count();
    TEST_ASSERT_EQUAL(paired_count + 1, new_paired_count);
    
    // Clean up - remove test device
    err = bt_remove_paired_device(&test_device);
    TEST_ASSERT_EQUAL(ESP_OK, err);
}

void setUp(void) {
    // This function is called before each test
}

void tearDown(void) {
    // This function is called after each test
}

// Test: Bluetooth stack initializes successfully
TEST_CASE("Bluetooth stack initializes successfully", "[bluetooth]") {
    esp_err_t err = bt_init();
    TEST_ASSERT_EQUAL(ESP_OK, err);
}

// Test: Parse SCAN command
TEST_CASE("Parse SCAN command", "[commands]") {
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("SCAN", &ctx));
    TEST_ASSERT_EQUAL(CMD_TYPE_SCAN, ctx.type);
}

// Test: Parse CONNECT command
TEST_CASE("Parse CONNECT command", "[commands]") {
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("CONNECT AA:BB:CC:DD:EE:FF", &ctx));
    TEST_ASSERT_EQUAL(CMD_TYPE_CONNECT, ctx.type);
    TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FF", ctx.params[0]);
}

// Test: Device Scanning - Discovery Events
TEST_CASE("Bluetooth scan reports discovered devices", "[bluetooth][a2dp][scan]") {
    // Mock an event callback to receive device discovery events
    bool device_found = false;

    // Register our test callback for BT events
    bt_register_discovery_callback(test_discovery_callback, &device_found);
    
    // Start scan
    esp_err_t err = bt_scan_start();
    TEST_ASSERT_EQUAL(ESP_OK, err);
    
    // Wait for discovery events
    uint32_t start_time = xTaskGetTickCount();
    while (!device_found && 
           (xTaskGetTickCount() - start_time < pdMS_TO_TICKS(5000))) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // Stop scanning
    err = bt_scan_stop();
    TEST_ASSERT_EQUAL(ESP_OK, err);
    
    // Verify we found at least one device
    TEST_ASSERT_TRUE_MESSAGE(device_found, "No devices discovered during scan");
}

// Test: Device Scanning - Filtering by Device Type
TEST_CASE("Bluetooth scan filters by device type", "[bluetooth][a2dp][scan]") {
    // Start scan with A2DP sink filter
    esp_err_t err = bt_scan_start_filtered(BT_DEVICE_TYPE_A2DP_SINK);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    
    // Allow time for scan
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    // Stop scanning
    err = bt_scan_stop();
    TEST_ASSERT_EQUAL(ESP_OK, err);
    
    // Get discovered devices
    bt_device_t devices[10];
    uint16_t device_count = 0;
    err = bt_get_discovered_devices(devices, 10, &device_count);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    
    // Verify all devices are A2DP sinks
    for (int i = 0; i < device_count; i++) {
        TEST_ASSERT_TRUE(bt_device_supports_profile(&devices[i], BT_PROFILE_A2DP_SINK));
    }
}

// Test discovery callback function
static void test_discovery_callback(bt_device_t* device, void* user_data) {
    if (device && user_data) {
        bool* found_ptr = (bool*)user_data;
        *found_ptr = true;
    }
}

// Add these test cases to your test function registrations
TEST_CASE("Bluetooth scanning basic functionality", "[bluetooth][a2dp][scan]")
{
    test_bt_scan_basic();
}

TEST_CASE("Bluetooth scan filters devices by type", "[bluetooth][a2dp][scan]")
{
    test_bt_scan_filtered();
}

TEST_CASE("Bluetooth scan returns device details", "[bluetooth][a2dp][scan]")
{
    test_bt_scan_get_results();
}

TEST_CASE("Bluetooth scan times out properly", "[bluetooth][a2dp][scan]")
{
    test_bt_scan_timeout();
}

TEST_CASE("Bluetooth scan can be stopped early", "[bluetooth][a2dp][scan]")
{
    test_bt_scan_stop();
}

void app_main(void) {
    unity_run_menu();
}