#include "test_config.h"
#include "bt_mock_devices.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "TEST_CONFIG";

// Define the correct pairing states based on the test expectations
#define BT_MOCK_PAIRING_STATE_PAIRED 2  // This is what the tests expect

// Define the actual interface structure with function pointers
static bt_interface_t mock_interface = {
    .reset = bt_mock_reset,
    .start_pairing = bt_mock_start_pairing,
    .send_pin = bt_mock_send_pin,
    .get_pairing_state = bt_mock_get_pairing_state,
    .get_pairing_method = bt_mock_get_pairing_method,
    .confirm_ssp = bt_mock_confirm_ssp,
    .is_device_paired = bt_mock_is_device_paired
    // Removed the unpair_device field that doesn't exist in bt_interface_t
};

// Return the implementation pointer when requested
bt_interface_t* get_bt_implementation(void) {
    ESP_LOGI(TAG, "Getting mock BT implementation");
    return &mock_interface;
}

// Setup function called during test initialization
void setup_mock_bt_implementation(void) {
    ESP_LOGI(TAG, "Setting up mock BT implementation");
    
    // Make sure we start with a clean state
    bt_mock_reset();
    
    // Setup default paired devices for testing
    uint8_t addr1[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    uint8_t addr2[6] = {0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
    uint8_t addr3[6] = {0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    
    // Add some test devices to the mock system
    bt_device_t device1 = {0};
    bt_device_t device2 = {0};
    bt_device_t device3 = {0};
    
    // Copy addresses
    memcpy(device1.addr, addr1, 6);
    memcpy(device2.addr, addr2, 6);
    memcpy(device3.addr, addr3, 6);
    
    // Set names
    strcpy(device1.name, "Test Speaker 1");
    strcpy(device2.name, "Test Speaker 2");
    strcpy(device3.name, "Test Device");
    
    // Add to paired devices
    bt_mock_add_paired_device(&device1);
    bt_mock_add_paired_device(&device2);
    bt_mock_add_paired_device(&device3);
    
    // Enable SSP support by default for tests
    bt_mock_set_ssp_supported(true);
    
    ESP_LOGI(TAG, "Mock BT implementation set up with 2 paired test devices");
}
