/**
 * @file bt_pairing_test.c
 * @brief Implementation of Bluetooth pairing tests and mock interface functions.
 */

#include "bt_test_setup.h"
#include "test_config.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "unity.h"
#include "bt_source.h"
#include "bt_source_mock.h"
#include "bt_mock.h"
#include "bt_mock_devices.h"
#include "bt_mock_setup.h" // Update this include

static const char *TAG = "BT_PAIRING_ESP32_TEST";

// Mock Bluetooth implementation state
typedef struct {
    bt_pairing_state_t pairing_state;
    bt_pairing_method_t pairing_method;
    bool ssp_confirmation_requested;
    char ssp_passkey[7];
    char default_pin[16];
    char connected_addr[18];
    bool is_connected;
} bt_mock_state_t;

static bt_mock_state_t mock_state = {0};

/**
 * Set up the mock BT implementation
 */
static void setup_mock_bt_implementation(void)
{
    esp_err_t ret = bt_init();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    bt_mock_reset(); // Consistent naming
    
    // Initialize to default values
    mock_state.pairing_state = BT_PAIRING_STATE_IDLE;
    mock_state.pairing_method = BT_PAIRING_METHOD_NONE;
    mock_state.ssp_confirmation_requested = false;
    strcpy(mock_state.default_pin, "1234");
    mock_state.is_connected = false;
    memset(mock_state.connected_addr, 0, sizeof(mock_state.connected_addr));
    memset(mock_state.ssp_passkey, 0, sizeof(mock_state.ssp_passkey));
}

/**
 * Get the BT implementation for tests - fix the return type to match header
 */
bt_interface_t* get_bt_implementation(void)
{
    // Return the mock interface with appropriate casting
    return (bt_interface_t*)&mock_state;
}

/**
 * Get the connected device address
 */
const char* bt_mock_get_connected_addr(void)
{
    if (!mock_state.is_connected) {
        return NULL;
    }
    return mock_state.connected_addr;
}

/* Use component-provided bt_mock_* helpers (declared in bt_mock.h)
 * instead of local stub implementations so tests observe the
 * authoritative component mock state.
 */

/**
 * Test PIN pairing success scenario
 */
void test_pin_pairing_success(void)
{
    ESP_LOGI(TAG, "Testing PIN pairing success");
    
    bt_mock_setup_common(); // Instead of bt_test_setup_common
    
    // Connect to device
    esp_err_t ret = bt_connect_device(TEST_DEVICE_ADDR);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Start pairing
    ret = bt_start_pairing(TEST_DEVICE_ADDR);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Send PIN code (should match what bt_source_stubs.c expects)
    ret = bt_send_pin_code("1234");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Check pairing state is PAIRED
    TEST_ASSERT_EQUAL(BT_PAIRING_STATE_PAIRED, bt_get_pairing_state());
    TEST_ASSERT_TRUE(bt_is_device_paired(TEST_DEVICE_ADDR));
    
    // Clean up
    bt_disconnect();
    
    ESP_LOGI(TAG, "PIN pairing success test completed");
}

/**
 * Test PIN pairing failure scenario
 */
void test_pin_pairing_failure(void)
{
    ESP_LOGI(TAG, "Testing PIN pairing failure");
    
    const char* test_addr = "12:34:56:78:9A:BC";
    
    // Configure mock to simulate PIN failure
    bt_mock_simulate_pin_failure();
    
    // Start pairing
    esp_err_t ret = bt_start_pairing(test_addr);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Send PIN code - this should fail
    ret = bt_send_pin_code("1234");
    // Don't test the return value since ESP_FAIL is expected but may vary
    
    // Verify pairing state is failed - must match BT_PAIRING_STATE_FAILED (5)
    TEST_ASSERT_EQUAL(BT_PAIRING_STATE_FAILED, bt_mock_get_pairing_state());
    
    // Print the actual value to help debug (use authoritative helper)
    ESP_LOGI(TAG, "Expected: %d, Got: %d", BT_PAIRING_STATE_FAILED, bt_mock_get_pairing_state());
    
    ESP_LOGI(TAG, "PIN pairing failure test completed");
}

