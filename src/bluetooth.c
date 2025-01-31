#include "bluetooth.h"  // Include the Bluetooth header
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"  // Ensure A2DP API is included
#include "nvs_flash.h"
#include "esp_log.h"
#include <string.h>  // Include for memset and strcmp
#include <math.h>    // Include for sin function
#include <stdlib.h>  // Include for malloc and free

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define TAG "BT_APP"
#define MAX_DEVICES 50
#define SAMPLE_RATE 44100
#define NOTE_DURATION 30  // Duration in seconds

typedef struct {
    uint8_t bda[ESP_BD_ADDR_LEN];
    char name[ESP_BT_GAP_MAX_BDNAME_LEN + 1];
} discovered_device_t;

static discovered_device_t discovered_devices[MAX_DEVICES];
static int num_discovered_devices = 0;

static SemaphoreHandle_t bt_mutex = NULL;
static bool pin_required = false;

void gap_event_handler(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param);
static void a2dp_sink_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param);

void audio_data_cb(uint8_t *data, uint32_t len);  // Add this declaration

// GAP event handler implementation
void gap_event_handler(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) {
    ESP_LOGI(TAG, "gap_event_handler: event = %d", event);
    switch (event) {
        case ESP_BT_GAP_DISC_RES_EVT: {
            char bda_str[18];
            uint8_t eir_length = 0;
            uint8_t *eir_name = NULL;

            for (int i = 0; i < param->disc_res.num_prop; i++) {
                esp_bt_gap_dev_prop_t *p = &param->disc_res.prop[i];
                if (p->type == ESP_BT_GAP_DEV_PROP_EIR) {
                    eir_name = esp_bt_gap_resolve_eir_data((uint8_t *)p->val, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &eir_length);
                    break;
                }
            }

            if (eir_name) {
                strncpy(bda_str, (char *)eir_name, eir_length);
                bda_str[eir_length] = '\0';  // Null-terminate the string
            } else {
                strcpy(bda_str, "Unknown");
            }

            bool device_found = false;
            for (int i = 0; i < num_discovered_devices; i++) {
                if (memcmp(discovered_devices[i].bda, param->disc_res.bda, ESP_BD_ADDR_LEN) == 0) {
                    device_found = true;
                    break;
                }
            }

            if (!device_found && num_discovered_devices < MAX_DEVICES) {
                memcpy(discovered_devices[num_discovered_devices].bda, param->disc_res.bda, ESP_BD_ADDR_LEN);
                strncpy(discovered_devices[num_discovered_devices].name, bda_str, ESP_BT_GAP_MAX_BDNAME_LEN);
                discovered_devices[num_discovered_devices].name[ESP_BT_GAP_MAX_BDNAME_LEN] = '\0';  // Null-terminate the string
                num_discovered_devices++;
            }

            ESP_LOGI(TAG, "Device found: %02x:%02x:%02x:%02x:%02x:%02x, Name: %s",
                     param->disc_res.bda[0], param->disc_res.bda[1], param->disc_res.bda[2],
                     param->disc_res.bda[3], param->disc_res.bda[4], param->disc_res.bda[5], bda_str);
            break;
        }
        case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
            if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
                ESP_LOGI(TAG, "Discovery stopped.");
                // Print the list of discovered devices
                ESP_LOGI(TAG, "Discovered devices:");
                for (int i = 0; i < num_discovered_devices; i++) {
                    ESP_LOGI(TAG, "MAC: %02x:%02x:%02x:%02x:%02x:%02x, Name: %s",
                             discovered_devices[i].bda[0], discovered_devices[i].bda[1], discovered_devices[i].bda[2],
                             discovered_devices[i].bda[3], discovered_devices[i].bda[4], discovered_devices[i].bda[5],
                             discovered_devices[i].name);
                }
                // Reset the discovered devices list
                num_discovered_devices = 0;
                memset(discovered_devices, 0, sizeof(discovered_devices));
            } else if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED) {
                ESP_LOGI(TAG, "Discovery started.");
            }
            break;
        case ESP_BT_GAP_KEY_NOTIF_EVT:
            ESP_LOGI(TAG, "Passkey Notification: %lu", (unsigned long)param->key_notif.passkey);
            break;
        case ESP_BT_GAP_KEY_REQ_EVT:
            ESP_LOGI(TAG, "Passkey Request");
            break;
        case ESP_BT_GAP_AUTH_CMPL_EVT:
            ESP_LOGI(TAG, "ESP_BT_GAP_AUTH_CMPL_EVT");
            if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
                ESP_LOGI(TAG, "Authentication success: %s", param->auth_cmpl.device_name);
                ESP_LOGI(TAG, "MAC: %02x:%02x:%02x:%02x:%02x:%02x",
                         param->auth_cmpl.bda[0], param->auth_cmpl.bda[1], param->auth_cmpl.bda[2],
                         param->auth_cmpl.bda[3], param->auth_cmpl.bda[4], param->auth_cmpl.bda[5]);
                ESP_LOGI(TAG, "Successfully paired with device: %02x:%02x:%02x:%02x:%02x:%02x",
                         param->auth_cmpl.bda[0], param->auth_cmpl.bda[1], param->auth_cmpl.bda[2],
                         param->auth_cmpl.bda[3], param->auth_cmpl.bda[4], param->auth_cmpl.bda[5]);

                // Play middle C note for 30 seconds
                ESP_LOGI(TAG, "Playing middle C note for 30 seconds");
                uint32_t sample_count = SAMPLE_RATE * NOTE_DURATION;
                uint8_t *audio_data = (uint8_t*) malloc(sample_count);
                if (audio_data == NULL) {
                    ESP_LOGE(TAG, "Failed to allocate memory for audio data");
                    break;
                }
                for (uint32_t i = 0; i < sample_count; i++) {
                    audio_data[i] = (uint8_t)(127.5 * (1 + sin(2 * M_PI * 261.63 * i / SAMPLE_RATE)));
                }
                audio_data_cb(audio_data, sample_count);
                free(audio_data);
            } else {
                ESP_LOGE(TAG, "Authentication failed, status: %d", param->auth_cmpl.stat);
                ESP_LOGE(TAG, "Failed to pair with device: %02x:%02x:%02x:%02x:%02x:%02x",
                         param->auth_cmpl.bda[0], param->auth_cmpl.bda[1], param->auth_cmpl.bda[2],
                         param->auth_cmpl.bda[3], param->auth_cmpl.bda[4], param->auth_cmpl.bda[5]);
            }
            break;
        case ESP_BT_GAP_CFM_REQ_EVT:
            ESP_LOGI(TAG, "Confirmation Request: Please confirm the passkey");
            esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
            break;
        case ESP_BT_GAP_PIN_REQ_EVT:
            ESP_LOGI(TAG, "PIN Code Request");
            if (pin_required) {
                esp_bt_gap_pin_reply(param->pin_req.bda, true, 0, NULL);
            } else {
                esp_bt_gap_pin_reply(param->pin_req.bda, false, 0, NULL);
            }
            break;
        default:
            break;
    }
}

