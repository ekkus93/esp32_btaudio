#include "bt_interface.h"

// Mock state variables
static bool mock_initialized = false;
static bool mock_scanning = false;
static bt_device_t mock_devices[20];
static int mock_device_count = 0;
// ... other state variables ...

static esp_err_t mock_init(void) {
    mock_initialized = true;
    return ESP_OK;
}

static esp_err_t mock_scan_start(void) {
    if (!mock_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    mock_scanning = true;
    return ESP_OK;
}

// ... implementations for all functions ...

// Export the mock implementation structure
bt_interface_t bt_mock_implementation = {
    .init = mock_init,
    .scan_start = mock_scan_start,
    // ... all other function pointers ...
};
