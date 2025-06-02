#include "bt_mock_devices.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "BT_MOCK";

// Mock device list
static bt_mock_device_t mock_devices[10];
static int mock_device_count = 0;
static bool mock_connected = false;
static bt_mock_device_t connected_device;
static bool ssp_supported = false;
static bool pin_failure_simulated = false;
static bool pairing_timeout_simulated = false;
static bool ssp_request_simulated = false;

void bt_mock_reset(void) {
    ESP_LOGI(TAG, "Mock Bluetooth state reset");
    mock_device_count = 0;
    mock_connected = false;
    ssp_supported = false;
    pin_failure_simulated = false;
    pairing_timeout_simulated = false;
    ssp_request_simulated = false;
    memset(&connected_device, 0, sizeof(bt_mock_device_t));
}

esp_err_t bt_mock_add_device(const char *addr, const char *name, bt_device_type_t type, bool supports_a2dp) {
    if (mock_device_count >= 10) {
        return ESP_ERR_NO_MEM;
    }
    
    // Parse address
    uint8_t bt_addr[6];
    int res = sscanf(addr, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", 
                    &bt_addr[0], &bt_addr[1], &bt_addr[2], 
                    &bt_addr[3], &bt_addr[4], &bt_addr[5]);
    
    if (res != 6) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Add device
    memcpy(mock_devices[mock_device_count].addr, bt_addr, 6);
    strncpy(mock_devices[mock_device_count].name, name, 63);
    mock_devices[mock_device_count].name[63] = '\0';
    mock_devices[mock_device_count].type = type;
    mock_devices[mock_device_count].supports_a2dp = supports_a2dp;
    
    mock_device_count++;
    
    ESP_LOGI(TAG, "Added mock device: %s (%s)", addr, name);
    
    return ESP_OK;
}

esp_err_t bt_mock_connect(const char *addr) {
    // Parse address
    uint8_t bt_addr[6];
    int res = sscanf(addr, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", 
                    &bt_addr[0], &bt_addr[1], &bt_addr[2], 
                    &bt_addr[3], &bt_addr[4], &bt_addr[5]);
    
    if (res != 6) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Find device
    for (int i = 0; i < mock_device_count; i++) {
        if (memcmp(mock_devices[i].addr, bt_addr, 6) == 0) {
            mock_connected = true;
            memcpy(&connected_device, &mock_devices[i], sizeof(bt_mock_device_t));
            ESP_LOGI(TAG, "Mock connected to device: %s", addr);
            return ESP_OK;
        }
    }
    
    return ESP_ERR_NOT_FOUND;
}

esp_err_t bt_mock_disconnect(void) {
    if (!mock_connected) {
        return ESP_ERR_INVALID_STATE;
    }
    
    mock_connected = false;
    ESP_LOGI(TAG, "Mock disconnected from device: %02x:%02x:%02x:%02x:%02x:%02x",
            connected_device.addr[0], connected_device.addr[1], connected_device.addr[2],
            connected_device.addr[3], connected_device.addr[4], connected_device.addr[5]);
    
    return ESP_OK;
}

bool bt_mock_is_connected(void) {
    return mock_connected;
}

void bt_mock_set_ssp_support(bool supported) {
    ESP_LOGI(TAG, "Set SSP support: %d", supported);
    ssp_supported = supported;
}

bool bt_mock_get_ssp_support(void) {
    return ssp_supported;
}

esp_err_t bt_mock_simulate_pin_failure(void) {
    ESP_LOGI(TAG, "Simulating PIN failure");
    pin_failure_simulated = true;
    return ESP_OK;
}

esp_err_t bt_mock_simulate_pairing_timeout(void) {
    ESP_LOGI(TAG, "Simulating pairing timeout");
    pairing_timeout_simulated = true;
    return ESP_OK;
}

esp_err_t bt_mock_simulate_ssp_request(uint32_t passkey) {
    ESP_LOGI(TAG, "Simulating SSP request with passkey: %u", passkey);
    ssp_request_simulated = true;
    return ESP_OK;
}

// Add this function to get the mock device count
int bt_mock_get_device_count(void) {
    return mock_device_count;
}

// Add this function to get the mock devices
esp_err_t bt_mock_get_devices(bt_mock_device_t *devices, int max_count, int *actual_count) {
    int count = (max_count < mock_device_count) ? max_count : mock_device_count;
    
    memcpy(devices, mock_devices, count * sizeof(bt_mock_device_t));
    *actual_count = count;
    
    return ESP_OK;
}