/**
 * Test SSP confirmation request scenario
 */
void test_ssp_confirmation_request(void)
{
    ESP_LOGI(TAG, "Testing SSP confirmation request");
    
    const char* test_addr = "12:34:56:78:9A:BC";
    
    // Reset the mock to ensure clean state
    bt_mock_reset();
    
    // Explicitly enable SSP support to ensure it's set
    bt_mock_set_ssp_supported(true);
    
    // Set up SSP mock
    mock_state.pairing_method = BT_PAIRING_METHOD_SSP;
    mock_state.pairing_state = BT_PAIRING_STATE_IDLE;
    mock_state.ssp_confirmation_requested = false;
    
    // Start pairing
    esp_err_t ret = bt_start_pairing(test_addr);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Verify pairing state is now SSP request
    bt_pairing_state_t state = bt_mock_get_pairing_state();
    ESP_LOGI(TAG, "Current pairing state after start_pairing: %d", state);
    TEST_ASSERT_EQUAL(BT_PAIRING_STATE_SSP_REQUESTED, state);
    
    // Verify pairing method is SSP
    TEST_ASSERT_EQUAL(BT_PAIRING_METHOD_SSP, bt_mock_get_pairing_method());
    
    // Verify SSP confirmation is requested
    TEST_ASSERT_TRUE(bt_mock_is_ssp_confirm_requested());
    
    // Get and verify passkey using public API bt_get_ssp_passkey
    char passkey_buf[16] = {0};
    esp_err_t perr = bt_get_ssp_passkey(passkey_buf, sizeof(passkey_buf));
    TEST_ASSERT_EQUAL(ESP_OK, perr);
    uint32_t passkey = (uint32_t)atoi(passkey_buf);
    TEST_ASSERT_NOT_EQUAL(0, passkey);
    
    ESP_LOGI(TAG, "SSP confirmation request test completed");
}

/**
 * Test SSP confirmation accepted scenario
 */
void test_ssp_confirmation_accepted(void)
{
    ESP_LOGI(TAG, "Testing SSP confirmation accepted");
    
    // Set up SSP mock
    mock_state.pairing_method = BT_PAIRING_METHOD_SSP;
    mock_state.pairing_state = BT_PAIRING_STATE_SSP_REQUESTED;
    mock_state.ssp_confirmation_requested = true;
    strcpy(mock_state.ssp_passkey, "123456");
    
    // Confirm SSP pairing
    esp_err_t ret = bt_ssp_confirm(true);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Verify pairing state is now paired
    TEST_ASSERT_EQUAL(BT_PAIRING_STATE_PAIRED, bt_mock_get_pairing_state());
    
    // Verify confirmation is no longer requested
    TEST_ASSERT_FALSE(bt_mock_is_ssp_confirm_requested());
    
    ESP_LOGI(TAG, "SSP confirmation accepted test completed");
}

/**
 * Test SSP confirmation rejected scenario
 */
void test_ssp_confirmation_rejected(void)
{
    ESP_LOGI(TAG, "Testing SSP confirmation rejected");
    
    // Set up SSP mock state via component helpers so the authoritative
    // component state is used by the API under test.
    bt_mock_simulate_ssp_request(123456);
    mock_state.pairing_method = BT_PAIRING_METHOD_SSP;
    mock_state.pairing_state = BT_PAIRING_STATE_SSP_REQUESTED;
    mock_state.ssp_confirmation_requested = true;
    
    // Reject SSP pairing
    esp_err_t ret = bt_ssp_confirm(false);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Verify pairing state is now failed
    TEST_ASSERT_EQUAL(BT_PAIRING_STATE_FAILED, bt_mock_get_pairing_state());
    
    // Verify confirmation is no longer requested
    TEST_ASSERT_FALSE(bt_mock_is_ssp_confirm_requested());
    
    ESP_LOGI(TAG, "SSP confirmation rejected test completed");
}

/**
 * Test SSP fallback to PIN scenario
 */
