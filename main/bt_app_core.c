#include "bt_app_core.h"
#include "esp_log.h"

static uint8_t discovered_devices = 0;

esp_err_t bt_app_init(void) {
    esp_err_t ret;

    ESP_LOGI(BT_APP_TAG, "Initializing Bluetooth...");

    // Initialize NVS
    ret = esp_bt_controller_mem_release(ESP_BT_MODE_BLE);
    if (ret) {
        ESP_LOGE(BT_APP_TAG, "Failed to release BLE memory: %s", esp_err_to_name(ret));
        return ret;
    }

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    if ((ret = esp_bt_controller_init(&bt_cfg)) != ESP_OK) {
        ESP_LOGE(BT_APP_TAG, "Initialize controller failed: %s", esp_err_to_name(ret));
        return ret;
    }

    if ((ret = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT)) != ESP_OK) {
        ESP_LOGE(BT_APP_TAG, "Enable controller failed: %s", esp_err_to_name(ret));
        return ret;
    }

    if ((ret = esp_bluedroid_init()) != ESP_OK) {
        ESP_LOGE(BT_APP_TAG, "Initialize bluedroid failed: %s", esp_err_to_name(ret));
        return ret;
    }

    if ((ret = esp_bluedroid_enable()) != ESP_OK) {
        ESP_LOGE(BT_APP_TAG, "Enable bluedroid failed: %s", esp_err_to_name(ret));
        return ret;
    }

    if ((ret = esp_bt_gap_register_callback(bt_app_gap_cb)) != ESP_OK) {
        ESP_LOGE(BT_APP_TAG, "Register GAP callback failed: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

static bt_device_t discovered_devices[BT_SCAN_MAX_RESULTS];
static uint8_t num_devices = 0;

void bt_app_gap_start_discovery(void) {
    ESP_LOGI(BT_APP_TAG, "Starting discovery for %d seconds...", BT_SCAN_DURATION);
    num_devices = 0;
    esp_bt_gap_start_discovery(BT_SCAN_MODE, BT_SCAN_DURATION, 0);
}

void bt_app_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) {
    switch (event) {
        case ESP_BT_GAP_DISC_RES_EVT:
            if (num_devices < BT_SCAN_MAX_RESULTS) {
                memcpy(discovered_devices[num_devices].bda, param->disc_res.bda, ESP_BD_ADDR_LEN);
                for (int i = 0; i < param->disc_res.num_prop; i++) {
                    if (param->disc_res.prop[i].type == ESP_BT_GAP_DEV_PROP_NAME) {
                        memcpy(discovered_devices[num_devices].name, 
                               param->disc_res.prop[i].val, 
                               param->disc_res.prop[i].len);
                        discovered_devices[num_devices].name[param->disc_res.prop[i].len] = '\0';
                        ESP_LOGI(BT_APP_TAG, "Device found: %s", discovered_devices[num_devices].name);
                    }
                }
                num_devices++;
            }
            break;

        case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
            if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
                ESP_LOGI(BT_APP_TAG, "Discovery complete - %d devices found", num_devices);
                for (int i = 0; i < num_devices; i++) {
                    ESP_LOGI(BT_APP_TAG, "Device %d: %s", i + 1, discovered_devices[i].name);
                }
            }
            break;

        default:
            ESP_LOGI(BT_APP_TAG, "GAP event: %d", event);
            break;
    }
}
