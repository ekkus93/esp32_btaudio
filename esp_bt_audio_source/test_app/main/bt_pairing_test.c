/**
 * @file bt_pairing_test.c
 * @brief Bluetooth pairing functionality tests
 * 
 * This file implements tests for the real Bluetooth pairing functionality,
 * including PIN-based pairing, SSP, and pairing management.
 */

#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "bt_source.h"
#include "bt_mock_devices.h"  // For hardware-dependent tests

static const char *TAG = "BT_PAIRING_TEST";

// Helper function to setup test environment
static void pairing_test_setup(void) {
    esp_err_t ret = bt_init();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    vTaskDelay(pdMS_TO_TICKS(100)); // Allow time for BT stack to initialize
}

// Helper function for cleanup
static void pairing_test_cleanup(void) {
    // Clean up any test state
    bt_disconnect();
    bt_scan_stop();
    vTaskDelay(pdMS_TO_TICKS(100)); // Allow time for cleanup
}

// Test #45: PIN-based pairing initiation
void test_pin_pairing_initiation(void) {
    ESP_LOGI(TAG, "Testing PIN pairing initiation");
    
    // Setup
    pairing_test_setup();
    
    // Test: For hardware-dependent tests, we need a hybrid approach
    // First, verify the real API using mock devices
    bt_mock_reset();
    bt_mock_add_device("11:22:33:44:55:66", "Test Device", BT_DEVICE_TYPE_AUDIO, false);
    
    // Start pairing
    esp_err_t ret = bt_start_pairing("11:22:33:44:55:66");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Check pairing state
    bt_pairing_state_t state = bt_get_pairing_state();
    TEST_ASSERT_EQUAL(BT_PAIRING_STATE_PIN_REQUESTED, state);
    
    // Clean up
    pairing_test_cleanup();
}

// Test #46: PIN-based pairing successful completion
void test_pin_pairing_success(void) {
    ESP_LOGI(TAG, "Testing PIN pairing success");
    
    // Setup
    pairing_test_setup();
    
    // Use mock for this hardware-dependent test
    bt_mock_reset();
    bt_mock_add_device("11:22:33:44:55:66", "Test Device", BT_DEVICE_TYPE_AUDIO, false);
    
    // Start pairing
    esp_err_t ret = bt_start_pairing("11:22:33:44:55:66");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Send PIN code
    ret = bt_send_pin_code("1234");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Wait briefly for pairing to complete
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Check pairing state
    bt_pairing_state_t state = bt_get_pairing_state();
    TEST_ASSERT_EQUAL(BT_PAIRING_STATE_COMPLETE, state);
    
    // Check device is paired
    bool is_paired = bt_is_device_paired("11:22:33:44:55:66");
    TEST_ASSERT_TRUE(is_paired);
    
    // Clean up
    pairing_test_cleanup();
}

// Test #47: PIN-based pairing failure handling
void test_pin_pairing_failure(void) {
    ESP_LOGI(TAG, "Testing PIN pairing failure");
    
    // Setup
    pairing_test_setup();
    
    // Use mock for this hardware-dependent test
    bt_mock_reset();
    bt_mock_add_device("11:22:33:44:55:66", "Test Device", BT_DEVICE_TYPE_AUDIO, false);
    
    // Start pairing
    esp_err_t ret = bt_start_pairing("11:22:33:44:55:66");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Simulate PIN failure
    bt_mock_simulate_pin_failure();
    
    // Try pairing with incorrect PIN
    ret = bt_send_pin_code("9999");  // Wrong PIN
    TEST_ASSERT_NOT_EQUAL(ESP_OK, ret);
    
    // Check pairing state
    bt_pairing_state_t state = bt_get_pairing_state();
    TEST_ASSERT_EQUAL(BT_PAIRING_STATE_FAILED, state);
    
    // Device should not be paired
    bool is_paired = bt_is_device_paired("11:22:33:44:55:66");
    TEST_ASSERT_FALSE(is_paired);
    
    // Clean up
    pairing_test_cleanup();
}

// Test #48: PIN-based pairing timeout handling
void test_pin_pairing_timeout(void) {
    ESP_LOGI(TAG, "Testing PIN pairing timeout");
    
    // Setup
    pairing_test_setup();
    
    // Use mock for this hardware-dependent test
    bt_mock_reset();
    bt_mock_add_device("11:22:33:44:55:66", "Test Device", BT_DEVICE_TYPE_AUDIO, false);
    
    // Start pairing
    esp_err_t ret = bt_start_pairing("11:22:33:44:55:66");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Simulate pairing timeout
    bt_mock_simulate_pairing_timeout();
    
    // Check pairing state
    bt_pairing_state_t state = bt_get_pairing_state();
    TEST_ASSERT_EQUAL(BT_PAIRING_STATE_TIMEOUT, state);
    
    // Clean up
    pairing_test_cleanup();
}