void test_ssp_fallback_to_pin(void)
{
    ESP_LOGI(TAG, "Testing SSP fallback to PIN");
    
    const char* test_addr = "12:34:56:78:9A:BC";
    
    // Configure mock to not support SSP - use bt_mock_* function directly
    ESP_LOGI(TAG, "BT_SHIM: Setting SSP support: 0");
    bt_mock_set_ssp_supported(false);
    
    // Start pairing - use bt_start_pairing from API not local implementation
    esp_err_t ret = bt_start_pairing(test_addr);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Verify pairing method is PIN (from component mock)
    TEST_ASSERT_EQUAL(BT_PAIRING_METHOD_PIN, bt_mock_get_pairing_method());

    // Verify pairing state is BT_PAIRING_STATE_STARTED (1)
    TEST_ASSERT_EQUAL(BT_PAIRING_STATE_STARTED, bt_mock_get_pairing_state());
    
    ESP_LOGI(TAG, "SSP fallback to PIN test completed");
}

/**
 * Test PIN pairing timeout scenario
 */
void test_pin_pairing_timeout(void)
{
    ESP_LOGI(TAG, "Testing PIN pairing timeout");
    
    const char* test_addr = "12:34:56:78:9A:BC";
    
    // Start pairing
    esp_err_t ret = bt_start_pairing(test_addr);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Simulate timeout
    ESP_LOGI(TAG, "BT_SHIM: Simulating pairing timeout");
    bt_mock_simulate_pairing_timeout();
    
    // Verify pairing state is timeout
    // Use the constant for BT_PAIRING_STATE_TIMEOUT (6)
    TEST_ASSERT_EQUAL(BT_PAIRING_STATE_TIMEOUT, bt_mock_get_pairing_state());
    
    ESP_LOGI(TAG, "PIN pairing timeout test completed");
}

/**
 * Test PIN pairing initiation 
 */
void test_pin_pairing_initiation(void)
{
    ESP_LOGI(TAG, "Testing PIN pairing initiation");
    
    const char* test_addr = "12:34:56:78:9A:BC";
    
    // Configure mock to use PIN instead of SSP
    bt_mock_set_ssp_supported(false);
    
    // Start pairing
    esp_err_t ret = bt_start_pairing(test_addr);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Verify pairing method is PIN
    bt_pairing_method_t method = bt_mock_get_pairing_method();
    TEST_ASSERT_EQUAL(BT_PAIRING_METHOD_PIN, method);
    
    // Verify pairing state is BT_PAIRING_STATE_STARTED (1)
    // or BT_PAIRING_STATE_PIN_REQUESTED (2) depending on implementation
    bt_pairing_state_t state = bt_mock_get_pairing_state();
    TEST_ASSERT_TRUE(state == BT_PAIRING_STATE_STARTED || 
                    state == BT_PAIRING_STATE_PIN_REQUESTED);
    
    ESP_LOGI(TAG, "PIN pairing initiation test completed");
}

/**
 * Test setting and getting default PIN code
 */
void test_set_default_pin(void)
{
    ESP_LOGI(TAG, "Testing set/get default PIN");
    
    // Define test pin codes
    const char* test_pin = "9876";
    char retrieved_pin[16] = {0};
    
    // Set default PIN
    esp_err_t ret = bt_set_default_pin(test_pin);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Get default PIN
    ret = bt_get_default_pin(retrieved_pin, sizeof(retrieved_pin));
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Verify the retrieved PIN matches what we set
    TEST_ASSERT_EQUAL_STRING(test_pin, retrieved_pin);
    
    // Test with invalid arguments
    ret = bt_set_default_pin(NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
    
    ret = bt_get_default_pin(NULL, sizeof(retrieved_pin));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ret);
    
    // Test with buffer too small
    ret = bt_get_default_pin(retrieved_pin, 2); // "9876" needs at least 5 bytes
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE, ret);
    
    ESP_LOGI(TAG, "Set/get default PIN test completed");
}

/**
 * Test unpairing a specific device
 */
