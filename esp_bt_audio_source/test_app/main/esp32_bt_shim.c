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
static bool ssp_support_enabled = true;
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
    ssp_support_enabled = true;
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
                test_devices[test_device_count].cod = 0x240404; // Audio device
                break;
            case BT_DEVICE_TYPE_PHONE:
                test_devices[test_device_count].cod = 0x200000; // Phone
                break;
            case BT_DEVICE_TYPE_COMPUTER:
                test_devices[test_device_count].cod = 0x100000; // Computer
                break;
            case BT_DEVICE_TYPE_HEADSET:
                test_devices[test_device_count].cod = 0x240418; // Headset
                break;
            case BT_DEVICE_TYPE_SPEAKER:
                test_devices[test_device_count].cod = 0x240414; // Speaker
                break;
            default:
                test_devices[test_device_count].cod = 0x000000; // Unknown
        }

        test_device_count++;
        test_device_added = true;
    }
}

/**
 * Configure SSP support for testing
 */
void bt_mock_set_ssp_supported(bool supported) {
    ESP_LOGI(TAG, "Setting SSP support: %d", supported);
    ssp_support_enabled = supported;
}

/**
 * Simulate PIN pairing failure
 */
void bt_mock_simulate_pin_failure(void) {
    ESP_LOGI(TAG, "Simulating PIN failure");
    pin_failure_simulated = true;
}

/**
 * Simulate pairing timeout
 */
void bt_mock_simulate_pairing_timeout(void) {
    ESP_LOGI(TAG, "Simulating pairing timeout");
    pairing_timeout_simulated = true;
}

/**
 * Simulate SSP request with a specific passkey
 */
esp_err_t bt_mock_simulate_ssp_request(uint32_t passkey) {
    ESP_LOGI(TAG, "Simulating SSP request with passkey: %" PRIu32, passkey);
    
    // The real implementation should provide a way to inject SSP requests
    // for test purposes
    return ESP_OK;
}