// Test #49: Setting and retrieving default PIN
void test_set_default_pin(void) {
    ESP_LOGI(TAG, "Testing default PIN setting");
    
    // Setup
    pairing_test_setup();
    
    // Set default PIN to a custom value
    esp_err_t ret = bt_set_default_pin("9876");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Get the default PIN
    char pin[16];
    ret = bt_get_default_pin(pin, sizeof(pin));
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Verify PIN value
    TEST_ASSERT_EQUAL_STRING("9876", pin);
    
    // Reset PIN to standard value for other tests
    ret = bt_set_default_pin("1234");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Clean up
    pairing_test_cleanup();
}

// Test #50: SSP confirmation request
void test_ssp_confirmation_request(void) {
    ESP_LOGI(TAG, "Testing SSP confirmation request");
    
    // Setup
    pairing_test_setup();
    
    // Use mock for this hardware-dependent test
    bt_mock_reset();
    bt_mock_add_device("11:22:33:44:55:66", "Test Device", BT_DEVICE_TYPE_AUDIO, false);
    bt_mock_set_ssp_supported(true);
    
    // Start pairing
    esp_err_t ret = bt_start_pairing("11:22:33:44:55:66");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Simulate SSP request
    bt_mock_simulate_ssp_request(123456);
    
    // Check pairing state
    bt_pairing_state_t state = bt_get_pairing_state();
    TEST_ASSERT_EQUAL(BT_PAIRING_STATE_SSP_CONFIRM, state);
    
    // Check pairing method
    bt_pairing_method_t method = bt_get_pairing_method();
    TEST_ASSERT_EQUAL(BT_PAIRING_SSP, method);
    
    // Get SSP passkey and verify
    char passkey[7];
    ret = bt_get_ssp_passkey(passkey, sizeof(passkey));
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL_STRING("123456", passkey);
    
    // Check if confirmation is requested
    bool confirm_requested = bt_is_ssp_confirm_requested();
    TEST_ASSERT_TRUE(confirm_requested);
    
    // Clean up
    pairing_test_cleanup();
}

// Test #51: SSP confirmation accepted
void test_ssp_confirmation_accepted(void) {
    ESP_LOGI(TAG, "Testing SSP confirmation accepted");
    
    // Setup
    pairing_test_setup();
    
    // Use mock for this hardware-dependent test
    bt_mock_reset();
    bt_mock_add_device("11:22:33:44:55:66", "Test Device", BT_DEVICE_TYPE_AUDIO, false);
    bt_mock_set_ssp_supported(true);
    
    // Start pairing
    esp_err_t ret = bt_start_pairing("11:22:33:44:55:66");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Simulate SSP request
    bt_mock_simulate_ssp_request(123456);
    
    // Accept the SSP confirmation
    ret = bt_ssp_confirm(true);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Wait briefly for pairing to complete
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Verify pairing state
    bt_pairing_state_t state = bt_get_pairing_state();
    TEST_ASSERT_EQUAL(BT_PAIRING_STATE_COMPLETE, state);
    
    // Check device is paired
    bool is_paired = bt_is_device_paired("11:22:33:44:55:66");
    TEST_ASSERT_TRUE(is_paired);
    
    // Clean up
    pairing_test_cleanup();
}

// Test #52: SSP confirmation rejected
void test_ssp_confirmation_rejected(void) {
    ESP_LOGI(TAG, "Testing SSP confirmation rejected");
    
    // Setup
    pairing_test_setup();
    
    // Use mock for this hardware-dependent test
    bt_mock_reset();
    bt_mock_add_device("11:22:33:44:55:66", "Test Device", BT_DEVICE_TYPE_AUDIO, false);
    bt_mock_set_ssp_supported(true);
    
    // Start pairing
    esp_err_t ret = bt_start_pairing("11:22:33:44:55:66");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Simulate SSP request
    bt_mock_simulate_ssp_request(123456);
    
    // Reject the SSP confirmation
    ret = bt_ssp_confirm(false);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Verify pairing state
    bt_pairing_state_t state = bt_get_pairing_state();
    TEST_ASSERT_EQUAL(BT_PAIRING_STATE_FAILED, state);
    
    // Check device is not paired
    bool is_paired = bt_is_device_paired("11:22:33:44:55:66");
    TEST_ASSERT_FALSE(is_paired);
    
    // Clean up
    pairing_test_cleanup();
}

