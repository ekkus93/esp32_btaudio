#include "unity.h"
#include "bt_source.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "BT_CONN_TEST";

// Mock connection event callback structure
typedef struct {
    bool connect_event_received;
    bool disconnect_event_received;
    bt_device_t connected_device;
    esp_err_t last_error;
} conn_event_data_t;

// Connection state change callback function
static void test_connection_callback(bool connected, bt_device_t *device, esp_err_t status, void *user_data) {
    conn_event_data_t *data = (conn_event_data_t *)user_data;
    if (data != NULL) {
        if (connected) {
            data->connect_event_received = true;
            if (device != NULL) {
                memcpy(&data->connected_device, device, sizeof(bt_device_t));
            }
        } else {
            data->disconnect_event_received = true;
        }
        data->last_error = status;
    }
}

// Test: Connect to a device by address
void test_bt_connect_by_address(void) {
    ESP_LOGI(TAG, "Testing connection by address");
    
    const char* test_addr = "AA:BB:CC:DD:EE:FF";
    conn_event_data_t event_data = {0};
    
    // Register connection callback
    esp_err_t err = bt_register_connection_callback(test_connection_callback, &event_data);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    
    // Connect to the device
    err = bt_connect(test_addr);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    
    // Wait for connection process
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Verify connection status
    bool is_connected = bt_is_connected();
    TEST_ASSERT_TRUE(is_connected);
    
    // Verify connection callback was triggered
    TEST_ASSERT_TRUE(event_data.connect_event_received);
    TEST_ASSERT_EQUAL(ESP_OK, event_data.last_error);
    
    // Clean up
    err = bt_disconnect();
    TEST_ASSERT_EQUAL(ESP_OK, err);
    vTaskDelay(pdMS_TO_TICKS(500));
}

// Test: Connect to a device by name
void test_bt_connect_by_name(void) {
    ESP_LOGI(TAG, "Testing connection by name");
    
    const char* test_name = "BT Headphones";
    conn_event_data_t event_data = {0};
    
    // Register connection callback
    esp_err_t err = bt_register_connection_callback(test_connection_callback, &event_data);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    
    // First scan to find the device
    err = bt_scan_start();
    TEST_ASSERT_EQUAL(ESP_OK, err);
    vTaskDelay(pdMS_TO_TICKS(2000));
    err = bt_scan_stop();
    TEST_ASSERT_EQUAL(ESP_OK, err);
    
    // Connect by name
    err = bt_connect_by_name(test_name);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    
    // Wait for connection process
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Verify connection status
    bool is_connected = bt_is_connected();
    TEST_ASSERT_TRUE(is_connected);
    
    // Verify connection callback was triggered
    TEST_ASSERT_TRUE(event_data.connect_event_received);
    TEST_ASSERT_EQUAL(ESP_OK, event_data.last_error);
    
    // Clean up
    err = bt_disconnect();
    TEST_ASSERT_EQUAL(ESP_OK, err);
    vTaskDelay(pdMS_TO_TICKS(500));
}

// Test: Handle connection failure
void test_bt_connect_failure(void) {
    ESP_LOGI(TAG, "Testing connection failure handling");
    
    const char* invalid_addr = "ZZ:ZZ:ZZ:ZZ:ZZ:ZZ"; // Invalid address format
    conn_event_data_t event_data = {0};
    
    // Register connection callback
    esp_err_t err = bt_register_connection_callback(test_connection_callback, &event_data);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    
    // Attempt to connect to invalid device
    err = bt_connect(invalid_addr);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, err);
    
    // Wait for potential connection attempt
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Verify not connected
    bool is_connected = bt_is_connected();
    TEST_ASSERT_FALSE(is_connected);
    
    // Verify error was propagated to callback
    TEST_ASSERT_NOT_EQUAL(ESP_OK, event_data.last_error);
}

// Test: Connection timeout
void test_bt_connect_timeout(void) {
    ESP_LOGI(TAG, "Testing connection timeout");
    
    // This address should be valid format but unreachable
    const char* unreachable_addr = "11:22:33:44:55:66";
    conn_event_data_t event_data = {0};
    
    // Register connection callback
    esp_err_t err = bt_register_connection_callback(test_connection_callback, &event_data);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    
    // Attempt connection with timeout
    err = bt_connect_with_timeout(unreachable_addr, 3000); // 3 second timeout
    TEST_ASSERT_EQUAL(ESP_OK, err); // Initial API call should succeed
    
    // Wait for timeout period
    vTaskDelay(pdMS_TO_TICKS(4000)); // Wait longer than timeout
    
    // Connection should have timed out
    bool is_connected = bt_is_connected();
    TEST_ASSERT_FALSE(is_connected);
    
    // Verify error was propagated to callback
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, event_data.last_error);
}

// Test: Get connection status information
void test_bt_connection_info(void) {
    ESP_LOGI(TAG, "Testing connection status information");
    
    const char* test_addr = "AA:BB:CC:DD:EE:FF";
    
    // First ensure disconnected
    bt_disconnect();
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Get connection info before connecting
    bt_connection_info_t info;
    esp_err_t err = bt_get_connection_info(&info);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_FALSE(info.connected);
    
    // Connect to device
    err = bt_connect(test_addr);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Get connection info after connecting
    err = bt_get_connection_info(&info);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(info.connected);
    TEST_ASSERT_EQUAL_STRING(test_addr, info.remote_addr);
    
    // Clean up
    err = bt_disconnect();
    TEST_ASSERT_EQUAL(ESP_OK, err);
    vTaskDelay(pdMS_TO_TICKS(500));
}

// Test: Auto-reconnection
void test_bt_auto_reconnect(void) {
    ESP_LOGI(TAG, "Testing auto-reconnection");
    
    const char* test_addr = "AA:BB:CC:DD:EE:FF";
    conn_event_data_t event_data = {0};
    
    // Register callback
    esp_err_t err = bt_register_connection_callback(test_connection_callback, &event_data);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    
    // Enable auto-reconnect
    err = bt_set_auto_reconnect(true);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    
    // Connect to the device initially
    err = bt_connect(test_addr);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Verify initial connection
    bool is_connected = bt_is_connected();
    TEST_ASSERT_TRUE(is_connected);
    
    // Reset event data
    memset(&event_data, 0, sizeof(conn_event_data_t));
    
    // Simulate connection drop (internal mock call)
    err = bt_simulate_disconnect();
    TEST_ASSERT_EQUAL(ESP_OK, err);
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Verify disconnect was detected
    TEST_ASSERT_TRUE(event_data.disconnect_event_received);
    
    // Wait for auto-reconnect
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // Verify reconnection occurred
    is_connected = bt_is_connected();
    TEST_ASSERT_TRUE(is_connected);
    TEST_ASSERT_TRUE(event_data.connect_event_received);
    
    // Clean up
    err = bt_set_auto_reconnect(false);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    
    err = bt_disconnect();
    TEST_ASSERT_EQUAL(ESP_OK, err);
    vTaskDelay(pdMS_TO_TICKS(500));
}
