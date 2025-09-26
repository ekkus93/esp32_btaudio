#include "bt_test_setup.h"
#include "test_config.h"
#include "esp_log.h"

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
    
    // Create a paired device
    bt_device_t paired_device = {
        .addr = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66},
        .type = BT_DEVICE_TYPE_AUDIO,
        .paired = true,
    };
    strcpy((char*)paired_device.name, TEST_DEVICE_NAME);
    
    // Add as paired device
    bt_mock_add_paired_device(&paired_device);
}
