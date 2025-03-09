#include "bluetooth/bt_app_a2dp.h"      
#include "bluetooth/bt_app_global.h"
#include "bluetooth/bt_app_audio.h"
#include "bluetooth/bt_app_av.h"
#include "custom_log.h"
#include "bluetooth/bt_app_conn.h"  // Add this include to get bluetooth_pair_device

#define TAG "BT_APP_A2DP"

// Define the A2DP callback function
// New task to handle beep processing on the other CPU with logging.
static void beep_task(void *params) {
    SAFE_ESP_LOGI(TAG, "beep_task started on core %d", xPortGetCoreID());
    
    // First initialize the sine table
    if (!sine_table_initialized) {
        for (int i = 0; i < TABLE_SIZE; i++) {
            sine_table[i] = (int16_t)(8000.0f * sinf(2.0f * M_PI * i / TABLE_SIZE));
        }
        sine_table_initialized = true;
        SAFE_ESP_LOGI(TAG, "Sine table initialized with %d samples", TABLE_SIZE);
    }
    
    // Reset beep duration and flag
    s_beep_duration = 0;
    s_beep_index = 0;
    s_beep_in_progress = true;
    
    // Start media streaming to play the beep
    if (s_a2d_state == APP_AV_STATE_CONNECTED) {
        SAFE_ESP_LOGI(TAG, "Starting media for beep");
        esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START);
    }
    
    // Wait for beep to complete - leave time for the audio to play
    vTaskDelay(pdMS_TO_TICKS(1500));
    
    // Stop media streaming after beep completes
    SAFE_ESP_LOGI(TAG, "Beep completed, stopping audio stream");
    esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_STOP);
    
    SAFE_ESP_LOGI(TAG, "beep_task completed, deleting task");
    vTaskDelete(NULL);
}

void bt_app_a2d_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param) {
    SAFE_ESP_LOGI(TAG, "A2DP callback event: %d", event);
    
    switch (event) {
        case ESP_A2D_CONNECTION_STATE_EVT:
            SAFE_ESP_LOGI(TAG, "A2DP connection state: %d", param->conn_stat.state);
            
            // Add error handling for failed connection attempts
            if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
                // Check if this was a connection attempt that failed
                if (s_a2d_state == APP_AV_STATE_CONNECTING) {
                    SAFE_ESP_LOGW(TAG, "Connection attempt failed - device may be off or out of range");
                }
                
                // Safely change state and handle cleanup
                s_a2d_state = APP_AV_STATE_UNCONNECTED;
                esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
                
                // Signal any waiting tasks
                if (s_waiting_task != NULL) {
                    s_operation_complete = true;
                    xTaskNotifyGive(s_waiting_task);
                }
                
                SAFE_ESP_LOGI(TAG, "A2DP disconnected");
                s_a2d_state = APP_AV_STATE_UNCONNECTED;
                
                // Add automatic retry if pairing was in progress but failed
                if (s_pairing_in_progress && s_pairing_attempt < MAX_PAIRING_ATTEMPTS) {
                    s_pairing_in_progress = false;
                    SAFE_ESP_LOGI(TAG, "Pairing failed, will retry in 3 seconds...");
                    
                    // Create a delayed task to retry
                    esp_bd_addr_t retry_addr;
                    memcpy(retry_addr, s_last_pairing_attempt, ESP_BD_ADDR_LEN);
                    
                    // Use a timer or task to retry
                    vTaskDelay(pdMS_TO_TICKS(3000));
                    SAFE_ESP_LOGI(TAG, "Retrying pairing now...");
                    
                    // Convert BD_ADDR to string for bluetooth_pair_device
                    char mac_str[18];
                    sprintf(mac_str, "%02x:%02x:%02x:%02x:%02x:%02x",
                            retry_addr[0], retry_addr[1], retry_addr[2],
                            retry_addr[3], retry_addr[4], retry_addr[5]);
                            
                    bluetooth_pair_device(mac_str, pin_required);
                } else {
                    s_pairing_in_progress = false;
                }
                
                s_media_state = APP_AV_MEDIA_STATE_IDLE;

                // Signal the waiting task if there is one
                if (s_waiting_task != NULL) {
                    s_operation_complete = true;
                    xTaskNotifyGive(s_waiting_task);
                }

                // In the A2DP callback when connection fails
                if (s_pairing_attempt >= MAX_PAIRING_ATTEMPTS) {
                    // Remove device-specific check and message
                    SAFE_ESP_LOGE(TAG, "------------------------------------------------------");
                    SAFE_ESP_LOGE(TAG, "Pairing failed after %d attempts.", MAX_PAIRING_ATTEMPTS);
                    SAFE_ESP_LOGE(TAG, "Please ensure the audio device is in pairing mode:");
                    SAFE_ESP_LOGE(TAG, "1. Check your device's pairing instructions");
                    SAFE_ESP_LOGE(TAG, "2. Make sure the device is charged and turned on");
                    SAFE_ESP_LOGE(TAG, "3. Ensure the device is within range");
                    SAFE_ESP_LOGE(TAG, "4. Try resetting the device if needed");
                    SAFE_ESP_LOGE(TAG, "------------------------------------------------------");
                }
            } else if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
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
                        
                        // If the stored volume is higher than desired default, force it lower.
                        if (vol > DEFAULT_VOLUME) {
                            vol = DEFAULT_VOLUME;
                        }
                        
                        s_current_volume = vol; // Set current volume immediately
                        
                        // Wait a bit before sending actual command to ensure AVRCP is ready
                        vTaskDelay(pdMS_TO_TICKS(500));
                        SAFE_ESP_LOGI(TAG, "Setting initial volume to %d", vol);
                        bluetooth_set_volume(vol);
                        
                        s_volume_initialized = true;
                    }
                }
                
                // Play the notification beep when connected
                xTaskCreatePinnedToCore(beep_task, "beep_task", 4096, NULL, 3, NULL, 1);
                
                // Don't automatically start media streaming
                // Remove or comment out automatic start code:
                // esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START);
                
                SAFE_ESP_LOGI(TAG, "Connected, beep triggered");
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

