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
#include <inttypes.h>  // Add this for PRId32
#include "esp_bt_device.h" // Add this line
#include "custom_log.h"
#include "esp_avrc_api.h"  // Include AVRCP API
#include "driver/uart.h"
#include "esp_timer.h"  // For esp_timer_get_time

// Reduce L2CAP buffer size (adjust as needed)
#define L2CAP_MTU 512  // Reduced from default
#define L2CAP_TX_BUF_SIZE 1024 // Reduced from default

#define TAG "BT_APP"
#define MAX_DEVICES 50
#define BT_APP_STACK_UP_EVT 0x0000    // << New definition
#define BT_DEVICE_NAME_KEY "bt_name"
#define DEFAULT_BT_DEVICE_NAME "monkfish"

// Define test tone parameters
#define SAMPLE_RATE     44100
#define TONE_FREQUENCY  440  // 440 Hz (A4 note)
#define TABLE_SIZE 100  // Precomputed sine table size
#define BEEP_DURATION_THRESHOLD (SAMPLE_RATE / 2)  // New: beep lasts about 0.5 seconds

// Add these additional constants
#define BT_VOLUME_KEY "bt_vol"
#define DEFAULT_VOLUME 32

// Forward declarations of callback functions
static int32_t a2dp_source_data_cb(uint8_t *data, int32_t len);
void bt_app_a2d_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param);

// Add this forward declaration after the other forward declarations at the top
static void memory_monitor_task(void *arg);

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

// Replace existing beep state variables:
static bool s_beep_in_progress = false;
static int s_beep_duration = 0;

// New global variables for sine lookup
static int16_t sine_table[TABLE_SIZE];
static bool sine_table_initialized = false;
static int s_beep_index = 0;

// Add this with the other global variables at the top of the file (after the defines)
static uint8_t s_current_volume = 0;

// Add these new congestion control variables after the existing global variables
#define MAX_CONGESTION_COUNT 5
static int s_congestion_count = 0;
static bool s_severe_congestion = false;
static uint32_t s_last_congestion_time = 0;
#define CONGESTION_RECOVERY_TIME_MS 2000 // Time to wait after severe congestion

// New helper function to initialize the sine_table once
static void init_sine_table(void) {
    for (int i = 0; i < TABLE_SIZE; i++) {
        float angle = 2.0f * M_PI * i / TABLE_SIZE;
        sine_table[i] = (int16_t)(32767.5f * sinf(angle));
    }
    sine_table_initialized = true;
}

// New helper function to trigger a beep.
static void trigger_beep(void) {
    s_beep_in_progress = true;
    s_beep_duration = 0;
    s_beep_index = 0;
    if (!sine_table_initialized) {
        init_sine_table();
    }
    ESP_LOGI(TAG, "Beep triggered (trigger_beep)");
}

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
// New task to handle beep processing on the other CPU with logging.
static void beep_task(void *params) {
    ESP_LOGI(TAG, "beep_task started on core %d", xPortGetCoreID());
    trigger_beep();
    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_LOGI(TAG, "beep_task completed, deleting task");
    vTaskDelete(NULL);
}

// In the A2DP callback, offload beep processing to core 1 on connection
static bool s_l2cap_congestion_flag = false;
static uint32_t s_last_operation_time = 0;
#define BT_OPERATION_DELAY_MS 300  // At least 300ms between BT operations

// Helper function to check if enough time has passed since last operation
static bool is_operation_time_ok(void) {
    uint32_t current_time = (uint32_t)(esp_timer_get_time() / 1000);
    if (current_time - s_last_operation_time < BT_OPERATION_DELAY_MS) {
        ESP_LOGW(TAG, "Operation attempted too soon after previous one");
        return false;
    }
    s_last_operation_time = current_time;
    return true;
}

// Replace the bt_app_a2d_cb function with a fixed version
// Add flag to track if the volume was initialized this boot cycle
static bool s_volume_initialized = false;

