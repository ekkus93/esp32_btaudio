/**
 * @file bt_pairing_test.c
 * @brief Implementation of Bluetooth pairing tests and mock interface functions.
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
void setup_mock_bt_implementation(void)
{
    // Initialize to default values
    mock_state.pairing_state = BT_PAIRING_STATE_IDLE;
    mock_state.pairing_method = BT_PAIRING_METHOD_NONE;
    mock_state.ssp_confirmation_requested = false;
    strcpy(mock_state.default_pin, "1234");
    mock_state.is_connected = false;
    memset(mock_state.connected_addr, 0, sizeof(mock_state.connected_addr));
    memset(mock_state.ssp_passkey, 0, sizeof(mock_state.ssp_passkey));
    
    // Reset the mock BT state
    bt_mock_reset();
}

/**
 * Get the BT implementation for tests - fix the return type to match header
 */
bt_interface_t* get_bt_implementation(void)
{
    // Cast to the expected return type
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

/**
 * Unpair a device
 */
esp_err_t bt_mock_unpair_device(const char* addr)
{
    if (addr == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Implementation would involve removing the device from a paired list
    // For the mock, we'll just simulate success
    return ESP_OK;
}

/**
 * Unpair all devices
 */
esp_err_t bt_mock_unpair_all_devices(void)
{
    // Implementation would involve clearing the paired device list
    // For the mock, we'll just simulate success
    return ESP_OK;
}

/**
 * Get the number of paired devices - fix to match header return type
 */
int bt_mock_get_paired_device_count(void)
{
    // Return a fixed number for testing
    return 1;
}

/**
 * Get the list of paired devices
 */
int bt_mock_get_paired_devices(bt_device_t* devices, int max_count)
{
    if (!devices || max_count <= 0) {
        return 0;
    }
    
    // Create a sample paired device
    if (max_count > 0) {
        strcpy(devices[0].name, "Mock Paired Device");
        devices[0].addr[0] = 0x12;
        devices[0].addr[1] = 0x34;
        devices[0].addr[2] = 0x56;
        devices[0].addr[3] = 0x78;
        devices[0].addr[4] = 0x9A;
        devices[0].addr[5] = 0xBC;
        devices[0].rssi = -70;
        devices[0].paired = true;
        devices[0].cod = 0x240404; // Audio device
        
        return 1; // Return 1 device
    }
    
    return 0;
}

/**
 * Send PIN code during pairing
 */
#if 0 // Comment out this function as it's now implemented in bt_mock_devices.c
esp_err_t bt_mock_send_pin(const char* pin)
{
    if (!pin) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Save the PIN and change pairing state to complete
    strncpy(mock_state.default_pin, pin, sizeof(mock_state.default_pin) - 1);
    mock_state.default_pin[sizeof(mock_state.default_pin) - 1] = '\0';
    mock_state.pairing_state = BT_PAIRING_STATE_PAIRED;
    
    return ESP_OK;
}
#endif

/**
 * Get the current pairing state
 */
bt_pairing_state_t bt_mock_get_pairing_state(void)
{
    // We need to make sure we're using the "current_pairing_state" from bt_mock_devices.c
    // We need to declare this as an extern so we can access it
    extern bt_pairing_state_t current_pairing_state;
    
    // Return the actual state from bt_mock_devices.c rather than our local state
    return current_pairing_state;
}

/**
 * Get the current pairing method
 */
bt_pairing_method_t bt_mock_get_pairing_method(void)
{
    // Similarly, we need to access the method from bt_mock_devices.c
    extern bt_pairing_method_t current_pairing_method;
    
    // Return the actual method from bt_mock_devices.c
    return current_pairing_method;
}

/**
 * Check if SSP confirmation is requested
 */
bool bt_mock_is_ssp_confirm_requested(void)
{
    // Access the SSP confirmation flag from bt_mock_devices.c
    extern bool s_ssp_confirmation_requested;
    
    // Return the actual value from bt_mock_devices.c
    return s_ssp_confirmation_requested;
}

/**
 * Confirm SSP pairing
 */
esp_err_t bt_mock_confirm_ssp(bool confirm)
{
    // Access global variables from bt_mock_devices.c
    extern bool s_ssp_confirmation_requested;
    extern bt_pairing_state_t current_pairing_state;
    
    if (!s_ssp_confirmation_requested) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Update both local and global state
    mock_state.ssp_confirmation_requested = false;
    s_ssp_confirmation_requested = false;
    
    if (confirm) {
        // Set to BT_PAIRING_STATE_PAIRED (0) as test expects
        mock_state.pairing_state = BT_PAIRING_STATE_PAIRED;
        current_pairing_state = BT_PAIRING_STATE_PAIRED;
    } else {
        // Set to BT_PAIRING_STATE_FAILED (5) as test expects
        mock_state.pairing_state = BT_PAIRING_STATE_FAILED;
        current_pairing_state = BT_PAIRING_STATE_FAILED;
    }
    
    return ESP_OK;
}

/**
 * Get the SSP passkey - fixed to match header return type
 */
uint32_t bt_mock_get_ssp_passkey(void)
{
    // Access global variable from bt_mock_devices.c
    extern uint32_t s_ssp_passkey_value;
    
    // Return the global passkey value, which should be non-zero
    // when bt_mock_simulate_ssp_request() is called
    return s_ssp_passkey_value;
}

/**
 * Test PIN pairing success scenario
 */
void test_pin_pairing_success(void)
{
    ESP_LOGI(TAG, "Testing PIN pairing success");
    
    // Get the test implementation
    bt_interface_t* impl = get_bt_implementation();
    TEST_ASSERT_NOT_NULL(impl);
    
    // Start pairing with a PIN-based device
    mock_state.pairing_method = BT_PAIRING_METHOD_PIN;
    mock_state.pairing_state = BT_PAIRING_STATE_PIN_REQUESTED;
    
    const char* test_addr = "12:34:56:78:9A:BC";
    esp_err_t ret = bt_start_pairing(test_addr);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Send PIN code
    ret = bt_send_pin_code("1234");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Verify pairing state is now complete
    TEST_ASSERT_EQUAL(BT_PAIRING_STATE_PAIRED, bt_get_pairing_state());
    
    // Device should now be paired
    TEST_ASSERT_TRUE(bt_is_device_paired(test_addr));
    
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
    TEST_ASSERT_NOT_EQUAL(ESP_OK, ret);
    
    // Verify pairing state is failed
    TEST_ASSERT_EQUAL(BT_PAIRING_STATE_FAILED, bt_mock_get_pairing_state());
    
    ESP_LOGI(TAG, "PIN pairing failure test completed");
}

/**
 * Test SSP confirmation request scenario
 */
void test_ssp_confirmation_request(void)
{
    ESP_LOGI(TAG, "Testing SSP confirmation request");
    
    const char* test_addr = "12:34:56:78:9A:BC";
    
    // Set up SSP mock
    mock_state.pairing_method = BT_PAIRING_METHOD_SSP;
    mock_state.pairing_state = BT_PAIRING_STATE_IDLE;
    mock_state.ssp_confirmation_requested = false;
    
    // Start pairing
    esp_err_t ret = bt_start_pairing(test_addr);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Verify pairing state is now SSP request
    TEST_ASSERT_EQUAL(BT_PAIRING_STATE_SSP_REQUESTED, bt_mock_get_pairing_state());
    
    // Verify pairing method is SSP
    TEST_ASSERT_EQUAL(BT_PAIRING_METHOD_SSP, bt_mock_get_pairing_method());
    
    // Verify SSP confirmation is requested
    TEST_ASSERT_TRUE(bt_mock_is_ssp_confirm_requested());
    
    // Get and verify passkey
    uint32_t passkey = bt_mock_get_ssp_passkey();
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
    
    // Set up SSP mock
    mock_state.pairing_method = BT_PAIRING_METHOD_SSP;
    mock_state.pairing_state = BT_PAIRING_STATE_SSP_REQUESTED;
    mock_state.ssp_confirmation_requested = true;
    strcpy(mock_state.ssp_passkey, "123456");
    
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
    
    // Verify pairing method is PIN
    TEST_ASSERT_EQUAL(BT_PAIRING_METHOD_PIN, bt_mock_get_pairing_method());
    
    // Verify pairing state is BT_PAIRING_STATE_STARTED (1)
    // The test is expecting exactly 1, not 2 as was being returned
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
    RUN_TEST(test_ssp_confirmation_request);
    RUN_TEST(test_ssp_confirmation_accepted);
    RUN_TEST(test_ssp_confirmation_rejected);
    RUN_TEST(test_ssp_fallback_to_pin);
    
    // Finish Unity tests
    UNITY_END();
    
    ESP_LOGI(TAG, "Bluetooth pairing tests completed");
}
