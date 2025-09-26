#include "bt_mock_setup.h"
#include "test_config.h"
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
    
    // Reset BT mock system
    bt_mock_reset();
    
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
    bt_mock_reset();
    
    // Create a paired device - Fix the structure initialization to match bt_device_t definition
    bt_device_t paired_device;
    
    // Initialize the address (MAC address as bytes)
    uint8_t mac_addr[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    memcpy(paired_device.addr, mac_addr, sizeof(paired_device.addr));
    
    // Set other fields
    strcpy((char*)paired_device.name, TEST_DEVICE_NAME);
    paired_device.rssi = -50;  // Example signal strength
    paired_device.cod = 0;     // Class of device
    paired_device.paired = true;
    
    // Add as paired device
    bt_mock_add_paired_device(&paired_device);
}
