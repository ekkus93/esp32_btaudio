#include "bluetooth.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>  // ...existing includes...
#include "esp_bt_defs.h"
#include <math.h>  // Add this for sinf()

#define TAG "BT_APP"
#define MAX_DEVICES 50
#define BT_APP_STACK_UP_EVT 0x0000    // << New definition

// Define test tone parameters
#define SAMPLE_RATE     44100
#define TONE_FREQUENCY  440  // 440 Hz (A4 note)

// Forward declarations of callback functions
static int32_t a2dp_source_data_cb(uint8_t *data, int32_t len);
void bt_app_a2d_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param);

typedef struct {
    uint8_t bda[ESP_BD_ADDR_LEN];
    char name[ESP_BT_GAP_MAX_BDNAME_LEN + 1];
} discovered_device_t;

static discovered_device_t discovered_devices[MAX_DEVICES];
static int num_discovered_devices = 0;

static bool pin_required = false;
static esp_bd_addr_t pending_pair_addr = {0};

// Add state tracking like the example
enum {
    APP_AV_STATE_IDLE,
    APP_AV_STATE_DISCOVERING,
    APP_AV_STATE_DISCOVERED,
    APP_AV_STATE_UNCONNECTED,
    APP_AV_STATE_CONNECTING,
    APP_AV_STATE_CONNECTED,
    APP_AV_STATE_DISCONNECTING,
};

enum {
    APP_AV_MEDIA_STATE_IDLE,
    APP_AV_MEDIA_STATE_STARTING,
    APP_AV_MEDIA_STATE_STARTED,
    APP_AV_MEDIA_STATE_STOPPING,
};

static int s_a2d_state = APP_AV_STATE_IDLE;
static int s_media_state = APP_AV_MEDIA_STATE_IDLE;

void gap_event_handler(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param);

void audio_data_cb(uint8_t *data, uint32_t len) {
    ESP_LOGI(TAG, "audio_data_cb: Received %lu bytes", (unsigned long)len);
    ESP_LOGD(TAG, "audio_data_cb: length=%lu, first bytes=%02x %02x %02x",
             (unsigned long)len, data[0], data[1], data[2]);
}