void bt_app_a2d_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param) {
    ESP_LOGI(TAG, "A2DP callback event: %d", event);
    
    switch (event) {
        case ESP_A2D_CONNECTION_STATE_EVT:
            ESP_LOGI(TAG, "A2DP connection state: %d", param->conn_stat.state);
            if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
                ESP_LOGI(TAG, "A2DP connected");
                s_a2d_state = APP_AV_STATE_CONNECTED;
                s_media_state = APP_AV_MEDIA_STATE_IDLE;
                
                xTaskCreatePinnedToCore(beep_task, "beep_task", 2048, NULL, 5, NULL, 1);
                
                // Reset congestion flag when connected
                s_l2cap_congestion_flag = false;
                
                // Check if we need to initialize the volume after connection
                if (!s_volume_initialized) {
                    // Get last saved volume from NVS, or use default
                    nvs_handle_t nvs_handle;
                    esp_err_t err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
                    if (err == ESP_OK) {
                        uint8_t vol = DEFAULT_VOLUME;
                        nvs_get_u8(nvs_handle, BT_VOLUME_KEY, &vol);
                        nvs_close(nvs_handle);
                        
                        // Set the initial volume with a delay to ensure connection is ready
                        s_current_volume = vol; // Set current volume immediately
                        
                        // Wait a bit before sending actual command to ensure AVRCP is ready
                        vTaskDelay(pdMS_TO_TICKS(500));
                        ESP_LOGI(TAG, "Setting initial volume to %d", vol);
                        bluetooth_set_volume(vol);
                        
                        s_volume_initialized = true;
                    }
                }
                
                // Start media after connection
                ESP_LOGI(TAG, "Requesting media playback...");
                esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY);
            } else if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
                ESP_LOGI(TAG, "A2DP disconnected");
                s_a2d_state = APP_AV_STATE_UNCONNECTED;
                s_media_state = APP_AV_MEDIA_STATE_IDLE;
            }
            break;
            
        case ESP_A2D_MEDIA_CTRL_ACK_EVT:
            ESP_LOGI(TAG, "A2DP media control ACK: cmd=%d, status=%d", param->media_ctrl_stat.cmd, param->media_ctrl_stat.status);
            if (param->media_ctrl_stat.cmd == ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY) {
                if (param->media_ctrl_stat.status == ESP_A2D_MEDIA_CTRL_ACK_SUCCESS) {
                    ESP_LOGI(TAG, "Media source ready, starting playback...");
                    esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START);
                } else {
                    ESP_LOGW(TAG, "Media source not ready, ACK status: %d", param->media_ctrl_stat.status);
                }
            } else if (param->media_ctrl_stat.status != ESP_A2D_MEDIA_CTRL_ACK_SUCCESS) {
                // If we get any error in media control, consider it a congestion
                s_l2cap_congestion_flag = true;
                s_last_congestion_time = (uint32_t)(esp_timer_get_time() / 1000);
                ESP_LOGW(TAG, "A2DP media control failed, possible congestion. Status: %d", 
                        param->media_ctrl_stat.status);
            } else {
                // Command completed successfully, clear any congestion flag
                s_l2cap_congestion_flag = false;
            }
            break;
            
        case ESP_A2D_AUDIO_STATE_EVT:
            ESP_LOGI(TAG, "A2D audio state: %d", param->audio_stat.state);
            if (param->audio_stat.state == ESP_A2D_AUDIO_STATE_STARTED) {
                s_media_state = APP_AV_MEDIA_STATE_STARTED;
                ESP_LOGI(TAG, "A2DP audio started");
                s_l2cap_congestion_flag = false; // Reset congestion flag when audio starts
            } else if (param->audio_stat.state == ESP_A2D_AUDIO_STATE_STOPPED) {
                s_media_state = APP_AV_MEDIA_STATE_IDLE;
                ESP_LOGI(TAG, "A2DP audio stopped");
            }
            break;
            
        // Handle all other A2DP events with a default case
        case ESP_A2D_AUDIO_CFG_EVT:
        case ESP_A2D_PROF_STATE_EVT:
        case ESP_A2D_SNK_PSC_CFG_EVT:
        case ESP_A2D_SNK_SET_DELAY_VALUE_EVT:
        case ESP_A2D_SNK_GET_DELAY_VALUE_EVT:
        case ESP_A2D_REPORT_SNK_DELAY_VALUE_EVT:
        default:
            ESP_LOGI(TAG, "Unhandled A2DP event: %d", event);
            break;
    }
}