esp_err_t bluetooth_pair_device(const char *mac_str, bool require_pin) {
    esp_bd_addr_t bd_addr;
    if (sscanf(mac_str, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
               &bd_addr[0], &bd_addr[1], &bd_addr[2],
               &bd_addr[3], &bd_addr[4], &bd_addr[5]) != 6) {
        ESP_LOGE(TAG, "Invalid MAC address format");
        return ESP_ERR_INVALID_ARG;
    }

    pin_required = require_pin;

    uint8_t iocap = ESP_BT_IO_CAP_IO;
    esp_err_t ret = esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &iocap, sizeof(uint8_t));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set security parameters: %s", esp_err_to_name(ret));
        return ret;
    }

    // Set the PIN code if required
    if (pin_required) {
        esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_FIXED;
        esp_bt_pin_code_t pin_code;
        memset(pin_code, '0', sizeof(pin_code));  // Example PIN code: "0000"
        ret = esp_bt_gap_set_pin(pin_type, sizeof(pin_code), pin_code);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set PIN code: %s", esp_err_to_name(ret));
            return ret;
        }
    }

    // Initiate bonding (pairing) with the device
    ret = esp_bt_gap_set_pin(ESP_BT_PIN_TYPE_FIXED, 0, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initiate pairing: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Pairing initiated with device: %02x:%02x:%02x:%02x:%02x:%02x",
             bd_addr[0], bd_addr[1], bd_addr[2], bd_addr[3], bd_addr[4], bd_addr[5]);

    return ESP_OK;
}