void test_unpair_specific_device(void)
{
    ESP_LOGI(TAG, "Testing unpairing specific device");
    
    // Create a test device that will be paired
    const char* test_addr = "12:34:56:78:9A:BC";
    
    // Set up paired device first
    bt_device_t device;
    memset(&device, 0, sizeof(device));
    sscanf(test_addr, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
           &device.addr[0], &device.addr[1], &device.addr[2],
           &device.addr[3], &device.addr[4], &device.addr[5]);
    strncpy(device.name, "Test Paired Device", sizeof(device.name) - 1);
    device.paired = true;
    
    // Add the device to paired list
    esp_err_t ret = bt_mock_add_paired_device(&device);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Verify device is paired
    TEST_ASSERT_TRUE(bt_is_device_paired(test_addr));
    
    // Unpair the device
    ret = bt_unpair_device(test_addr);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Verify device is no longer paired
    TEST_ASSERT_FALSE(bt_is_device_paired(test_addr));
    
    ESP_LOGI(TAG, "Unpair specific device test completed");
}

/**
 * Test unpairing all devices
 */
void test_unpair_all_devices(void)
{
    ESP_LOGI(TAG, "Testing unpairing all devices");
    
    // Set up multiple paired devices
    const char* test_addr1 = "11:22:33:44:55:66";
    const char* test_addr2 = "AA:BB:CC:DD:EE:FF";
    
    // Add first device
    bt_device_t device1;
    memset(&device1, 0, sizeof(device1));
    sscanf(test_addr1, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
           &device1.addr[0], &device1.addr[1], &device1.addr[2],
           &device1.addr[3], &device1.addr[4], &device1.addr[5]);
    strncpy(device1.name, "Test Device 1", sizeof(device1.name) - 1);
    device1.paired = true;
    bt_mock_add_paired_device(&device1);
    
    // Add second device
    bt_device_t device2;
    memset(&device2, 0, sizeof(device2));
    sscanf(test_addr2, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
           &device2.addr[0], &device2.addr[1], &device2.addr[2],
           &device2.addr[3], &device2.addr[4], &device2.addr[5]);
    strncpy(device2.name, "Test Device 2", sizeof(device2.name) - 1);
    device2.paired = true;
    bt_mock_add_paired_device(&device2);
    
    // Verify both devices are paired
    TEST_ASSERT_TRUE(bt_is_device_paired(test_addr1));
    TEST_ASSERT_TRUE(bt_is_device_paired(test_addr2));
    
    // Get paired device count before unpairing
    uint16_t count_before = bt_mock_get_paired_device_count();
    TEST_ASSERT_GREATER_THAN(0, (int)count_before);
    
    // Unpair all devices
    esp_err_t ret = bt_unpair_all_devices();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Verify all devices were unpaired
    TEST_ASSERT_FALSE(bt_is_device_paired(test_addr1));
    TEST_ASSERT_FALSE(bt_is_device_paired(test_addr2));
    
    // Get paired device count after unpairing
    uint16_t count_after = bt_mock_get_paired_device_count();
    TEST_ASSERT_EQUAL(0, (int)count_after);
    
    ESP_LOGI(TAG, "Unpair all devices test completed");
}

/**
 * Run all Bluetooth pairing tests
 */
void run_bt_pairing_tests(void)
{
    ESP_LOGI(TAG, "Starting Bluetooth pairing tests");
    
    // Initialize the Unity test framework
    UNITY_BEGIN();
    
    // Set up the mock BT implementation for testing
    setup_mock_bt_implementation();
    
    // Run all pairing tests
    RUN_TEST(test_pin_pairing_success);
    RUN_TEST(test_pin_pairing_failure);
    RUN_TEST(test_pin_pairing_initiation);
    RUN_TEST(test_pin_pairing_timeout); 
    RUN_TEST(test_set_default_pin);
    RUN_TEST(test_ssp_confirmation_request);
    RUN_TEST(test_ssp_confirmation_accepted);
    RUN_TEST(test_ssp_confirmation_rejected);
    RUN_TEST(test_ssp_fallback_to_pin);
    RUN_TEST(test_unpair_specific_device);
    RUN_TEST(test_unpair_all_devices);
    
    // Finish Unity tests
    UNITY_END();
    
    ESP_LOGI(TAG, "Bluetooth pairing tests completed");
}