// Update a2dp_source_data_cb to work with the simplified congestion detection
static int32_t a2dp_source_data_cb(uint8_t *data, int32_t len) {
    // Check for severe congestion condition
    if (s_severe_congestion) {
        uint32_t current_time = (uint32_t)(esp_timer_get_time() / 1000);
        if (current_time - s_last_congestion_time < CONGESTION_RECOVERY_TIME_MS) {
            // Still in recovery period - send silence
            memset(data, 0, len);
            return len;
        } else {
            // Recovery period over, try again
            s_severe_congestion = false;
            s_congestion_count = 0;
            ESP_LOGI(TAG, "Exiting severe congestion state after recovery time");
        }
    }
    
    if (s_l2cap_congestion_flag) {
        // Track congestion occurrences
        s_congestion_count++;
        if (s_congestion_count >= MAX_CONGESTION_COUNT) {
            s_severe_congestion = true;
            s_last_congestion_time = (uint32_t)(esp_timer_get_time() / 1000);
            ESP_LOGW(TAG, "Entering severe congestion state - backing off for %d ms", 
                    CONGESTION_RECOVERY_TIME_MS);
        }
        
        // When congestion detected, send silence
        memset(data, 0, len);
        return len;
    } else {
        // Reset congestion counter if no congestion
        s_congestion_count = 0;
    }

    // Rest of function remains the same
    if (s_beep_in_progress && s_beep_duration >= BEEP_DURATION_THRESHOLD) {
        ESP_LOGI(TAG, "Beep duration reached: %d samples, stopping beep", s_beep_duration);
        s_beep_in_progress = false;
        s_beep_duration = 0;
        memset(data, 0, len); // Output silence after beep
        return len;
    }

    int16_t *samples = (int16_t*)data;
    int num_samples = len / 2;

    if (s_beep_in_progress) {
        // Only log occasionally to reduce system overhead
        if (s_beep_duration % 1000 == 0) {
            ESP_LOGD(TAG, "Generating beep: duration=%d", s_beep_duration);
        }
        
        for (int i = 0; i < num_samples; i++) {
            samples[i] = sine_table[s_beep_index];
            s_beep_index = (s_beep_index + 1) % TABLE_SIZE;
            s_beep_duration++;
        }
    } else {
        memset(data, 0, len);
    }
    return len;
}

static SemaphoreHandle_t s_bt_resource_mutex = NULL;

esp_err_t bluetooth_start_discovery(void) {
    ESP_LOGI(TAG, "###Starting Bluetooth device discovery");
    if (xSemaphoreTake(s_bt_resource_mutex, portMAX_DELAY) == pdTRUE) {
        esp_err_t ret = esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 30, 0);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start discovery: %s", esp_err_to_name(ret));
        }
        xSemaphoreGive(s_bt_resource_mutex);
    }
    ESP_LOGI(TAG, "###Leaving bluetooth_start_discovery");
    return ESP_OK;
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
    
    if (xSemaphoreTake(s_bt_resource_mutex, portMAX_DELAY) == pdTRUE) {
        // First establish connection without security
        esp_err_t ret = esp_a2d_source_connect(bd_addr);  // Changed to source
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initiate connection: %s", esp_err_to_name(ret));
            xSemaphoreGive(s_bt_resource_mutex);
            return ret;
        }
        xSemaphoreGive(s_bt_resource_mutex);
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

    // Initialize controller with increased stack size
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    bt_cfg.controller_task_stack_size = 8192;  // Double the stack size (was 4096)
    bt_cfg.hci_uart_no = UART_NUM_1;  // Use UART1 instead of default to avoid conflicts
    
    // Increase the BT controller memory if available
    bt_cfg.bt_max_acl_conn = 1; // We only need one connection
    bt_cfg.bt_max_sync_conn = 0; // Not using SCO
    // Limit the number of advertising packets to save memory
    bt_cfg.normal_adv_size = 10;
    // Remove the line with normal_scan_size as it doesn't exist in the struct
    
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

    // Remove L2CAP configuration
    /*
    // Configure L2CAP channel parameters
    esp_bt_cfg_l2cap_capab_ex_data_t l2cap_cfg;
    memset(&l2cap_cfg, 0, sizeof(esp_bt_cfg_l2cap_capab_ex_data_t));
    l2cap_cfg.l2cap_tx_buf_size = L2CAP_TX_BUF_SIZE;
    l2cap_cfg.l2cap_mtu        = L2CAP_MTU;
    esp_bt_gap_set_l2cap_capability(&l2cap_cfg);
    */

    if (!s_bt_resource_mutex) {
        s_bt_resource_mutex = xSemaphoreCreateMutex();
        if (!s_bt_resource_mutex) {
            ESP_LOGE(TAG, "Failed to create Bluetooth resource mutex");
            return ESP_ERR_NO_MEM;
        }
    }

    // Initialize the volume from NVS
    nvs_handle_t nvs_handle;
    ret = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (ret == ESP_OK) {
        // Check if volume exists in NVS
        uint8_t vol;
        ret = nvs_get_u8(nvs_handle, BT_VOLUME_KEY, &vol);
        if (ret == ESP_ERR_NVS_NOT_FOUND) {
            // If not found, set default volume of 32
            nvs_set_u8(nvs_handle, BT_VOLUME_KEY, DEFAULT_VOLUME);
            nvs_commit(nvs_handle);
            s_current_volume = DEFAULT_VOLUME;
        } else if (ret == ESP_OK) {
            // Use the stored volume
            s_current_volume = vol;
        }
        nvs_close(nvs_handle);
    }

    // At the end of initialization, start our memory monitor task
    xTaskCreate(memory_monitor_task, "mem_monitor", 2048, NULL, 1, NULL);

    // Set default volume after initialization
    ret = bluetooth_set_volume(DEFAULT_VOLUME);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set default volume: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Default volume set to: %d", DEFAULT_VOLUME);
    }
    
    ESP_LOGI(TAG, "Bluetooth stack initialized successfully with enhanced congestion control");
    return ESP_OK;
}