static esp_err_t init_nvs(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

static esp_err_t init_bt_controller(void) {
    esp_err_t ret;
    
    // First release BLE memory since we're using Dual Mode
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));
    
    // Get default config and modify for Dual Mode
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    bt_cfg.mode = ESP_BT_MODE_BTDM;  // Changed from ESP_BT_MODE_CLASSIC_BT to ESP_BT_MODE_BTDM
    bt_cfg.bt_max_acl_conn = 3;      // Number of ACL connections
    bt_cfg.bt_max_sync_conn = 3;     // Number of SCO connections
    
    // Initialize controller
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        ESP_LOGE(TAG, "Bluetooth controller initialize failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Enable controller in Dual Mode
    ret = esp_bt_controller_enable(ESP_BT_MODE_BTDM);
    if (ret) {
        ESP_LOGE(TAG, "Bluetooth controller enable failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    return ESP_OK;
}

static esp_err_t init_bluedroid(void) {
    esp_err_t ret = esp_bluedroid_init();
    if (ret) {
        ESP_LOGE(TAG, "%s initialize bluedroid failed: %s\n", __func__, esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bluedroid_enable();
    if (ret) {
        ESP_LOGE(TAG, "%s enable bluedroid failed: %s\n", __func__, esp_err_to_name(ret));
        return ret;
    }
    return ESP_OK;
}

static esp_err_t init_bt_gap(void) {
    esp_err_t ret = esp_bt_gap_register_callback(gap_event_handler);
    if (ret) {
        ESP_LOGE(TAG, "%s gap register failed: %s\n", __func__, esp_err_to_name(ret));
        return ret;
    }

    // Set the scan mode to connectable and discoverable
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);

    return ESP_OK;
}

static esp_err_t init_a2dp(void) {
    // Ensure you are using esp_a2d_sink_init
    esp_err_t ret = esp_a2d_sink_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize A2DP sink: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

esp_err_t bluetooth_init(void) {
    // Initialize mutex
    bt_mutex = xSemaphoreCreateMutex();
    if (bt_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create Bluetooth mutex");
        return ESP_FAIL;
    }

    // Initialize components
    ESP_ERROR_CHECK(init_nvs());

    // Deinitialize Bluetooth controller if already initialized
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED) {
        ESP_ERROR_CHECK(esp_bt_controller_disable());
    }
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_INITED) {
        ESP_ERROR_CHECK(esp_bt_controller_deinit());
    }

    ESP_ERROR_CHECK(init_bt_controller());
    ESP_ERROR_CHECK(init_bluedroid());
    ESP_ERROR_CHECK(init_bt_gap());
    ESP_ERROR_CHECK(init_a2dp());

    return ESP_OK;
}

esp_err_t bluetooth_start_discovery(void) {
    if (xSemaphoreTake(bt_mutex, portMAX_DELAY)) {
        // Check if Bluetooth controller is enabled
        esp_bt_controller_status_t status = esp_bt_controller_get_status();
        if (status != ESP_BT_CONTROLLER_STATUS_ENABLED) {
            ESP_LOGE(TAG, "Bluetooth controller is not enabled, status: %d", status);
            xSemaphoreGive(bt_mutex);
            return ESP_ERR_INVALID_STATE;
        }

        // Set the inquiry duration to 30 seconds
        esp_err_t ret = esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 30, 0);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start discovery: %s", esp_err_to_name(ret));
        }
        xSemaphoreGive(bt_mutex);
        return ret;
    }
    return ESP_FAIL;
}

void audio_data_cb(uint8_t *data, uint32_t len) {
    // Implement the audio data callback function
    // For example, you can send the audio data to a Bluetooth audio sink
    ESP_LOGI(TAG, "Audio data callback: length = %lu", (unsigned long)len);  // Use %lu for uint32_t
    // Add your code here to handle the audio data
}

static void a2dp_sink_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param) {
    ESP_LOGI(TAG, "A2DP Sink callback event: %d", event);
    // Add proper implementation here if needed
}
