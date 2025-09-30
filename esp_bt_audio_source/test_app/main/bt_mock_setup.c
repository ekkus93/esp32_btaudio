#include "bt_mock_setup.h"
#include "test_config.h"
#include "bt_source_mock.h"
#include "esp_log.h"
#include <string.h>  // Add this for strcpy

static const char *TAG = "BT_MOCK_SETUP";

void bt_mock_setup_common(void)
{
    ESP_LOGI(TAG, "Setting up common BT test environment");
    
    // Make sure BT is initialized
    esp_err_t ret = bt_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize BT: %d", ret);
    }
    // Initialize component-level mock state if available so its internal
    // structures are allocated/zeroed before we call bt_mock_reset() and
    // populate devices. bt_mock_init is provided by the component mock.
    #if defined(BT_MOCK_PROVIDES_PROTOTYPES)
    bt_mock_init();
    #endif
    
    // Reset both the test-app stub state and the component-level mock state so
    // tests start from a clean slate. bt_reset_for_test() is a weak symbol
    // provided by the test-app stub implementation and clears stub-local
    // variables like connection/scan/pairing flags. bt_mock_reset() resets the
    // component-level mock state.
    bt_reset_for_test();
    bt_mock_reset();

    // Default to PIN pairing for tests unless a test explicitly enables SSP.
    // This makes tests that expect PIN-based flows (bt_send_pin_code) behave
    // deterministically.
    bt_mock_set_ssp_supported(false);
    
    // Add some test devices
    bt_mock_setup_devices();
}

void bt_mock_setup_devices(void)
{
    ESP_LOGI(TAG, "Setting up mock devices");
    
    // Add test audio device
    bt_mock_add_test_device(TEST_DEVICE_ADDR, TEST_DEVICE_NAME, BT_DEVICE_TYPE_AUDIO);
    
    // Add some additional devices for testing
    bt_mock_add_test_device("22:33:44:55:66:77", "Test_Phone", BT_DEVICE_TYPE_PHONE);
    bt_mock_add_test_device("33:44:55:66:77:88", "Test_Computer", BT_DEVICE_TYPE_OTHER);
}

void bt_mock_setup_paired_devices(void)
{
    ESP_LOGI(TAG, "Setting up paired devices");
    
    // Initialize BT and reset mock
    bt_init();
    #if defined(BT_MOCK_PROVIDES_PROTOTYPES)
    bt_mock_init();
    #endif
    bt_mock_reset();
    
    // Create a paired device - Fix the structure initialization to match bt_device_t definition
    bt_device_t paired_device = {0};
    const uint8_t mac_addr[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    memcpy(paired_device.addr, mac_addr, sizeof(mac_addr));
    strncpy(paired_device.name, TEST_DEVICE_NAME, sizeof(paired_device.name) - 1);
    paired_device.rssi = -50;  // Example signal strength
    paired_device.cod = 0x240404;     // Audio device class of device
    paired_device.paired = true;
    
    // Add as paired device
    bt_mock_add_paired_device(&paired_device);
}
