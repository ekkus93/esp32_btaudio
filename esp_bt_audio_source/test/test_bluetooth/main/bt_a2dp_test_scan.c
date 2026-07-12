/* bt_a2dp_test_scan.c — scan scenario test bodies, split out of
 * bt_a2dp_test.c; linked into the same test_bluetooth app. */
#include "bt_a2dp_test_shared.h"

void test_bluetooth_stack_init(void) {
    ESP_LOGI(TAG, "Testing Bluetooth stack initialization");
    
    // Initialize Bluetooth stack
    esp_err_t ret = test_bt_manager_init();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    ESP_LOGI(TAG, "Bluetooth stack initialization test completed");
}

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
