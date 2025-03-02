#include "bluetooth/bt_app_a2dp.h"      
#include "bluetooth/bt_app_global.h"
#include "bluetooth/bt_app_audio.h"
#include "bluetooth/bt_app_av.h"
#include "custom_log.h"

#define TAG "BT_APP_A2DP"

// Define the A2DP callback function
// New task to handle beep processing on the other CPU with logging.
static void beep_task(void *params) {
    SAFE_ESP_LOGI(TAG, "beep_task started on core %d", xPortGetCoreID());
    trigger_beep();
    vTaskDelay(pdMS_TO_TICKS(1000));
    SAFE_ESP_LOGI(TAG, "beep_task completed, deleting task");
    vTaskDelete(NULL);
}

void bt_app_a2d_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param) {
    SAFE_ESP_LOGI(TAG, "A2DP callback event: %d", event);
    
    switch (event) {
        case ESP_A2D_CONNECTION_STATE_EVT:
            SAFE_ESP_LOGI(TAG, "A2DP connection state: %d", param->conn_stat.state);
            if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
                SAFE_ESP_LOGI(TAG, "A2DP connected");
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
                        SAFE_ESP_LOGI(TAG, "Setting initial volume to %d", vol);
                        bluetooth_set_volume(vol);
                        
                        s_volume_initialized = true;
                    }
                }
                
                // Start media after connection
                SAFE_ESP_LOGI(TAG, "Requesting media playback...");
                esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY);
            } else if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
                SAFE_ESP_LOGI(TAG, "A2DP disconnected");
                s_a2d_state = APP_AV_STATE_UNCONNECTED;
                s_media_state = APP_AV_MEDIA_STATE_IDLE;
            }
            break;
            
        case ESP_A2D_MEDIA_CTRL_ACK_EVT:
            SAFE_ESP_LOGI(TAG, "A2DP media control ACK: cmd=%d, status=%d", param->media_ctrl_stat.cmd, param->media_ctrl_stat.status);
            if (param->media_ctrl_stat.cmd == ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY) {
                if (param->media_ctrl_stat.status == ESP_A2D_MEDIA_CTRL_ACK_SUCCESS) {
                    SAFE_ESP_LOGI(TAG, "Media source ready, starting playback...");
                    esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START);
                } else {
                    SAFE_ESP_LOGW(TAG, "Media source not ready, ACK status: %d", param->media_ctrl_stat.status);
                }
            } else if (param->media_ctrl_stat.status != ESP_A2D_MEDIA_CTRL_ACK_SUCCESS) {
                // If we get any error in media control, consider it a congestion
                s_l2cap_congestion_flag = true;
                s_last_congestion_time = (uint32_t)(esp_timer_get_time() / 1000);
                SAFE_ESP_LOGW(TAG, "A2DP media control failed, possible congestion. Status: %d", 
                        param->media_ctrl_stat.status);
            } else {
                // Command completed successfully, clear any congestion flag
                s_l2cap_congestion_flag = false;
            }
            break;
            
        case ESP_A2D_AUDIO_STATE_EVT:
            SAFE_ESP_LOGI(TAG, "A2D audio state: %d", param->audio_stat.state);
            if (param->audio_stat.state == ESP_A2D_AUDIO_STATE_STARTED) {
                s_media_state = APP_AV_MEDIA_STATE_STARTED;
                SAFE_ESP_LOGI(TAG, "A2DP audio started");
                s_l2cap_congestion_flag = false; // Reset congestion flag when audio starts
            } else if (param->audio_stat.state == ESP_A2D_AUDIO_STATE_STOPPED) {
                s_media_state = APP_AV_MEDIA_STATE_IDLE;
                SAFE_ESP_LOGI(TAG, "A2DP audio stopped");
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
            SAFE_ESP_LOGI(TAG, "Unhandled A2DP event: %d", event);
            break;
    }
}

static int16_t sine_table[TABLE_SIZE];

// Update a2dp_source_data_cb to work with the simplified congestion detection
int32_t a2dp_source_data_cb(uint8_t *data, int32_t len) {
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
            SAFE_ESP_LOGI(TAG, "Exiting severe congestion state after recovery time");
        }
    }
    
    if (s_l2cap_congestion_flag) {
        // Track congestion occurrences
        s_congestion_count++;
        if (s_congestion_count >= MAX_CONGESTION_COUNT) {
            s_severe_congestion = true;
            s_last_congestion_time = (uint32_t)(esp_timer_get_time() / 1000);
            SAFE_ESP_LOGW(TAG, "Entering severe congestion state - backing off for %d ms", 
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
        SAFE_ESP_LOGI(TAG, "Beep duration reached: %d samples, stopping beep", s_beep_duration);
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
            SAFE_ESP_LOGD(TAG, "Generating beep: duration=%d", s_beep_duration);
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

// Changed from static to non-static since it's now exposed in the header
esp_err_t init_a2dp(void) {
    SAFE_ESP_LOGI(TAG, "###Initializing A2DP source - 1");
    
    // A2DP is already initialized in bluetooth_init()
    // Just verify the state
    if (s_a2d_state == APP_AV_STATE_IDLE) {
        s_a2d_state = APP_AV_STATE_DISCOVERING;
        SAFE_ESP_LOGI(TAG, "A2DP source initialization complete");
        return ESP_OK;
    } else {
        SAFE_ESP_LOGE(TAG, "A2DP in invalid state: %d", s_a2d_state);
        return ESP_ERR_INVALID_STATE;
    }
}

// AVRCP controller callback
void avrc_ct_callback(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *param) {
    SAFE_ESP_LOGD(TAG, "AVRC controller event: %d", event);
    switch (event) {
        case ESP_AVRC_CT_CONNECTION_STATE_EVT:
            SAFE_ESP_LOGI(TAG, "AVRC controller connection state: %d", param->conn_stat.connected);
            if (param->conn_stat.connected) {
                // Just log connection, don't try to register for notifications here
                SAFE_ESP_LOGI(TAG, "AVRCP controller connected");
            }
            break;
            
        case ESP_AVRC_CT_REMOTE_FEATURES_EVT:
            SAFE_ESP_LOGI(TAG, "AVRC remote features: 0x%" PRIx32, param->rmt_feats.feat_mask);
            break;
            
        case ESP_AVRC_CT_PASSTHROUGH_RSP_EVT:
            SAFE_ESP_LOGI(TAG, "Passthrough response received");
            break;
            
        case ESP_AVRC_CT_METADATA_RSP_EVT:
            SAFE_ESP_LOGI(TAG, "Metadata response received");
            break;
            
        case ESP_AVRC_CT_SET_ABSOLUTE_VOLUME_RSP_EVT:
            SAFE_ESP_LOGI(TAG, "Set absolute volume response");
            // Store volume value if available
            if (param->set_volume_rsp.volume <= 127) {
                s_current_volume = param->set_volume_rsp.volume;
                SAFE_ESP_LOGI(TAG, "Volume set to %d", s_current_volume);
            }
            break;
            
        default:
            SAFE_ESP_LOGW(TAG, "Unhandled AVRC controller event: %d", event);
            break;
    }
}