
#include "bluetooth/bt_app_global.h"
#include "bluetooth/bt_app_conn.h"
#include "bluetooth/bt_app_a2dp.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "nvs_flash.h"
#include "esp_log.h"

#define TAG "BT_APP_INIT"

esp_err_t bluetooth_init(void) {
    esp_err_t ret;

    SAFE_ESP_LOGI(TAG, "Initializing Bluetooth stack");

    // Initialize NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize controller with increased stack size
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    bt_cfg.controller_task_stack_size = 8192;  // Double the stack size
    bt_cfg.hci_uart_no = UART_NUM_1;  // Use UART1 instead of default
    bt_cfg.bt_max_acl_conn = 1; // We only need one connection
    bt_cfg.bt_max_sync_conn = 0; // Not using SCO
    bt_cfg.normal_adv_size = 10;

    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        SAFE_ESP_LOGE(TAG, "Bluetooth controller init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BTDM);
    if (ret) {
        SAFE_ESP_LOGE(TAG, "Bluetooth controller enable failed: %s", esp_err_to_name(ret));
        esp_bt_controller_deinit();
        return ret;
    }

    ret = esp_bluedroid_init();
    if (ret) {
        SAFE_ESP_LOGE(TAG, "Bluedroid init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bluedroid_enable();
    if (ret) {
        SAFE_ESP_LOGE(TAG, "Bluedroid enable failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Initialize AVRCP controller first
    ret = esp_avrc_ct_init();
    if (ret != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "AVRCP controller init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Register AVRCP callback
    ret = esp_avrc_ct_register_callback(avrc_ct_callback);
    if (ret != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "AVRCP callback register failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Initialize AVRCP target
    ret = esp_avrc_tg_init();
    if (ret != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "AVRCP target init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Configure AVRCP target features
    esp_avrc_rn_evt_cap_mask_t evt_set = {0};
    esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET, &evt_set, ESP_AVRC_RN_VOLUME_CHANGE);
    esp_avrc_tg_set_rn_evt_cap(&evt_set);

    // Initialize A2DP source
    ret = esp_a2d_source_init();
    if (ret != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "A2DP source init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Register A2DP callbacks
    esp_a2d_register_callback(bt_app_a2d_cb);
    esp_a2d_source_register_data_callback(a2dp_source_data_cb);

    // Set device name
    ret = esp_bt_gap_set_device_name("ESP_A2DP_SRC");
    if (ret != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "Set device name failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Set Class of Device
    esp_bt_cod_t cod = {
        .major = 0x01,
        .minor = 0x03,
        .service = 0x24
    };
    ret = esp_bt_gap_set_cod(cod, ESP_BT_SET_COD_ALL);
    if (ret != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "Set Class of Device failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Register GAP callback and set scan mode
    esp_bt_gap_register_callback(gap_event_handler);
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);

    // Initialize the volume from NVS
    nvs_handle_t nvs_handle;
    ret = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (ret == ESP_OK) {
        uint8_t vol;
        ret = nvs_get_u8(nvs_handle, BT_VOLUME_KEY, &vol);
        if (ret == ESP_ERR_NVS_NOT_FOUND) {
            nvs_set_u8(nvs_handle, BT_VOLUME_KEY, DEFAULT_VOLUME);
            nvs_commit(nvs_handle);
            s_current_volume = DEFAULT_VOLUME;
        } else if (ret == ESP_OK) {
            s_current_volume = vol;
        }
        nvs_close(nvs_handle);
    }

    SAFE_ESP_LOGI(TAG, "Bluetooth stack initialized successfully");
    return ESP_OK;
}
