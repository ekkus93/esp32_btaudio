/**
 * ESP32 Bluetooth Shim Layer
 * 
 * This is a thin wrapper around the real implementation to facilitate testing on device.
 * Instead of mocking, we defer to the actual implementation while adding test instrumentation.
 */

#include "esp_log.h"
#include "bt_source.h"
#include <string.h>
#include <stdbool.h>
#include <inttypes.h>
#include "bt_mock_devices.h"  // Include the mock devices header

static const char *TAG = "BT_SHIM";

// Test variables
static bool test_mode = false;
static bool test_device_added = false;
static bool ssp_supported = true;
static bool pin_failure_simulated = false;
static bool pairing_timeout_simulated = false;
static uint32_t simulated_passkey = 0;
static bt_device_t test_devices[5];
static int test_device_count = 0;

/**
 * Reset all test state
 */
static void esp32_bt_mock_reset(void) // Renamed to avoid conflict
{
    ESP_LOGI(TAG, "Resetting Bluetooth test state");
    test_mode = true;
    test_device_added = false;
    ssp_supported = true;
    pin_failure_simulated = false;
    pairing_timeout_simulated = false;
    memset(test_devices, 0, sizeof(test_devices));
    test_device_count = 0;
}

/**
 * Add a test device to scan results
 */
void bt_mock_add_test_device(const char* addr_str, const char* name, bt_device_type_t type) {
    ESP_LOGI(TAG, "Adding test device to scan results: %s (%s)", addr_str, name);

    if (test_device_count >= 5) {
        ESP_LOGW(TAG, "Too many test devices, ignoring");
        return;
    }

    // Parse MAC address
    uint8_t addr[6];
    if (sscanf(addr_str, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
               &addr[0], &addr[1], &addr[2], &addr[3], &addr[4], &addr[5]) == 6) {

        // Add to test devices
        memcpy(test_devices[test_device_count].addr, addr, 6);
        strncpy(test_devices[test_device_count].name, name, sizeof(test_devices[test_device_count].name) - 1);
        test_devices[test_device_count].name[sizeof(test_devices[test_device_count].name) - 1] = '\0';

        // Set device class of device (CoD) based on type
        switch (type) {
            case BT_DEVICE_TYPE_AUDIO:
                // Handle different audio device types by examining the name
                if (strstr(name, "Computer") != NULL) {
                    test_devices[test_device_count].cod = 0x240404;  // Computer audio device
                } else if (strstr(name, "Headset") != NULL) {
                    test_devices[test_device_count].cod = 0x240408;  // Audio headset device
                } else if (strstr(name, "Speaker") != NULL) {
                    test_devices[test_device_count].cod = 0x240414;  // Audio speaker device
                } else {
                    test_devices[test_device_count].cod = 0x240400;  // Default audio device
                }
                break;
            case BT_DEVICE_TYPE_PHONE:
                test_devices[test_device_count].cod = 0x500204;  // Phone device
                break;
            default:
                test_devices[test_device_count].cod = 0x120104;  // Generic device
                break;
        }

        test_device_count++;
        test_device_added = true;
    }
}

/**
 * Configure SSP support for testing
 */
void esp32_bt_shim_set_ssp_supported(bool supported)
{
    // Store the value locally first
    ssp_supported = supported;
    
    // Call the real implementation instead of redefining
    bt_mock_set_ssp_supported(supported);
}

/**
 * Simulate PIN pairing failure
 */
static void esp32_bt_shim_simulate_pin_failure(void)
{
    ESP_LOGI(TAG, "Simulating PIN failure");
    pin_failure_simulated = true;
}

/**
 * Simulate pairing timeout
 */
void esp32_bt_shim_simulate_pairing_timeout(void)
{
    ESP_LOGI(TAG, "Simulating PIN pairing timeout");
    
    // Call the real implementation instead of redefining
    bt_mock_simulate_pairing_timeout();
}

/**
 * Simulate SSP request with a specific passkey
 */
static esp_err_t esp32_bt_shim_simulate_ssp_request(uint32_t passkey)
{
    ESP_LOGI(TAG, "Simulating SSP request with passkey: %" PRIu32, passkey);
    
    // The real implementation should provide a way to inject SSP requests
    // for test purposes
    return ESP_OK;
}

// If this function exists and uses bt_mock_simulate_pin_failure, update it to use the renamed version
void bt_simulate_pin_failure(void) 
{
    esp32_bt_shim_simulate_pin_failure();
}

// If this function exists and uses bt_mock_simulate_ssp_request, update it to use the renamed version
esp_err_t bt_simulate_ssp_request(uint32_t passkey)
{
    return esp32_bt_shim_simulate_ssp_request(passkey);
}