// Test #53: SSP fallback to PIN
void test_ssp_fallback_to_pin(void) {
    ESP_LOGI(TAG, "Testing SSP fallback to PIN");
    
    // Setup
    pairing_test_setup();
    
    // Use mock for this hardware-dependent test - disable SSP to test fallback
    bt_mock_reset();
    bt_mock_add_device("11:22:33:44:55:66", "Test Device", BT_DEVICE_TYPE_AUDIO, false);
    bt_mock_set_ssp_supported(false);
    
    // Start pairing
    esp_err_t ret = bt_start_pairing("11:22:33:44:55:66");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Check that we fall back to PIN pairing
    bt_pairing_state_t state = bt_get_pairing_state();
    TEST_ASSERT_EQUAL(BT_PAIRING_STATE_PIN_REQUESTED, state);
    
    bt_pairing_method_t method = bt_get_pairing_method();
    TEST_ASSERT_EQUAL(BT_PAIRING_PIN, method);
    
    // Complete PIN pairing
    ret = bt_send_pin_code("1234");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Verify pairing completed
    state = bt_get_pairing_state();
    TEST_ASSERT_EQUAL(BT_PAIRING_STATE_COMPLETE, state);
    
    // Clean up
    pairing_test_cleanup();
}

// Test #54: Unpairing a specific device
void test_unpair_specific_device(void) {
    ESP_LOGI(TAG, "Testing unpairing a specific device");
    
    // Setup
    pairing_test_setup();
    
    // Use mock for hardware-dependent test
    bt_mock_reset();
    bt_mock_add_device("11:22:33:44:55:66", "Test Device", BT_DEVICE_TYPE_AUDIO, true);
    
    // Verify device starts as paired
    bool is_paired = bt_is_device_paired("11:22:33:44:55:66");
    TEST_ASSERT_TRUE(is_paired);
    
    // Get initial paired device count
    uint16_t initial_count = bt_get_paired_device_count();
    
    // Unpair the device
    esp_err_t ret = bt_unpair_device("11:22:33:44:55:66");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Verify device is no longer paired
    is_paired = bt_is_device_paired("11:22:33:44:55:66");
    TEST_ASSERT_FALSE(is_paired);
    
    // Verify paired device count decreased
    uint16_t new_count = bt_get_paired_device_count();
    TEST_ASSERT_EQUAL(initial_count - 1, new_count);
    
    // Clean up
    pairing_test_cleanup();
}

// Test #55: Unpairing all devices
void test_unpair_all_devices(void) {
    ESP_LOGI(TAG, "Testing unpairing all devices");
    
    // Setup
    pairing_test_setup();
    
    // Add multiple paired devices using mock
    bt_mock_reset();
    bt_mock_add_device("11:22:33:44:55:66", "Test Device 1", BT_DEVICE_TYPE_AUDIO, true);
    bt_mock_add_device("AA:BB:CC:DD:EE:FF", "Test Device 2", BT_DEVICE_TYPE_AUDIO, true);
    
    // Verify we have some paired devices
    uint16_t initial_count = bt_get_paired_device_count();
    TEST_ASSERT_GREATER_THAN(0, initial_count);
    
    // Unpair all devices
    esp_err_t ret = bt_unpair_all_devices();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Verify no devices remain paired
    uint16_t new_count = bt_get_paired_device_count();
    TEST_ASSERT_EQUAL(0, new_count);
    
    // Verify individual devices are unpaired
    TEST_ASSERT_FALSE(bt_is_device_paired("11:22:33:44:55:66"));
    TEST_ASSERT_FALSE(bt_is_device_paired("AA:BB:CC:DD:EE:FF"));
    
    // Clean up
    pairing_test_cleanup();
}

// Test #56: Paired devices persistence
void test_paired_devices_stored(void) {
    ESP_LOGI(TAG, "Testing paired devices storage");
    
    // Setup
    pairing_test_setup();
    
    // Add a paired device using mock
    bt_mock_reset();
    bt_mock_add_device("11:22:33:44:55:66", "Test Device", BT_DEVICE_TYPE_AUDIO, true);
    
    // Store paired devices
    esp_err_t ret = bt_store_paired_devices();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Reset BT state to simulate system restart
    bt_mock_reset();
    
    // Load paired devices
    ret = bt_load_paired_devices();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Verify the device is still paired after loading
    bool is_paired = bt_is_device_paired("11:22:33:44:55:66");
    TEST_ASSERT_TRUE(is_paired);
    
    // Clean up
    pairing_test_cleanup();
}