void gap_event_handler(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) {
    ESP_LOGD(TAG, "GAP event handler called with event: %d", event);
    switch (event) {
        case ESP_BT_GAP_DISC_RES_EVT: {
            // Log raw device address and property count
            ESP_LOGD(TAG, "DISC_RES: num_prop=%d", param->disc_res.num_prop);
            ESP_LOG_BUFFER_HEX(TAG, param->disc_res.bda, ESP_BD_ADDR_LEN);
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
                bda_str[eir_length] = '\0';
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
                discovered_devices[num_discovered_devices].name[ESP_BT_GAP_MAX_BDNAME_LEN] = '\0';
                num_discovered_devices++;
            }

            ESP_LOGI(TAG, "Device found: %02x:%02x:%02x:%02x:%02x:%02x, Name: %s",
                     param->disc_res.bda[0], param->disc_res.bda[1], param->disc_res.bda[2],
                     param->disc_res.bda[3], param->disc_res.bda[4], param->disc_res.bda[5], bda_str);
            break;
        }
        case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
            ESP_LOGD(TAG, "DISC_STATE_CHANGED: state=%d", param->disc_st_chg.state);
            if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
                ESP_LOGI(TAG, "Discovery stopped.");
                ESP_LOGI(TAG, "Discovered devices:");
                for (int i = 0; i < num_discovered_devices; i++) {
                    ESP_LOGI(TAG, "MAC: %02x:%02x:%02x:%02x:%02x:%02x, Name: %s",
                             discovered_devices[i].bda[0], discovered_devices[i].bda[1], discovered_devices[i].bda[2],
                             discovered_devices[i].bda[3], discovered_devices[i].bda[4], discovered_devices[i].bda[5],
                             discovered_devices[i].name);
                }
                num_discovered_devices = 0;
                memset(discovered_devices, 0, sizeof(discovered_devices));
            }
            break;

        case ESP_BT_GAP_RMT_SRVCS_EVT:
            ESP_LOGI(TAG, "Remote device services discovered");
            break;

        case ESP_BT_GAP_RMT_SRVC_REC_EVT:
            ESP_LOGI(TAG, "Remote device service record");
            break;

        case ESP_BT_GAP_AUTH_CMPL_EVT:
            ESP_LOGD(TAG, "AUTH_CMPL: status=%d", param->auth_cmpl.stat);
            if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
                ESP_LOGI(TAG, "Authentication success: %s", param->auth_cmpl.device_name);
                // ...existing success handling...
            } else {
                ESP_LOGE(TAG, "Authentication failed, status: %d", param->auth_cmpl.stat);
            }
            break;

        case ESP_BT_GAP_PIN_REQ_EVT:
            ESP_LOGI(TAG, "PIN request event received");
            // If no PIN required or fallback, always reply with empty PIN (legacy mode)
            if (!pin_required) {
                ESP_LOGI(TAG, "Legacy pairing: replying with empty PIN");
                esp_bt_pin_code_t legacy_pin = {0};
                esp_bt_gap_pin_reply(param->pin_req.bda, true, 0, legacy_pin);
            } else {
                ESP_LOGI(TAG, "Legacy pairing with fixed PIN 1234");
                esp_bt_pin_code_t pin_code = { '1', '2', '3', '4' };
                esp_bt_gap_pin_reply(param->pin_req.bda, true, 4, pin_code);
            }
            break;

        case ESP_BT_GAP_CFM_REQ_EVT:
            ESP_LOGI(TAG, "SSP confirmation request: auto-confirming.");
            esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
            break;

        case ESP_BT_GAP_KEY_NOTIF_EVT:
            // ...existing key notification code...
            break;

        case ESP_BT_GAP_KEY_REQ_EVT:
            // ...existing key request code...
            break;

        case ESP_BT_GAP_READ_RSSI_DELTA_EVT:
        case ESP_BT_GAP_CONFIG_EIR_DATA_EVT:
        case ESP_BT_GAP_SET_AFH_CHANNELS_EVT:
        case ESP_BT_GAP_READ_REMOTE_NAME_EVT:
        case ESP_BT_GAP_MODE_CHG_EVT:
        case ESP_BT_GAP_REMOVE_BOND_DEV_COMPLETE_EVT:
        case ESP_BT_GAP_QOS_CMPL_EVT:
            ESP_LOGI(TAG, "ACL event received, ignoring pairing fallback.");
            break;
        case ESP_BT_GAP_ACL_CONN_CMPL_STAT_EVT:
        case ESP_BT_GAP_ACL_DISCONN_CMPL_STAT_EVT:
            ESP_LOGI(TAG, "ACL event received, ignoring pairing fallback.");
            break;
        case ESP_BT_GAP_SET_PAGE_TO_EVT:
        case ESP_BT_GAP_GET_PAGE_TO_EVT:
        case ESP_BT_GAP_ACL_PKT_TYPE_CHANGED_EVT:
        case ESP_BT_GAP_ENC_CHG_EVT:
        case ESP_BT_GAP_SET_MIN_ENC_KEY_SIZE_EVT:
        case ESP_BT_GAP_GET_DEV_NAME_CMPL_EVT:
            ESP_LOGI(TAG, "Unhandled GAP event: %d", event);
            break;

        default:
            ESP_LOGD(TAG, "Unhandled GAP event %d, raw data:", event);
            ESP_LOG_BUFFER_HEX(TAG, (uint8_t *)param, sizeof(*param));
            break;
    }
}

// Define the A2DP callback function
void bt_app_a2d_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param) {
    ESP_LOGI(TAG, "A2DP callback event: %d", event);
    switch (event) {
        case ESP_A2D_CONNECTION_STATE_EVT:
            ESP_LOGI(TAG, "A2DP connection state: %d", param->conn_stat.state);
            if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
                ESP_LOGI(TAG, "A2DP connected");
                s_a2d_state = APP_AV_STATE_CONNECTED;
                s_media_state = APP_AV_MEDIA_STATE_IDLE;
                
                // Start media after connection
                ESP_LOGI(TAG, "Starting media playback...");
                esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY);
            } else if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
                ESP_LOGI(TAG, "A2DP disconnected");
                s_a2d_state = APP_AV_STATE_UNCONNECTED;
                s_media_state = APP_AV_MEDIA_STATE_IDLE;
            }
            break;
        case ESP_A2D_AUDIO_STATE_EVT:
            ESP_LOGI(TAG, "A2DP audio state: %d", param->audio_stat.state);
            if (param->audio_stat.state == ESP_A2D_AUDIO_STATE_STARTED) {
                s_media_state = APP_AV_MEDIA_STATE_STARTED;
            } else if (param->audio_stat.state == ESP_A2D_AUDIO_STATE_STOPPED) {
                s_media_state = APP_AV_MEDIA_STATE_IDLE;
            }
            break;
        case ESP_A2D_MEDIA_CTRL_ACK_EVT:
            ESP_LOGI(TAG, "A2DP media control ACK: %d", param->media_ctrl_stat.cmd);
            if (param->media_ctrl_stat.cmd == ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY &&
                param->media_ctrl_stat.status == ESP_A2D_MEDIA_CTRL_ACK_SUCCESS) {
                esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START);
            }
            break;
        // Handle all other A2DP events
        case ESP_A2D_AUDIO_CFG_EVT:
        case ESP_A2D_PROF_STATE_EVT:
        case ESP_A2D_SNK_PSC_CFG_EVT:
        case ESP_A2D_SNK_SET_DELAY_VALUE_EVT:
        case ESP_A2D_SNK_GET_DELAY_VALUE_EVT:
        case ESP_A2D_REPORT_SNK_DELAY_VALUE_EVT:
            ESP_LOGI(TAG, "Unhandled A2DP event: %d", event);
            break;
        default:
            ESP_LOGI(TAG, "Unhandled A2DP event: %d", event);
            break;
    }
}