// AVRCP controller callback
void avrc_ct_callback(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *param) {
    ESP_LOGD(TAG, "AVRC controller event: %d", event);
    switch (event) {
        case ESP_AVRC_CT_CONNECTION_STATE_EVT:
            ESP_LOGI(TAG, "AVRC controller connection state: %d", param->conn_stat.connected);
            if (param->conn_stat.connected) {
                // Just log connection, don't try to register for notifications here
                ESP_LOGI(TAG, "AVRCP controller connected");
            }
            break;
            
        case ESP_AVRC_CT_REMOTE_FEATURES_EVT:
            ESP_LOGI(TAG, "AVRC remote features: 0x%" PRIx32, param->rmt_feats.feat_mask);
            break;
            
        case ESP_AVRC_CT_PASSTHROUGH_RSP_EVT:
            ESP_LOGI(TAG, "Passthrough response received");
            break;
            
        case ESP_AVRC_CT_METADATA_RSP_EVT:
            ESP_LOGI(TAG, "Metadata response received");
            break;
            
        case ESP_AVRC_CT_SET_ABSOLUTE_VOLUME_RSP_EVT:
            ESP_LOGI(TAG, "Set absolute volume response");
            // Store volume value if available
            if (param->set_volume_rsp.volume <= 127) {
                s_current_volume = param->set_volume_rsp.volume;
                ESP_LOGI(TAG, "Volume set to %d", s_current_volume);
            }
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
        if (xSemaphoreTake(s_bt_resource_mutex, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(TAG, "Attempting to disconnect from device...");
            esp_err_t ret = esp_a2d_source_disconnect(pending_pair_addr);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to disconnect: %s", esp_err_to_name(ret));
                xSemaphoreGive(s_bt_resource_mutex);
                return ret;
            }
            xSemaphoreGive(s_bt_resource_mutex);
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
        if (xSemaphoreTake(s_bt_resource_mutex, portMAX_DELAY) == pdTRUE) {
            esp_err_t ret = esp_bt_gap_remove_bond_device(pending_pair_addr);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to unpair: %s", esp_err_to_name(ret));
                xSemaphoreGive(s_bt_resource_mutex);
                return ret;
            }
            xSemaphoreGive(s_bt_resource_mutex);
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
    
    if (xSemaphoreTake(s_bt_resource_mutex, portMAX_DELAY) == pdTRUE) {
        // Establish connection
        esp_err_t ret = esp_a2d_source_connect(bd_addr);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initiate connection: %s", esp_err_to_name(ret));
            xSemaphoreGive(s_bt_resource_mutex);
            return ret;
        }
        xSemaphoreGive(s_bt_resource_mutex);
    }

    // Set default volume after connecting
    esp_err_t ret = bluetooth_set_volume(DEFAULT_VOLUME);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set default volume after connecting: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Default volume set after connecting to: %d", DEFAULT_VOLUME);
    }

    return ESP_OK;
}

