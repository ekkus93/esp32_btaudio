#include "bluetooth/bt_app_init.h"
#include "bluetooth/bt_app_global.h"
#include "bluetooth/bt_app_av.h"
#include "bluetooth/bt_app_conn.h"
#include "bluetooth/bt_app_a2dp.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "esp_gap_bt_api.h"

static const char *TAG = "BT_APP_INIT";

// Add function prototype here
void dump_bt_controller_status(void);

// Implementation of the bluetooth_init function
esp_err_t bluetooth_init(void) {
    esp_err_t ret;
    
    ESP_LOGI(TAG, "Initializing Bluetooth stack");
    
    // Initialize NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialized successfully");
    
    // Release any previous controller memory
    esp_bt_controller_mem_release(ESP_BT_MODE_BLE);
    ESP_LOGI(TAG, "Released BLE controller memory");
    
    // Initialize controller with default config
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();

    // IMPORTANT: Set consistent mode parameters that match sdkconfig
    // This is likely the source of the initialization error
    bt_cfg.mode = ESP_BT_MODE_CLASSIC_BT;  // Only use classic mode, not dual mode
    bt_cfg.bt_max_acl_conn = 2;
    bt_cfg.bt_max_sync_conn = 1;
    
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth controller init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Bluetooth controller initialized");
    
    // Enable controller in classic mode only - this matches our configuration above
    ret = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth controller enable failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Bluetooth controller enabled in classic mode");
    
    // Initialize Bluedroid stack
    ret = esp_bluedroid_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Bluedroid stack initialized");
    
    // Enable Bluedroid stack
    ret = esp_bluedroid_enable();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid enable failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Bluedroid stack enabled");
    
    // Set up Bluetooth classic protocols
    ret = esp_a2d_source_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "A2DP source init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "A2DP source initialized");
    
    // Register A2DP callback and data callback
    ret = esp_a2d_register_callback(&bt_app_a2d_cb);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "A2DP callback registration failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "A2DP callback registered");
    
    // THIS IS THE KEY LINE THAT WAS MISSING! Register the data callback
    ret = esp_a2d_source_register_data_callback(a2dp_source_data_cb);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "A2DP data callback registration failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "A2DP data callback registered successfully");
    
    // Initialize AVRCP controller
    ret = esp_avrc_ct_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "AVRC controller init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "AVRCP controller initialized");
    
    // Register GAP callback function
    ret = esp_bt_gap_register_callback(gap_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GAP register callback failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "GAP callback registered");
    
    // Create the mutex for Bluetooth operations
    if (s_bt_resource_mutex == NULL) {
        s_bt_resource_mutex = xSemaphoreCreateMutex();
        if (s_bt_resource_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create Bluetooth mutex");
            return ESP_FAIL;
        }
    }
    ESP_LOGI(TAG, "Bluetooth resource mutex created");
    
    ESP_LOGI(TAG, "Bluetooth stack initialized successfully");
    
    // Call the function to dump Bluetooth controller status
    dump_bt_controller_status();
    
    return ESP_OK;
}

// The function definition remains the same
void dump_bt_controller_status(void) {
    esp_bt_controller_status_t status = esp_bt_controller_get_status();
    const char *status_str = "";
    
    switch (status) {
        case ESP_BT_CONTROLLER_STATUS_IDLE:
            status_str = "IDLE";
            break;
        case ESP_BT_CONTROLLER_STATUS_INITED:
            status_str = "INITED";
            break;
        case ESP_BT_CONTROLLER_STATUS_ENABLED:
            status_str = "ENABLED";
            break;
        case ESP_BT_CONTROLLER_STATUS_NUM:
            status_str = "INVALID";
            break;
    }
    
    ESP_LOGI(TAG, "BT Controller Status: %s", status_str);
    
    // Get and display Bluetooth MAC address
    const uint8_t *mac = esp_bt_dev_get_address();
    if (mac) {
        ESP_LOGI(TAG, "BT MAC: %02x:%02x:%02x:%02x:%02x:%02x", 
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    } else {
        ESP_LOGW(TAG, "Could not get BT MAC address");
    }
}
