/**
 * @file bt_pairing_test.c
 * @brief Bluetooth pairing functionality tests
 * 
 * This file implements tests for the real Bluetooth pairing functionality,
 * including PIN-based pairing, SSP, and pairing management.
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "unity.h"
#include "bt_source.h"
#include "bt_mock_devices.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "BT_PAIRING_TEST";
static const char *PAIRING_TEST_TAG = "BT_PAIRING_TEST";

// Helper function to convert uint8_t array to string address
static void addr_to_str(uint8_t* addr, char* addr_str) {
    sprintf(addr_str, "%02x:%02x:%02x:%02x:%02x:%02x", 
            addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
}

// Helper function to create a bt_device_t from components
static bt_device_t create_device(uint8_t* addr, const char* name, bool supports_a2dp) {
    bt_device_t device;
    
    // Initialize the entire structure to zero first
    memset(&device, 0, sizeof(bt_device_t));
    
    // Copy address if valid
    if (addr != NULL) {
        memcpy(device.addr, addr, 6);
    }
    
    // Copy name if valid
    if (name != NULL) {
        strncpy(device.name, name, sizeof(device.name) - 1);
        device.name[sizeof(device.name) - 1] = '\0';
    }
    
    // Set any other fields that might exist in the structure
    // For example, if bt_device_t has a supports_a2dp field:
    // device.supports_a2dp = supports_a2dp;
    
    return device;
}

// Test functions
void test_pin_pairing_success(void) {
    ESP_LOGI(PAIRING_TEST_TAG, "Testing PIN pairing success");
    
    // Use bt_mock_devices for testing
    bt_mock_reset();
    
    // Create test data
    char addr_str[18];
    uint8_t addr[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    addr_to_str(addr, addr_str);
    
    // Test pairing using PIN method
    bt_mock_set_default_pin("1234");
    
    // Start pairing with the device
    esp_err_t ret = bt_start_pairing(addr_str);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Verify state is PIN_REQUESTED
    TEST_ASSERT_EQUAL(BT_PAIRING_STATE_PIN_REQUESTED, bt_mock_get_pairing_state());
    TEST_ASSERT_EQUAL(BT_PAIRING_METHOD_PIN, bt_mock_get_pairing_method());
    
    // Send PIN
    ret = bt_send_pin_code("1234");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Verify pairing was successful
    TEST_ASSERT_EQUAL(BT_PAIRING_STATE_PAIRED, bt_mock_get_pairing_state());
}

void test_pin_pairing_failure(void) {
    ESP_LOGI(PAIRING_TEST_TAG, "Testing PIN pairing failure");
    
    bt_mock_reset();
    
    // Create test data
    char addr_str[18];
    uint8_t addr[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    addr_to_str(addr, addr_str);
    
    // Start pairing
    esp_err_t ret = bt_start_pairing(addr_str);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Send wrong PIN
    ret = bt_send_pin_code("0000"); // This PIN is set to fail in the mock
    TEST_ASSERT_NOT_EQUAL(ESP_OK, ret);
    
    // Verify pairing failed
    TEST_ASSERT_EQUAL(BT_PAIRING_STATE_FAILED, bt_mock_get_pairing_state());
}

void test_pin_pairing_timeout(void) {
    ESP_LOGI(PAIRING_TEST_TAG, "Testing PIN pairing timeout");
    
    bt_mock_reset();
    
    // Create test data
    char addr_str[18];
    uint8_t addr[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    addr_to_str(addr, addr_str);
    
    // Start pairing
    esp_err_t ret = bt_start_pairing(addr_str);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Send timeout PIN
    ret = bt_send_pin_code("9999"); // This PIN is set to time out in the mock
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, ret);
    
    // Verify pairing timed out
    TEST_ASSERT_EQUAL(BT_PAIRING_STATE_TIMEOUT, bt_mock_get_pairing_state());
}

void test_set_default_pin(void) {
    ESP_LOGI(PAIRING_TEST_TAG, "Testing set default PIN");
    
    bt_mock_reset();
    
    // Set a new default PIN
    esp_err_t ret = bt_set_default_pin("5678");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Get the PIN back
    char pin[17];
    ret = bt_get_default_pin(pin, sizeof(pin));
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Verify PIN was set correctly
    TEST_ASSERT_EQUAL_STRING("5678", pin);
}

void test_ssp_confirmation_request(void) {
    ESP_LOGI(TAG, "Testing SSP confirmation request");
    
    // Fix the function name to match the actual implementation
    bt_mock_set_ssp_supported(true);
    
    // Remove the wrong function name here
    // bt_mock_set_ssp_support(true);
    
    bt_mock_reset();
    
    // Create test data
    char addr_str[18];
    uint8_t addr[6] = {0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
    addr_to_str(addr, addr_str);
    
    // Start pairing
    esp_err_t ret = bt_start_pairing(addr_str);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Force SSP request
    // Add required parameter to bt_mock_simulate_ssp_request calls
    bt_mock_simulate_ssp_request(123456);
    
    // Verify SSP confirmation request
    TEST_ASSERT_EQUAL(BT_PAIRING_STATE_SSP_REQUESTED, bt_mock_get_pairing_state());
    TEST_ASSERT_EQUAL(BT_PAIRING_METHOD_SSP, bt_mock_get_pairing_method());
    TEST_ASSERT_TRUE(bt_mock_is_ssp_confirm_requested());
    
    // Get passkey
    uint32_t passkey = bt_mock_get_ssp_passkey();
    TEST_ASSERT_NOT_EQUAL(0, passkey);
}

void test_ssp_confirmation_accepted(void) {
    ESP_LOGI(PAIRING_TEST_TAG, "Testing SSP confirmation accepted");
    
    bt_mock_reset();
    // Fix function name
    bt_mock_set_ssp_supported(true);
    
    // Create test data
    char addr_str[18];
    uint8_t addr[6] = {0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
    addr_to_str(addr, addr_str);
    
    // Start pairing
    esp_err_t ret = bt_start_pairing(addr_str);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Force SSP request
    // Add required parameter to bt_mock_simulate_ssp_request calls
    bt_mock_simulate_ssp_request(123456);
    
    // Accept confirmation
    ret = bt_ssp_confirm(true);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Verify pairing successful
    TEST_ASSERT_EQUAL(BT_PAIRING_STATE_PAIRED, bt_mock_get_pairing_state());
}

void test_ssp_confirmation_rejected(void) {
    ESP_LOGI(PAIRING_TEST_TAG, "Testing SSP confirmation rejected");
    
    bt_mock_reset();
    // Fix function name
    bt_mock_set_ssp_supported(true);
    
    // Create test data
    char addr_str[18];
    uint8_t addr[6] = {0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
    addr_to_str(addr, addr_str);
    
    // Start pairing
    esp_err_t ret = bt_start_pairing(addr_str);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Force SSP request
    // Add required parameter to bt_mock_simulate_ssp_request calls
    bt_mock_simulate_ssp_request(123456);
    
    // Reject confirmation
    ret = bt_ssp_confirm(false);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Verify pairing failed
    TEST_ASSERT_EQUAL(BT_PAIRING_STATE_FAILED, bt_mock_get_pairing_state());
}

void test_ssp_fallback_to_pin(void) {
    ESP_LOGI(PAIRING_TEST_TAG, "Testing SSP fallback to PIN");
    
    bt_mock_reset();
    // Fix function name
    bt_mock_set_ssp_supported(false); // Disable SSP support
    
    // Create test data
    char addr_str[18];
    uint8_t addr[6] = {0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
    addr_to_str(addr, addr_str);
    
    // Start pairing
    esp_err_t ret = bt_start_pairing(addr_str);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Should use PIN method
    TEST_ASSERT_EQUAL(BT_PAIRING_METHOD_PIN, bt_mock_get_pairing_method());
    TEST_ASSERT_EQUAL(BT_PAIRING_STATE_PIN_REQUESTED, bt_mock_get_pairing_state());
}

void test_unpair_specific_device(void) {
    ESP_LOGI(PAIRING_TEST_TAG, "Testing unpair specific device");
    
    bt_mock_reset();
    
    // Create test data
    uint8_t addr[6] = {0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    char addr_str[18];
    addr_to_str(addr, addr_str);
    
    // Add a paired device
    bt_device_t device = create_device(addr, "Test Device", true);
    bt_add_paired_device(&device);
    
    // Verify it's paired
    bool is_paired = bt_is_device_paired(addr_str);
    TEST_ASSERT_TRUE(is_paired);
    
    // Unpair it
    esp_err_t ret = bt_unpair_device(addr_str);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Verify it's no longer paired
    is_paired = bt_is_device_paired(addr_str);
    TEST_ASSERT_FALSE(is_paired);
}

void test_unpair_invalid_address(void) {
    ESP_LOGI(PAIRING_TEST_TAG, "Testing unpair invalid address");
    
    bt_mock_reset();
    
    // Try to unpair with an invalid address
    esp_err_t ret = bt_unpair_device("00:00:00:00:00:00");
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
}

void test_unpair_nonexistent_device(void) {
    ESP_LOGI(PAIRING_TEST_TAG, "Testing unpair nonexistent device");
    
    bt_mock_reset();
    
    // Try to unpair with a valid but nonexistent address
    esp_err_t ret = bt_unpair_device("AA:BB:CC:DD:EE:FF");
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, ret);
}

void test_paired_devices_stored(void) {
    ESP_LOGI(PAIRING_TEST_TAG, "Testing paired devices stored");
    
    // Clean state at the beginning of test
    bt_unpair_all_devices();
    
    // Verify no devices are paired
    TEST_ASSERT_EQUAL(0, bt_get_paired_device_count());
    
    // Add a paired device
    uint8_t addr[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    char addr_str[18];
    addr_to_str(addr, addr_str);
    
    bt_device_t device = create_device(addr, "Test Device", true);
    esp_err_t ret = bt_add_paired_device(&device);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Verify it's paired
    TEST_ASSERT_EQUAL(1, bt_get_paired_device_count());
    
    // Store paired devices
    ret = bt_store_paired_devices();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Reset paired devices
    bt_unpair_all_devices();
    TEST_ASSERT_EQUAL(0, bt_get_paired_device_count());
    
    // Load paired devices
    ret = bt_load_paired_devices();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Verify paired device was restored
    TEST_ASSERT_EQUAL(1, bt_get_paired_device_count());
    
    // Clean up after test
    bt_unpair_all_devices();
}

void test_paired_devices_retrieval(void) {
    ESP_LOGI(PAIRING_TEST_TAG, "Testing paired devices retrieval");
    
    // Use bt_unpair_all_devices() instead of bt_reset_paired_devices
    bt_unpair_all_devices();
    
    // Add two paired devices
    uint8_t addr1[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    uint8_t addr2[6] = {0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
    char addr1_str[18], addr2_str[18];
    addr_to_str(addr1, addr1_str);
    addr_to_str(addr2, addr2_str);
    
    bt_device_t device1 = create_device(addr1, "Test Speaker 1", true);
    bt_device_t device2 = create_device(addr2, "Test Speaker 2", true);
    bt_add_paired_device(&device1);
    bt_add_paired_device(&device2);
    
    // Get paired devices
    bt_device_t devices[5];
    int count = bt_get_paired_devices(devices, 5);
    
    // Verify two devices were returned
    TEST_ASSERT_EQUAL(2, count);
    
    // Verify device names
    bool found_device1 = false;
    bool found_device2 = false;
    
    for (int i = 0; i < count; i++) {
        char dev_addr[18];
        addr_to_str(devices[i].addr, dev_addr);
        
        if (strcmp(dev_addr, addr1_str) == 0) {
            found_device1 = true;
            TEST_ASSERT_EQUAL_STRING("Test Speaker 1", devices[i].name);
        } else if (strcmp(dev_addr, addr2_str) == 0) {
            found_device2 = true;
            TEST_ASSERT_EQUAL_STRING("Test Speaker 2", devices[i].name);
        }
    }
    
    TEST_ASSERT_TRUE(found_device1);
    TEST_ASSERT_TRUE(found_device2);
}

void test_paired_device_connection_info(void) {
    ESP_LOGI(PAIRING_TEST_TAG, "Testing paired device connection info");
    
    bt_mock_reset();
    
    // Add a paired device
    uint8_t addr[6] = {0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    char addr_str[18];
    addr_to_str(addr, addr_str);
    
    bt_device_t device = create_device(addr, "Test Speaker", true);
    bt_add_paired_device(&device);
    
    // Connect to it
    esp_err_t ret = bt_connect(addr_str);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Get connection info
    bt_connection_info_t info;
    ret = bt_get_connection_info(&info);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Verify info matches the device
    TEST_ASSERT_TRUE(info.connected);
    TEST_ASSERT_EQUAL_STRING(addr_str, info.remote_addr);
    TEST_ASSERT_EQUAL_STRING("Test Speaker", info.remote_name);
}

void test_unpair_all_devices(void) {
    ESP_LOGI(PAIRING_TEST_TAG, "Testing unpair all devices");
    
    bt_unpair_all_devices();
    
    // Add multiple paired devices
    uint8_t addr1[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    uint8_t addr2[6] = {0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
    
    bt_device_t device1 = create_device(addr1, "Device 1", true);
    bt_device_t device2 = create_device(addr2, "Device 2", true);
    bt_add_paired_device(&device1);
    bt_add_paired_device(&device2);
    
    // Verify paired device count
    int count = bt_get_paired_device_count();
    TEST_ASSERT_EQUAL(2, count);
    
    // Unpair all devices
    esp_err_t ret = bt_unpair_all_devices();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Verify no devices are paired
    count = bt_get_paired_device_count();
    TEST_ASSERT_EQUAL(0, count);
}

void test_unpair_all_when_none_paired(void) {
    ESP_LOGI(PAIRING_TEST_TAG, "Testing unpair all when none paired");
    
    bt_unpair_all_devices();
    
    // Verify no devices are paired
    int count = bt_get_paired_device_count();
    TEST_ASSERT_EQUAL(0, count);
    
    // Unpair all devices
    esp_err_t ret = bt_unpair_all_devices();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Verify still no devices are paired
    count = bt_get_paired_device_count();
    TEST_ASSERT_EQUAL(0, count);
}

void test_unpair_connected_device(void) {
    ESP_LOGI(PAIRING_TEST_TAG, "Testing unpair connected device");
    
    bt_mock_reset();
    
    // Add a paired device
    uint8_t addr[6] = {0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    char addr_str[18];
    addr_to_str(addr, addr_str);
    
    bt_device_t device = create_device(addr, "Test Speaker", true);
    bt_add_paired_device(&device);
    esp_err_t ret = bt_connect(addr_str);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Verify we're connected
    TEST_ASSERT_TRUE(bt_is_connected());
    
    // Unpair the connected device
    ret = bt_unpair_device(addr_str);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Verify we're no longer connected
    TEST_ASSERT_FALSE(bt_is_connected());
    
    // Verify it's no longer paired
    bool is_paired = bt_is_device_paired(addr_str);
    TEST_ASSERT_FALSE(is_paired);
}

void test_unpair_persistence(void) {
    ESP_LOGI(PAIRING_TEST_TAG, "Testing unpair persistence");
    
    bt_unpair_all_devices();
    
    // Add two paired devices
    uint8_t addr1[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    uint8_t addr2[6] = {0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
    char addr1_str[18], addr2_str[18];
    addr_to_str(addr1, addr1_str);
    addr_to_str(addr2, addr2_str);
    
    bt_device_t device1 = create_device(addr1, "Device 1", true);
    bt_device_t device2 = create_device(addr2, "Device 2", true);
    bt_add_paired_device(&device1);
    bt_add_paired_device(&device2);
    
    // Store paired devices
    esp_err_t ret = bt_store_paired_devices();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Unpair one device
    ret = bt_unpair_device(addr1_str);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Verify only one device is paired
    TEST_ASSERT_EQUAL(1, bt_get_paired_device_count());
    
    // Store paired devices again
    ret = bt_store_paired_devices();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Reset paired devices
    bt_unpair_all_devices();
    TEST_ASSERT_EQUAL(0, bt_get_paired_device_count());
    
    // Load paired devices
    ret = bt_load_paired_devices();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Verify only the non-unpaired device was restored
    TEST_ASSERT_EQUAL(1, bt_get_paired_device_count());
    
    bt_device_t devices[5];
    int count = bt_get_paired_devices(devices, 5);
    TEST_ASSERT_EQUAL(1, count);
    
    char dev_addr[18];
    addr_to_str(devices[0].addr, dev_addr);
    TEST_ASSERT_EQUAL_STRING(addr2_str, dev_addr);
}

void test_unpair_all_persistence(void) {
    ESP_LOGI(PAIRING_TEST_TAG, "Testing unpair all persistence");
    
    bt_unpair_all_devices();
    
    // Add two paired devices
    uint8_t addr1[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    uint8_t addr2[6] = {0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
    
    bt_device_t device1 = create_device(addr1, "Device 1", true);
    bt_device_t device2 = create_device(addr2, "Device 2", true);
    bt_add_paired_device(&device1);
    bt_add_paired_device(&device2);
    
    // Store paired devices
    esp_err_t ret = bt_store_paired_devices();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Unpair all devices
    ret = bt_unpair_all_devices();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Verify no devices are paired
    TEST_ASSERT_EQUAL(0, bt_get_paired_device_count());
    
    // Store paired devices again
    ret = bt_store_paired_devices();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Reset paired devices
    bt_unpair_all_devices();
    
    // Add some dummy devices to make sure they get replaced on load
    bt_device_t dummy1 = create_device(addr1, "Dummy 1", true);
    bt_device_t dummy2 = create_device(addr2, "Dummy 2", true);
    bt_add_paired_device(&dummy1);
    bt_add_paired_device(&dummy2);
    TEST_ASSERT_EQUAL(2, bt_get_paired_device_count());
    
    // Load paired devices
    ret = bt_load_paired_devices();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Verify no devices were restored (since we unpaired all before storing)
    TEST_ASSERT_EQUAL(0, bt_get_paired_device_count());
}

void test_unpair_all_multiple_devices(void) {
    ESP_LOGI(PAIRING_TEST_TAG, "Testing unpair all with multiple devices");
    
    bt_unpair_all_devices();
    
    // Add 5 paired devices
    uint8_t addr1[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    uint8_t addr2[6] = {0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
    uint8_t addr3[6] = {0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    uint8_t addr4[6] = {0x44, 0x55, 0x66, 0x77, 0x88, 0x99};
    uint8_t addr5[6] = {0x55, 0x66, 0x77, 0x88, 0x99, 0xAA};
    
    bt_device_t device1 = create_device(addr1, "Device 1", true);
    bt_device_t device2 = create_device(addr2, "Device 2", true);
    bt_device_t device3 = create_device(addr3, "Device 3", true);
    bt_device_t device4 = create_device(addr4, "Device 4", true);
    bt_device_t device5 = create_device(addr5, "Device 5", true);
    
    bt_add_paired_device(&device1);
    bt_add_paired_device(&device2);
    bt_add_paired_device(&device3);
    bt_add_paired_device(&device4);
    bt_add_paired_device(&device5);
    
    // Verify 5 devices are paired
    int paired_count = bt_get_paired_device_count();
    TEST_ASSERT_EQUAL(5, paired_count);
    
    // Unpair all devices
    esp_err_t ret = bt_unpair_all_devices();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Verify no devices are paired
    paired_count = bt_get_paired_device_count();
    TEST_ASSERT_EQUAL(0, paired_count);
}

void test_unpair_all_with_connected_devices(void) {
    ESP_LOGI(PAIRING_TEST_TAG, "Testing unpair all with connected devices");
    
    bt_mock_reset();
    
    // Add a paired device and connect to it
    uint8_t addr[6] = {0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    char addr_str[18];
    addr_to_str(addr, addr_str);
    
    bt_device_t device = create_device(addr, "Test Device", true);
    bt_add_paired_device(&device);
    esp_err_t ret = bt_connect(addr_str);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Verify we're connected
    TEST_ASSERT_TRUE(bt_is_connected());
    
    // Unpair all devices
    ret = bt_unpair_all_devices();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Verify no devices are paired
    TEST_ASSERT_EQUAL(0, bt_get_paired_device_count());
    
    // Verify we're no longer connected
    TEST_ASSERT_FALSE(bt_is_connected());
}

void app_main_bt_pairing_tests(void)
{
    ESP_LOGI(PAIRING_TEST_TAG, "Starting Bluetooth pairing tests");
    
    // Initialize NVS flash to ensure it's ready for BT operations
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        ESP_LOGI(PAIRING_TEST_TAG, "Erasing NVS flash");
        ESP_ERROR_CHECK(nvs_flash_erase());
        // Retry initialization
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Initialize Bluetooth before running tests
    ESP_LOGI(PAIRING_TEST_TAG, "Initializing Bluetooth for pairing tests");
    ret = bt_init();  // Changed from bt_initialize() to bt_init()
    ESP_ERROR_CHECK(ret);
    
    // Clean up any existing pairing state
    bt_unpair_all_devices();
    
    UNITY_BEGIN();
    
    // PIN pairing tests
    RUN_TEST(test_pin_pairing_success);
    RUN_TEST(test_pin_pairing_failure);
    RUN_TEST(test_pin_pairing_timeout);
    RUN_TEST(test_set_default_pin);
    
    // SSP pairing tests
    RUN_TEST(test_ssp_confirmation_request);
    RUN_TEST(test_ssp_confirmation_accepted);
    RUN_TEST(test_ssp_confirmation_rejected);
    RUN_TEST(test_ssp_fallback_to_pin);
    
    // Unpair device tests
    RUN_TEST(test_unpair_specific_device);
    RUN_TEST(test_unpair_invalid_address);
    RUN_TEST(test_unpair_nonexistent_device);
    RUN_TEST(test_unpair_connected_device);
    
    // Unpair all devices tests
    RUN_TEST(test_unpair_all_devices);
    RUN_TEST(test_unpair_all_when_none_paired);
    RUN_TEST(test_unpair_all_with_connected_devices);
    RUN_TEST(test_unpair_all_multiple_devices);
    RUN_TEST(test_unpair_all_persistence);
    
    // Persistence tests
    RUN_TEST(test_unpair_persistence);
    RUN_TEST(test_paired_device_connection_info);
    RUN_TEST(test_paired_devices_retrieval);
    RUN_TEST(test_paired_devices_stored);
    
    UNITY_END();
    
    // Cleanup before exiting
    ESP_LOGI(PAIRING_TEST_TAG, "Cleaning up Bluetooth pairing tests");
    bt_unpair_all_devices();
    
    // No need to call bt_deinit() here as it could be causing conflicts
    // with other test modules if they share the same BT stack
    
    ESP_LOGI(PAIRING_TEST_TAG, "Bluetooth pairing tests completed");
}

// Add a function to be called from app_main to properly schedule these tests
void run_bt_pairing_tests(void) 
{
    // Add a delay to ensure BT stack is fully initialized before running tests
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    ESP_LOGI(PAIRING_TEST_TAG, "Starting Bluetooth pairing test suite");
    
    // Before running pairing tests, ensure no BT scan is in progress
    // This can help prevent conflicts with the A2DP tests
    bt_mock_stop_scan();  // Changed from bt_stop_scan() to bt_mock_stop_scan()
    vTaskDelay(pdMS_TO_TICKS(500)); // Wait for scan to fully stop
    
    // Clear any cached device scan data that might be causing issues
    bt_mock_reset();  // Changed from bt_clear_scan_results() to bt_mock_reset() which already clears scan data
    
    // Ensure the mock BT system is in a clean state - this is redundant now
    // bt_mock_reset();
    
    app_main_bt_pairing_tests();
    
    // Ensure we're not leaving any scans running
    bt_mock_stop_scan();  // Changed from bt_stop_scan() to bt_mock_stop_scan()
    
    // Clean up scan results again to prevent other tests from using potentially corrupted data
    bt_mock_reset();  // Changed from bt_clear_scan_results() to bt_mock_reset()
    
    ESP_LOGI(PAIRING_TEST_TAG, "Bluetooth pairing test suite completed");
    
    // Add delay after tests to allow system to stabilize
    vTaskDelay(pdMS_TO_TICKS(1000));
}