// Function to restart the Bluetooth stack
esp_err_t restart_bluetooth_stack(void) {
    esp_err_t ret;

    // Disable Bluedroid
    if (xSemaphoreTake(s_bt_resource_mutex, portMAX_DELAY) == pdTRUE) {
        ret = esp_bluedroid_disable();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to disable Bluedroid: %s", esp_err_to_name(ret));
            xSemaphoreGive(s_bt_resource_mutex);
            return ret;
        }

        // Deinitialize Bluedroid
        ret = esp_bluedroid_deinit();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to deinitialize Bluedroid: %s", esp_err_to_name(ret));
            xSemaphoreGive(s_bt_resource_mutex);
            return ret;
        }

        // Disable Bluetooth controller
        ret = esp_bt_controller_disable();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to disable Bluetooth controller: %s", esp_err_to_name(ret));
            xSemaphoreGive(s_bt_resource_mutex);
            return ret;
        }

        // Deinitialize Bluetooth controller
        ret = esp_bt_controller_deinit();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to deinitialize Bluetooth controller: %s", esp_err_to_name(ret));
            xSemaphoreGive(s_bt_resource_mutex);
            return ret;
        }

        // Reinitialize and enable Bluetooth stack
        ret = bluetooth_init();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to reinitialize Bluetooth stack: %s", esp_err_to_name(ret));
            xSemaphoreGive(s_bt_resource_mutex);
            return ret;
        }
        xSemaphoreGive(s_bt_resource_mutex);
    }

    ESP_LOGI(TAG, "Bluetooth stack restarted successfully");
    return ESP_OK;
}

// Function to set the Bluetooth device name
esp_err_t bluetooth_set_device_name(const char *name) {
    if (xSemaphoreTake(s_bt_resource_mutex, portMAX_DELAY) == pdTRUE) {
        esp_err_t ret = esp_bt_gap_set_device_name(name);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set device name: %s", esp_err_to_name(ret));
            xSemaphoreGive(s_bt_resource_mutex);
            return ret;
        }

        // Save the device name to NVS
        nvs_handle_t nvs_handle;
        ret = nvs_open("storage", NVS_READWRITE, &nvs_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to open NVS handle: %s", esp_err_to_name(ret));
            xSemaphoreGive(s_bt_resource_mutex);
            return ret;
        }

        ret = nvs_set_str(nvs_handle, BT_DEVICE_NAME_KEY, name);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to save device name to NVS: %s", esp_err_to_name(ret));
            nvs_close(nvs_handle);
            xSemaphoreGive(s_bt_resource_mutex);
            return ret;
        }

        nvs_close(nvs_handle);
        ESP_LOGI(TAG, "Device name set to: %s", name);

        xSemaphoreGive(s_bt_resource_mutex);
    }

    // Restart Bluetooth stack to apply the new name
    return restart_bluetooth_stack();
}

// Function to get the Bluetooth device name
esp_err_t bluetooth_get_device_name(char *name, size_t max_len) {
    // Read the device name from NVS
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS handle: %s", esp_err_to_name(ret));
        return ret;
    }

    size_t name_len = max_len;
    ret = nvs_get_str(nvs_handle, BT_DEVICE_NAME_KEY, name, &name_len);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        // If not found, use the default name and save it to NVS
        strncpy(name, DEFAULT_BT_DEVICE_NAME, max_len);
        name[max_len - 1] = '\0';  // Ensure null-termination
        ret = nvs_set_str(nvs_handle, BT_DEVICE_NAME_KEY, name);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to save default device name to NVS: %s", esp_err_to_name(ret));
        } else {
            ret = nvs_commit(nvs_handle);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to commit default device name to NVS: %s", esp_err_to_name(ret));
            }
        }
    } else if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read device name from NVS: %s", esp_err_to_name(ret));
    }

    nvs_close(nvs_handle);
    ESP_LOGI(TAG, "Device name: %s", name);
    return ret;
}

esp_err_t bluetooth_write_audio(const uint8_t* data, size_t* written) {  // Update this line to match the declaration
    if (!data || !written || !*written) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_media_state != APP_AV_MEDIA_STATE_STARTED) {
        ESP_LOGW(TAG, "A2DP media not started");
        return ESP_ERR_INVALID_STATE;
    }

    // Add logging to monitor buffer usage
    //esp_a2d_source_get_buffer_status(&available, &total);
    //SAFE_ESP_LOGD(TAG, "A2DP Buffer: Available=%d, Total=%d", available, total);

    return esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY);
}