// Replace with a simpler version that's closer to the original:
int32_t a2dp_source_data_cb(uint8_t *data, int32_t len) {
    static bool first_call = true;
    
    // Log first call details
    if (first_call) {
        SAFE_ESP_LOGI(TAG, "First data callback - len: %" PRId32 ", beep_in_progress: %d, sine_table_initialized: %d", 
                len, (int)s_beep_in_progress, (int)sine_table_initialized);
        first_call = false;
        
        // Ensure sine table is initialized on first callback
        if (!sine_table_initialized) {
            SAFE_ESP_LOGI(TAG, "Initializing sine table in data callback");
            for (int i = 0; i < TABLE_SIZE; i++) {
                sine_table[i] = (int16_t)(8000.0f * sinf(2.0f * M_PI * i / TABLE_SIZE));
            }
            sine_table_initialized = true;
        }
    }
    
    // Simple approach - if beep is active, generate sine wave
    if (s_beep_in_progress) {
        static int debug_counter = 0;
        if (++debug_counter % 30 == 0) {
            SAFE_ESP_LOGI(TAG, "Generating beep data: %" PRId32 " bytes, index: %d", len, s_beep_index);
        }
        
        int16_t *samples = (int16_t*)data;
        int num_samples = len / 4; // Stereo, 16-bit samples
        
        for (int i = 0; i < num_samples; i++) {
            // Calculate simple sine wave
            int index = s_beep_index % TABLE_SIZE;
            int16_t sample = sine_table[index];
            
            // Write to both channels
            samples[i*2] = sample;      // Left channel
            samples[i*2+1] = sample;    // Right channel
            
            s_beep_index++;
            s_beep_duration++;
            
            // Stop after certain duration
            if (s_beep_duration >= BEEP_DURATION_THRESHOLD) {
                SAFE_ESP_LOGI(TAG, "Beep complete after %d samples", s_beep_duration);
                s_beep_in_progress = false;
                // Fill rest with zeros
                memset(&samples[i*2+2], 0, (num_samples-i-1) * 4);
                break;
            }
        }
    } else {
        // No beep, fill with silence
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

void start_audio_stream(void) {
    if (s_a2d_state == APP_AV_STATE_CONNECTED && 
        s_media_state != APP_AV_MEDIA_STATE_STARTED) {
        
        SAFE_ESP_LOGI(TAG, "Starting audio stream");
        esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START);
    }
}