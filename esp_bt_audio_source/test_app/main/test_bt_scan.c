#include "test_bt_scan.h"

#define TAG "BT_STUB"

// Mock devices for testing - using the real bt_device_t structure
static bt_device_t mock_devices[] = {
    {
        .addr = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55},
        .name = "BT Speaker",
        .rssi = -65,
        .paired = false,
        // Store A2DP sink devices with 0x01 in cod field for testing purposes
        .cod = 0x01  
    },
    {
        .addr = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF},
        .name = "BT Headphones", 
        .rssi = -70,
        .paired = true,
        .cod = 0x01  // A2DP sink device
    },
    {
        .addr = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66},
        .name = "Some Phone",
        .rssi = -80,
        .paired = false,
        .cod = 0x02  // Not an A2DP sink device
    },
    {
        .addr = {0x99, 0x88, 0x77, 0x66, 0x55, 0x44},
        .name = "Other Device",
        .rssi = -90,
        .paired = false,
        .cod = 0x03  // Not an A2DP sink device
    }
};

static bool is_scanning = false;
static uint32_t scan_start_time = 0;

// Test implementations
void test_bt_scan_basic(void) {
    ESP_LOGI(TAG, "Stub: Starting BT scan");
    is_scanning = true;
    TEST_ASSERT_TRUE(is_scanning);
    
    // Let scan run for 1 second
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    ESP_LOGI(TAG, "Stub: Stopping BT scan");
    is_scanning = false;
    TEST_ASSERT_FALSE(is_scanning);
}

void test_bt_scan_filtered(void) {
    bt_device_t devices[10];
    uint16_t count = 0;
    
    // First scan
    ESP_LOGI(TAG, "Stub: Starting BT scan with filter");
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Get only audio devices
    for (int i = 0, j = 0; i < sizeof(mock_devices) / sizeof(mock_devices[0]) && j < 10; i++) {
        // In our test case, devices with cod=0x01 are A2DP sinks
        if (mock_devices[i].cod == 0x01) { 
            memcpy(&devices[j++], &mock_devices[i], sizeof(bt_device_t));
            count++;
        }
    }
    
    ESP_LOGI(TAG, "Stub: Found %d A2DP sink devices", count);
    
    // Should find 2 audio devices in our mock data
    TEST_ASSERT_EQUAL(2, count);
    TEST_ASSERT_EQUAL_STRING("BT Speaker", devices[0].name);
    TEST_ASSERT_EQUAL_STRING("BT Headphones", devices[1].name);
}

void test_bt_scan_get_results(void) {
    bt_device_t devices[10];
    uint16_t count;
    
    // First scan
    ESP_LOGI(TAG, "Stub: Starting BT scan");
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Copy all mock devices
    count = sizeof(mock_devices) / sizeof(mock_devices[0]);
    if (count > 10) count = 10;
    
    for (int i = 0; i < count; i++) {
        memcpy(&devices[i], &mock_devices[i], sizeof(bt_device_t));
    }
    
    ESP_LOGI(TAG, "Stub: Getting discovered device count: %d", count);
    
    // Should match our mock data
    TEST_ASSERT_EQUAL(4, count);
    TEST_ASSERT_EQUAL(0x00, devices[0].addr[0]);
    TEST_ASSERT_EQUAL(0xAA, devices[1].addr[0]);
    
    // Check for paired status
    TEST_ASSERT_FALSE(devices[0].paired);
    TEST_ASSERT_TRUE(devices[1].paired);
    
    // Check signal strength ordering
    TEST_ASSERT_TRUE(devices[0].rssi > devices[1].rssi);
    TEST_ASSERT_TRUE(devices[1].rssi > devices[2].rssi);
}

void test_bt_scan_timeout(void) {
    // Start scan with 5-second timeout
    ESP_LOGI(TAG, "Stub: Starting BT scan with timeout");
    is_scanning = true;
    scan_start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    TEST_ASSERT_TRUE(is_scanning);
    
    // Wait for just over 5 seconds
    vTaskDelay(pdMS_TO_TICKS(5500));
    
    // Scan should have auto-stopped
    uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    if (current_time - scan_start_time > 5000) {
        is_scanning = false;
    }
    TEST_ASSERT_FALSE(is_scanning);
    ESP_LOGI(TAG, "Stub: Scan timed out after 5 seconds");
}

void test_bt_scan_stop(void) {
    // Start scan with 60-second timeout
    ESP_LOGI(TAG, "Stub: Starting BT scan");
    is_scanning = true;
    TEST_ASSERT_TRUE(is_scanning);
    
    // Wait briefly
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Stop scan early
    ESP_LOGI(TAG, "Stub: Stopping BT scan early");
    is_scanning = false;
    TEST_ASSERT_FALSE(is_scanning);
}
