/**
 * Bluetooth Mock Devices
 *
 * This file provides mock implementations for Bluetooth device management functions.
 */

#include <string.h>
#include "esp_log.h"
#include "bt_mock_devices.h"

static const char *TAG = "BT_MOCK_DEVICES";

// Mock device data
static bt_mock_device_t mock_devices[5] = {
    {{0x11, 0x22, 0x33, 0x44, 0x55, 0x66}, "Mock Device 1", BT_DEVICE_TYPE_AUDIO, true},
    {{0x22, 0x33, 0x44, 0x55, 0x66, 0x77}, "Mock Device 2", BT_DEVICE_TYPE_AUDIO, true},
    {{0x33, 0x44, 0x55, 0x66, 0x77, 0x88}, "Mock Device 3", BT_DEVICE_TYPE_AUDIO, true},
    {{0x44, 0x55, 0x66, 0x77, 0x88, 0x99}, "Mock Speaker", BT_DEVICE_TYPE_AUDIO, true},
    {{0x55, 0x66, 0x77, 0x88, 0x99, 0xAA}, "Mock Headphones", BT_DEVICE_TYPE_AUDIO, true}
};

static int mock_device_count = 5;

// Add these function prototypes
int bt_mock_get_device_count(void);
esp_err_t bt_mock_get_devices(bt_mock_device_t *devices, int max_count, int *actual_count);
void bt_mock_set_ssp_support(bool supported);
bool bt_mock_get_ssp_support(void);
esp_err_t bt_mock_simulate_pin_failure(void);
esp_err_t bt_mock_simulate_pairing_timeout(void);
esp_err_t bt_mock_simulate_ssp_request(uint32_t passkey);

void bt_mock_reset(void) {
    memset(mock_devices, 0, sizeof(mock_devices));
    mock_device_count = 0;
}

void bt_mock_add_device(const char *addr, const char *name, bt_device_type_t type, bool paired) {
    if (mock_device_count >= 5) {
        ESP_LOGW(TAG, "Max mock devices reached");
        return;
    }
    
    bt_mock_device_t *dev = &mock_devices[mock_device_count++];
    memcpy(dev->addr, addr, 6);
    strncpy(dev->name, name, sizeof(dev->name) - 1);
    dev->type = type;
    dev->paired = paired;
}

int bt_mock_get_device_count(void) {
    return mock_device_count;
}

esp_err_t bt_mock_get_devices(bt_mock_device_t *devices, int max_count, int *actual_count) {
    if (max_count < mock_device_count) {
        ESP_LOGW(TAG, "Buffer too small, truncating device list");
        mock_device_count = max_count;
    }
    
    memcpy(devices, mock_devices, mock_device_count * sizeof(bt_mock_device_t));
    
    if (actual_count) {
        *actual_count = mock_device_count;
    }
    
    return ESP_OK;
}

static bool ssp_supported = true;

/**
 * Set the SSP support flag for mock devices
 * 
 * @param supported Whether SSP is supported
 */
void bt_mock_set_ssp_support(bool supported) {
    ssp_supported = supported;
}

/**
 * Get the SSP support flag for mock devices
 * 
 * @return Whether SSP is supported
 */
bool bt_mock_get_ssp_support(void) {
    return ssp_supported;
}

esp_err_t bt_mock_simulate_pin_failure(void) {
    ESP_LOGI(TAG, "Simulating PIN failure");
    
    // Simulate a pairing failure due to incorrect PIN
    for (int i = 0; i < mock_device_count; i++) {
        mock_devices[i].paired = false;
    }
    
    return ESP_OK;
}

esp_err_t bt_mock_simulate_pairing_timeout(void) {
    ESP_LOGI(TAG, "Simulating pairing timeout");
    
    // Simulate a pairing timeout
    for (int i = 0; i < mock_device_count; i++) {
        mock_devices[i].paired = false;
    }
    
    return ESP_OK;
}

esp_err_t bt_mock_simulate_ssp_request(uint32_t passkey) {
    ESP_LOGI(TAG, "Simulating SSP request with passkey: %d", passkey);
    
    if (!ssp_supported) {
        ESP_LOGW(TAG, "SSP not supported, cannot simulate request");
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    // For testing, assume SSP request always succeeds
    for (int i = 0; i < mock_device_count; i++) {
        mock_devices[i].paired = true;
    }
    
    return ESP_OK;
}