// AVRCP volume control functions
esp_err_t bluetooth_volume_up(void) {
    ESP_LOGI(TAG, "Sending volume up command");
    
    // Ensure we're not sending commands too quickly
    if (!is_operation_time_ok()) {
        ESP_LOGW(TAG, "Volume up command rejected - too soon after previous operation");
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret = esp_avrc_ct_send_passthrough_cmd(
        0, // transaction label
        ESP_AVRC_PT_CMD_VOL_UP, // operation ID for volume up
        ESP_AVRC_PT_CMD_STATE_PRESSED);
        
    // Release the button after pressing
    vTaskDelay(30 / portTICK_PERIOD_MS); // Increased delay for better stability
    
    esp_avrc_ct_send_passthrough_cmd(
        0, 
        ESP_AVRC_PT_CMD_VOL_UP,
        ESP_AVRC_PT_CMD_STATE_RELEASED);
    
    // Update our local volume estimate (cap at 127)
    if (s_current_volume < 127) {
        s_current_volume += 5; // Increment by a reasonable step
        if (s_current_volume > 127) {
            s_current_volume = 127;
        }
    }
    
    return ret;
}

esp_err_t bluetooth_volume_down(void) {
    ESP_LOGI(TAG, "Sending volume down command");
    
    // Ensure we're not sending commands too quickly
    if (!is_operation_time_ok()) {
        ESP_LOGW(TAG, "Volume down command rejected - too soon after previous operation");
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret = esp_avrc_ct_send_passthrough_cmd(
        0, // transaction label
        ESP_AVRC_PT_CMD_VOL_DOWN, // operation ID for volume down
        ESP_AVRC_PT_CMD_STATE_PRESSED);
        
    // Release the button after pressing
    vTaskDelay(30 / portTICK_PERIOD_MS); // Increased delay for better stability
    
    esp_avrc_ct_send_passthrough_cmd(
        0,
        ESP_AVRC_PT_CMD_VOL_DOWN,
        ESP_AVRC_PT_CMD_STATE_RELEASED);
    
    // Update our local volume estimate
    if (s_current_volume >= 5) {
        s_current_volume -= 5; // Decrement by a reasonable step
    } else {
        s_current_volume = 0; // Set to minimum if we would underflow
    }
    
    return ret;
}

esp_err_t bluetooth_set_volume(uint8_t volume) {
    ESP_LOGI(TAG, "Setting volume to: %d", volume);
    
    // Ensure we're not sending commands too quickly
    if (!is_operation_time_ok()) {
        ESP_LOGW(TAG, "Set volume command rejected - too soon after previous operation");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Ensure volume is in valid range (0-127 for AVRCP)
    if (volume > 127) {
        volume = 127;
    }
    
    // Store locally first
    s_current_volume = volume;
    
    // Save to NVS for persistence across reboots
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        nvs_set_u8(nvs_handle, BT_VOLUME_KEY, volume);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
    }
    
    // Set volume initialized flag
    s_volume_initialized = true;
    
    // Send command to device
    return esp_avrc_ct_send_set_absolute_volume_cmd(0, volume);
}

// Function to get the current volume level - doesn't need to send any commands
esp_err_t bluetooth_get_volume(void) {
    ESP_LOGI(TAG, "Current volume is %d", s_current_volume);
    return ESP_OK;
}

// Function to get the stored volume level
uint8_t bluetooth_get_current_volume(void) {
    return s_current_volume;
}

// Make sure this function is present in the source file with proper implementation
esp_err_t bluetooth_send_beep(void) {
    ESP_LOGI(TAG, "Sending beep");
    if (s_a2d_state != APP_AV_STATE_CONNECTED) {
        ESP_LOGW(TAG, "Not connected: cannot send beep");
        return ESP_ERR_INVALID_STATE;
    }
    trigger_beep();
    return ESP_OK;
}

// Create a new timer task to periodically check for memory issues
#define MEMORY_CHECK_INTERVAL_MS 5000
static void memory_monitor_task(void *arg) {
    while(1) {
        // Get free heap memory
        size_t free_heap = esp_get_free_heap_size();
        ESP_LOGI(TAG, "Free heap: %u bytes", free_heap);
        
        // If memory is critically low, force congestion mode
        if (free_heap < 20000) { // 20KB is a critical threshold
            s_severe_congestion = true;
            s_last_congestion_time = (uint32_t)(esp_timer_get_time() / 1000);
            ESP_LOGW(TAG, "Memory critically low (%u bytes). Enforcing congestion control.", 
                   free_heap);
        }
        
        vTaskDelay(pdMS_TO_TICKS(MEMORY_CHECK_INTERVAL_MS));
    }
}