// Test #57: Paired devices retrieval
void test_paired_devices_retrieval(void) {
    ESP_LOGI(TAG, "Testing paired devices retrieval");
    
    // Setup
    pairing_test_setup();
    
    // Add multiple paired devices using mock
    bt_mock_reset();
    bt_mock_add_device("11:22:33:44:55:66", "Test Speaker 1", BT_DEVICE_TYPE_AUDIO, true);
    bt_mock_add_device("AA:BB:CC:DD:EE:FF", "Test Speaker 2", BT_DEVICE_TYPE_AUDIO, true);
    
    // Get paired device count
    uint16_t count = bt_get_paired_device_count();
    TEST_ASSERT_EQUAL(2, count);
    
    // Get paired devices
    bt_device_t devices[5];
    int num_devices = bt_get_paired_devices(devices, 5);
    TEST_ASSERT_EQUAL(2, num_devices);
    
    // Verify device details
    bool found_device1 = false;
    bool found_device2 = false;
    
    for (int i = 0; i < num_devices; i++) {
        char addr[18];
        sprintf(addr, "%02x:%02x:%02x:%02x:%02x:%02x",
                devices[i].addr[0], devices[i].addr[1],
                devices[i].addr[2], devices[i].addr[3],
                devices[i].addr[4], devices[i].addr[5]);
        
        if (strcasecmp(addr, "11:22:33:44:55:66") == 0) {
            TEST_ASSERT_EQUAL_STRING("Test Speaker 1", devices[i].name);
            found_device1 = true;
        } else if (strcasecmp(addr, "AA:BB:CC:DD:EE:FF") == 0) {
            TEST_ASSERT_EQUAL_STRING("Test Speaker 2", devices[i].name);
            found_device2 = true;
        }
    }
    
    TEST_ASSERT_TRUE(found_device1);
    TEST_ASSERT_TRUE(found_device2);
    
    // Clean up
    pairing_test_cleanup();
}

// Test #58: Paired device connection info
void test_paired_device_connection_info(void) {
    ESP_LOGI(TAG, "Testing paired device connection info");
    
    // Setup
    pairing_test_setup();
    
    // Add paired device using mock
    bt_mock_reset();
    bt_mock_add_device("11:22:33:44:55:66", "Test Speaker", BT_DEVICE_TYPE_AUDIO, true);
    
    // Connect to the device
    esp_err_t ret = bt_connect("11:22:33:44:55:66");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Get connection info
    bt_connection_info_t conn_info;
    ret = bt_get_paired_device_info("11:22:33:44:55:66", &conn_info);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Verify connection info
    TEST_ASSERT_TRUE(conn_info.connected);
    TEST_ASSERT_EQUAL_STRING("11:22:33:44:55:66", conn_info.remote_addr);
    TEST_ASSERT_EQUAL_STRING("Test Speaker", conn_info.remote_name);
    
    // Clean up
    pairing_test_cleanup();
}

// Test #59: Unpairing a nonexistent device
void test_unpair_nonexistent_device(void) {
    ESP_LOGI(TAG, "Testing unpairing a nonexistent device");
    
    // Setup
    pairing_test_setup();
    
    // Unpair a device that doesn't exist
    esp_err_t ret = bt_unpair_device("00:11:22:33:44:55");
    
    // Should return not found error
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, ret);
    
    // Clean up
    pairing_test_cleanup();
}

// Test #60: Unpairing a connected device
void test_unpair_connected_device(void) {
    ESP_LOGI(TAG, "Testing unpairing a connected device");
    
    // Setup
    pairing_test_setup();
    
    // Add paired device and connect using mock
    bt_mock_reset();
    bt_mock_add_device("11:22:33:44:55:66", "Test Speaker", BT_DEVICE_TYPE_AUDIO, true);
    
    // Connect to the device
    esp_err_t ret = bt_connect("11:22:33:44:55:66");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    vTaskDelay(pdMS_TO_TICKS(100));
    TEST_ASSERT_TRUE(bt_is_connected());
    
    // Unpair the device while connected
    ret = bt_unpair_device("11:22:33:44:55:66");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Device should be unpaired
    TEST_ASSERT_FALSE(bt_is_device_paired("11:22:33:44:55:66"));
    
    // Connection should be terminated
    TEST_ASSERT_FALSE(bt_is_connected());
    
    // Clean up
    pairing_test_cleanup();
}