// Generate a sine wave test tone
static int32_t a2dp_source_data_cb(uint8_t *data, int32_t len) {
    static float phase = 0.0f;
    static const float phase_inc = 2.0f * M_PI * TONE_FREQUENCY / SAMPLE_RATE;
    
    // Each sample is 16-bit (2 bytes)
    int16_t *samples = (int16_t*)data;
    int num_samples = len / 2;  // 2 bytes per sample
    
    // Generate sine wave
    for (int i = 0; i < num_samples; i++) {
        samples[i] = (int16_t)(32767.0f * sinf(phase));  // Scale to 16-bit range
        phase += phase_inc;
        if (phase >= 2.0f * M_PI) {
            phase -= 2.0f * M_PI;
        }
    }
    
    return len;
}

esp_err_t bluetooth_start_discovery(void) {
    ESP_LOGI(TAG, "###Starting Bluetooth device discovery");
    esp_err_t ret = esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 30, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start discovery: %s", esp_err_to_name(ret));
    }
    ESP_LOGI(TAG, "###Leaving bluetooth_start_discovery");
    return ret;
}

// Add more detailed logging in the pairing function
esp_err_t bluetooth_pair_device(const char *mac_str, bool require_pin) {
    esp_bd_addr_t bd_addr;
    if (sscanf(mac_str, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", 
               &bd_addr[0], &bd_addr[1], &bd_addr[2], 
               &bd_addr[3], &bd_addr[4], &bd_addr[5]) != 6) {
        ESP_LOGE(TAG, "Invalid MAC address format");
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(pending_pair_addr, bd_addr, ESP_BD_ADDR_LEN);
    pin_required = require_pin;

    ESP_LOGD(TAG, "Pairing with MAC=%02x:%02x:%02x:%02x:%02x:%02x, require_pin=%s",
             bd_addr[0], bd_addr[1], bd_addr[2],
             bd_addr[3], bd_addr[4], bd_addr[5],
             require_pin ? "true" : "false");

    // Use an alternative security profile: change IO capability from ESP_BT_IO_CAP_IO to ESP_BT_IO_CAP_OUT
    esp_bt_io_cap_t io_cap = ESP_BT_IO_CAP_OUT; // Previously: ESP_BT_IO_CAP_IO
    esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &io_cap, sizeof(uint8_t));

    s_a2d_state = APP_AV_STATE_CONNECTING;
    
    // First establish connection without security
    esp_err_t ret = esp_a2d_source_connect(bd_addr);  // Changed to source
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initiate connection: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

// Changed from static to non-static since it's now exposed in the header
esp_err_t init_a2dp(void) {
    ESP_LOGI(TAG, "###Initializing A2DP source - 1");
    
    // A2DP is already initialized in bluetooth_init()
    // Just verify the state
    if (s_a2d_state == APP_AV_STATE_IDLE) {
        s_a2d_state = APP_AV_STATE_DISCOVERING;
        ESP_LOGI(TAG, "A2DP source initialization complete");
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "A2DP in invalid state: %d", s_a2d_state);
        return ESP_ERR_INVALID_STATE;
    }
}

