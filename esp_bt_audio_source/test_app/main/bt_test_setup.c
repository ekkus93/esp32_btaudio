#include "bt_test_setup.h"
#include "test_config.h"
#include "bt_source_mock.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "BT_TEST_SETUP";

void bt_mock_setup_common(void)
{
    ESP_LOGI(TAG, "Setting up common BT test environment");
    
    // Make sure BT is initialized
    esp_err_t ret = bt_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize BT: %d", ret);
    }
    
    // Reset BT mock system
    bt_mock_reset();
    
    // Add some test devices
    bt_mock_setup_devices();
}

void bt_mock_setup_devices(void)
{
    ESP_LOGI(TAG, "Setting up mock devices");
    
    // Add test audio device. Prefer component-level authoritative helper
    // when available so other delegated calls (connect-by-name, scan)
    // observe the same device list.
#if defined(BT_MOCK_PROVIDES_PROTOTYPES)
    bt_mock_add_device(TEST_DEVICE_ADDR, TEST_DEVICE_NAME, BT_DEVICE_TYPE_AUDIO, false);
    bt_mock_add_device("22:33:44:55:66:77", "Test_Phone", BT_DEVICE_TYPE_PHONE, false);
    bt_mock_add_device("33:44:55:66:77:88", "Test_Computer", BT_DEVICE_TYPE_OTHER, false);
#else
    // Fallback to local test-app helper when component mock is not present
    bt_mock_add_test_device(TEST_DEVICE_ADDR, TEST_DEVICE_NAME, BT_DEVICE_TYPE_AUDIO);
    bt_mock_add_test_device("22:33:44:55:66:77", "Test_Phone", BT_DEVICE_TYPE_PHONE);
    bt_mock_add_test_device("33:44:55:66:77:88", "Test_Computer", BT_DEVICE_TYPE_OTHER);
#endif
}

void bt_mock_setup_paired_devices(void)
{
    ESP_LOGI(TAG, "Setting up paired devices");
    
    // Initialize BT and reset mock
    bt_init();
    bt_mock_reset();
    
    // Create a paired device
    bt_device_t paired_device = {0};
    const uint8_t addr_bytes[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    memcpy(paired_device.addr, addr_bytes, sizeof(addr_bytes));
    strncpy(paired_device.name, TEST_DEVICE_NAME, sizeof(paired_device.name) - 1);
    paired_device.rssi = -50;
    paired_device.cod = 0x240404; // Audio device class of device
    paired_device.paired = true;
    
    // Add as paired device
    bt_mock_add_paired_device(&paired_device);
}

void setup_mock_devices(void)
{
    ESP_LOGI(TAG, "setup_mock_devices shim invoked");
    bt_mock_setup_devices();
}