// Test #61: Unpairing with invalid address
void test_unpair_invalid_address(void) {
    ESP_LOGI(TAG, "Testing unpairing with invalid address");
    
    // Setup
    pairing_test_setup();
    
    // Test with NULL address
    esp_err_t ret = bt_unpair_device(NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
    
    // Test with malformed address
    ret = bt_unpair_device("NOT:A:VALID:MAC:ADDRESS");
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
    
    // Test with incomplete address
    ret = bt_unpair_device("11:22:33:44");
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
    
    // Clean up
    pairing_test_cleanup();
}

// Test #62: Unpair persistence
void test_unpair_persistence(void) {
    ESP_LOGI(TAG, "Testing unpair persistence");
    
    // Setup
    pairing_test_setup();
    
    // Add multiple paired devices using mock
    bt_mock_reset();
    bt_mock_add_device("11:22:33:44:55:66", "Device 1", BT_DEVICE_TYPE_AUDIO, true);
    bt_mock_add_device("AA:BB:CC:DD:EE:FF", "Device 2", BT_DEVICE_TYPE_AUDIO, true);
    
    // Check initial paired device count
    uint16_t initial_count = bt_get_paired_device_count();
    TEST_ASSERT_EQUAL(2, initial_count);
    
    // Unpair one device
    esp_err_t ret = bt_unpair_device("11:22:33:44:55:66");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Store paired devices
    ret = bt_store_paired_devices();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Reset BT state to simulate system restart
    bt_mock_reset();
    
    // Load paired devices
    ret = bt_load_paired_devices();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Check that only one device is paired after loading
    uint16_t final_count = bt_get_paired_device_count();
    TEST_ASSERT_EQUAL(1, final_count);
    
    // Verify which device is still paired
    TEST_ASSERT_FALSE(bt_is_device_paired("11:22:33:44:55:66"));
    TEST_ASSERT_TRUE(bt_is_device_paired("AA:BB:CC:DD:EE:FF"));
    
    // Clean up
    pairing_test_cleanup();
}

// Test #63: Unpair all when no devices are paired
void test_unpair_all_when_none_paired(void) {
    ESP_LOGI(TAG, "Testing unpair all when no devices paired");
    
    // Setup
    pairing_test_setup();
    
    // Reset to ensure no paired devices
    bt_mock_reset();
    
    // Verify initial state has no paired devices
    uint16_t initial_count = bt_get_paired_device_count();
    TEST_ASSERT_EQUAL(0, initial_count);
    
    // Unpair all devices
    esp_err_t ret = bt_unpair_all_devices();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Count should still be zero
    uint16_t final_count = bt_get_paired_device_count();
    TEST_ASSERT_EQUAL(0, final_count);
    
    // Clean up
    pairing_test_cleanup();
}

// Test #64: Unpair all with connected devices
void test_unpair_all_with_connected_devices(void) {
    ESP_LOGI(TAG, "Testing unpair all with connected devices");
    
    // Setup
    pairing_test_setup();
    
    // Add paired device and connect using mock
    bt_mock_reset();
    bt_mock_add_device("11:22:33:44:55:66", "Test Device", BT_DEVICE_TYPE_AUDIO, true);
    
    // Connect to device
    esp_err_t ret = bt_connect("11:22:33:44:55:66");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    vTaskDelay(pdMS_TO_TICKS(100));
    TEST_ASSERT_TRUE(bt_is_connected());
    
    // Unpair all devices
    ret = bt_unpair_all_devices();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // No devices should be paired
    uint16_t count = bt_get_paired_device_count();
    TEST_ASSERT_EQUAL(0, count);
    
    // Connection should be terminated
    TEST_ASSERT_FALSE(bt_is_connected());
    
    // Clean up
    pairing_test_cleanup();
}

// Test #65: Unpair all persistence
void test_unpair_all_persistence(void) {
    ESP_LOGI(TAG, "Testing unpair all persistence");
    
    // Setup
    pairing_test_setup();
    
    // Add multiple paired devices using mock
    bt_mock_reset();
    bt_mock_add_device("11:22:33:44:55:66", "Device 1", BT_DEVICE_TYPE_AUDIO, true);
    bt_mock_add_device("AA:BB:CC:DD:EE:FF", "Device 2", BT_DEVICE_TYPE_AUDIO, true);
    
    // Check initial count
    uint16_t initial_count = bt_get_paired_device_count();
    TEST_ASSERT_EQUAL(2, initial_count);
    
    // Unpair all devices
    esp_err_t ret = bt_unpair_all_devices();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Store paired devices
    ret = bt_store_paired_devices();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Reset BT state to simulate system restart
    bt_mock_reset();
    
    // Load paired devices
    ret = bt_load_paired_devices();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Verify no devices are paired after loading
    uint16_t final_count = bt_get_paired_device_count();
    TEST_ASSERT_EQUAL(0, final_count);
    
    // Clean up
    pairing_test_cleanup();
}

// Test #66: Unpair all with multiple devices
void test_unpair_all_multiple_devices(void) {
    ESP_LOGI(TAG, "Testing unpair all with multiple devices");
    
    // Setup
    pairing_test_setup();
    
    // Add multiple paired devices using mock (5 devices)
    bt_mock_reset();
    bt_mock_add_device("11:22:33:44:55:66", "Device 1", BT_DEVICE_TYPE_AUDIO, true);
    bt_mock_add_device("AA:BB:CC:DD:EE:FF", "Device 2", BT_DEVICE_TYPE_AUDIO, true);
    bt_mock_add_device("12:34:56:78:9A:BC", "Device 3", BT_DEVICE_TYPE_AUDIO, true);
    bt_mock_add_device("FE:DC:BA:98:76:54", "Device 4", BT_DEVICE_TYPE_AUDIO, true);
    bt_mock_add_device("A1:B2:C3:D4:E5:F6", "Device 5", BT_DEVICE_TYPE_AUDIO, true);
    
    // Verify initial count
    uint16_t initial_count = bt_get_paired_device_count();
    TEST_ASSERT_EQUAL(5, initial_count);
    
    // Unpair all devices
    esp_err_t ret = bt_unpair_all_devices();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Verify final count is zero
    uint16_t final_count = bt_get_paired_device_count();
    TEST_ASSERT_EQUAL(0, final_count);
    
    // Check that none of the specific devices are still paired
    TEST_ASSERT_FALSE(bt_is_device_paired("11:22:33:44:55:66"));
    TEST_ASSERT_FALSE(bt_is_device_paired("AA:BB:CC:DD:EE:FF"));
    TEST_ASSERT_FALSE(bt_is_device_paired("12:34:56:78:9A:BC"));
    TEST_ASSERT_FALSE(bt_is_device_paired("FE:DC:BA:98:76:54"));
    TEST_ASSERT_FALSE(bt_is_device_paired("A1:B2:C3:D4:E5:F6"));
    
    // Clean up
    pairing_test_cleanup();
}

// Main entry point for BT pairing tests
void app_main_bt_pairing_tests(void) 
{
    ESP_LOGI(TAG, "Running Bluetooth pairing tests");
    
    UNITY_BEGIN();
    
    // PIN-based pairing tests (45-49)
    RUN_TEST(test_pin_pairing_initiation);
    RUN_TEST(test_pin_pairing_success);
    RUN_TEST(test_pin_pairing_failure);
    RUN_TEST(test_pin_pairing_timeout);
    RUN_TEST(test_set_default_pin);
    
    // SSP tests (50-53)
    RUN_TEST(test_ssp_confirmation_request);
    RUN_TEST(test_ssp_confirmation_accepted);
    RUN_TEST(test_ssp_confirmation_rejected);
    RUN_TEST(test_ssp_fallback_to_pin);
    
    // Device unpairing tests (54-55, 59-64)
    RUN_TEST(test_unpair_specific_device);
    RUN_TEST(test_unpair_all_devices);
    RUN_TEST(test_unpair_nonexistent_device);
    RUN_TEST(test_unpair_connected_device);
    RUN_TEST(test_unpair_invalid_address);
    RUN_TEST(test_unpair_all_when_none_paired);
    RUN_TEST(test_unpair_all_with_connected_devices);
    
    // Paired device management tests (56-58, 65-66)
    RUN_TEST(test_paired_devices_stored);
    RUN_TEST(test_paired_devices_retrieval);
    RUN_TEST(test_paired_device_connection_info);
    RUN_TEST(test_unpair_persistence);
    RUN_TEST(test_unpair_all_persistence);
    RUN_TEST(test_unpair_all_multiple_devices);
    
    UNITY_END();
    
    ESP_LOGI(TAG, "Bluetooth pairing tests completed");
}