// Add Bluetooth initialization function
esp_err_t bluetooth_init(void) {
    esp_err_t ret;

    ESP_LOGI(TAG, "###Initializing Bluetooth stack - 1");

    // Initialize NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "###Initializing Bluetooth stack - 2");

    // Initialize controller with default config
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        ESP_LOGE(TAG, "Bluetooth controller init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "###Initializing Bluetooth stack - 3");

    ret = esp_bt_controller_enable(ESP_BT_MODE_BTDM);
    if (ret) {
        ESP_LOGE(TAG, "Bluetooth controller enable failed: %s", esp_err_to_name(ret));
        esp_bt_controller_deinit();
        return ret;
    }
    ESP_LOGI(TAG, "###Initializing Bluetooth stack - 4");

    ret = esp_bluedroid_init();
    if (ret) {
        ESP_LOGE(TAG, "Bluedroid init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "###Initializing Bluetooth stack - 5");

    ret = esp_bluedroid_enable();
    if (ret) {
        ESP_LOGE(TAG, "Bluedroid enable failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "###Initializing Bluetooth stack - 6");

    // 1. Initialize AVRCP controller first
    ret = esp_avrc_ct_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "AVRCP controller init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 2. Register AVRCP callback
    ret = esp_avrc_ct_register_callback(avrc_ct_callback);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "AVRCP callback register failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 3. Initialize AVRCP target
    ret = esp_avrc_tg_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "AVRCP target init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 4. Configure AVRCP target features
    esp_avrc_rn_evt_cap_mask_t evt_set = {0};
    esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET, &evt_set, ESP_AVRC_RN_VOLUME_CHANGE);
    esp_avrc_tg_set_rn_evt_cap(&evt_set);

    // 5. Initialize A2DP source
    ret = esp_a2d_source_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "A2DP source init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 6. Register A2DP callbacks
    esp_a2d_register_callback(bt_app_a2d_cb);
    esp_a2d_source_register_data_callback(a2dp_source_data_cb);

    // 7. Set device name
    ret = esp_bt_gap_set_device_name("ESP_A2DP_SRC");
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Set device name failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 8. Set Class of Device
    esp_bt_cod_t cod = {
        .major = 0x01,
        .minor = 0x03,
        .service = 0x24
    };
    ret = esp_bt_gap_set_cod(cod, ESP_BT_SET_COD_ALL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Set Class of Device failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 9. Register GAP callback and set scan mode
    esp_bt_gap_register_callback(gap_event_handler);
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);

    ESP_LOGI(TAG, "Bluetooth stack initialized successfully");
    return ESP_OK;
}

// AVRCP controller callback
void avrc_ct_callback(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *param) {
    ESP_LOGD(TAG, "AVRC controller event: %d", event);
    switch (event) {
        case ESP_AVRC_CT_CONNECTION_STATE_EVT:
            ESP_LOGI(TAG, "AVRC controller connection state: %d", param->conn_stat.connected);
            if (param->conn_stat.connected) {
                // Get AVRCP capabilities after connection
                esp_avrc_ct_send_get_rn_capabilities_cmd(0);
            }
            break;
        case ESP_AVRC_CT_REMOTE_FEATURES_EVT:
            ESP_LOGI(TAG, "AVRC remote features: 0x%" PRIx32, param->rmt_feats.feat_mask);
            break;
        default:
            ESP_LOGW(TAG, "Unhandled AVRC controller event: %d", event);
            break;
    }
}

// Add the bt_av_hdl_stack_evt function to handle the stack event
void bt_av_hdl_stack_evt(uint16_t event, void *p_param) {
    ESP_LOGD(TAG, "%s event: %d", __func__, event);

    switch (event) {
        case BT_APP_STACK_UP_EVT: {
            // Set device name
            esp_bt_gap_set_device_name("ESP_A2DP_SRC");

            // Register GAP callback
            esp_bt_gap_register_callback(gap_event_handler);

            // Set scan mode
            esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
            break;
        }
        default:
            ESP_LOGE(TAG, "%s unhandled event: %d", __func__, event);
            break;
    }
}

// Function to disconnect from a paired device
esp_err_t bluetooth_disconnect_device(void) {
    if (s_a2d_state == APP_AV_STATE_CONNECTED) {
        esp_err_t ret = esp_a2d_source_disconnect(pending_pair_addr);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to disconnect: %s", esp_err_to_name(ret));
            return ret;
        }
        ESP_LOGI(TAG, "Disconnected from device");
        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "No device is currently connected");
        return ESP_ERR_INVALID_STATE;
    }
}

// Function to unpair a device
esp_err_t bluetooth_unpair_device(void) {
    if (s_a2d_state == APP_AV_STATE_CONNECTED || s_a2d_state == APP_AV_STATE_CONNECTING) {
        esp_err_t ret = esp_bt_gap_remove_bond_device(pending_pair_addr);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to unpair: %s", esp_err_to_name(ret));
            return ret;
        }
        ESP_LOGI(TAG, "Unpaired device");
        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "No device is currently paired");
        return ESP_ERR_INVALID_STATE;
    }
}

// Function to connect to a paired device
esp_err_t bluetooth_connect_device(const char *mac_str) {
    esp_bd_addr_t bd_addr;
    if (sscanf(mac_str, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", 
               &bd_addr[0], &bd_addr[1], &bd_addr[2], 
               &bd_addr[3], &bd_addr[4], &bd_addr[5]) != 6) {
        ESP_LOGE(TAG, "Invalid MAC address format");
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(pending_pair_addr, bd_addr, ESP_BD_ADDR_LEN);

    ESP_LOGD(TAG, "Connecting to MAC=%02x:%02x:%02x:%02x:%02x:%02x",
             bd_addr[0], bd_addr[1], bd_addr[2],
             bd_addr[3], bd_addr[4], bd_addr[5]);

    s_a2d_state = APP_AV_STATE_CONNECTING;
    
    // Establish connection
    esp_err_t ret = esp_a2d_source_connect(bd_addr);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initiate connection: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}