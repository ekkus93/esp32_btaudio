#include "bluetooth/bt_app_audio.h"
#include "bluetooth/bt_app_global.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

#define TAG "BT_APP_AUDIO"

// Increase buffer count and add better management
#define AUDIO_BUFFER_SIZE 4112
#define AUDIO_BUFFER_COUNT 16   // Increase from 8 to 16 
#define BT_AUDIO_MONITOR_INTERVAL_MS 5000 // Rename to avoid conflict

// Pre-allocated audio buffers
static uint8_t s_audio_buffers[AUDIO_BUFFER_COUNT][AUDIO_BUFFER_SIZE];
static bool s_buffer_in_use[AUDIO_BUFFER_COUNT] = {false};
static SemaphoreHandle_t s_buffer_mutex = NULL;

// Track buffer statistics
static volatile int s_buffer_allocations = 0;
static volatile int s_buffer_releases = 0;
static volatile int s_buffer_allocation_failures = 0;

// Add this declaration with other static variables
static int16_t sine_table[TABLE_SIZE]; // Add this line for the sine wave lookup table

// Forward declarations
static void buffer_monitor_task(void *arg);

// Initialize the sine table for beep generation
static void init_sine_table(void) {
    for (int i = 0; i < TABLE_SIZE; i++) {
        float angle = 2.0f * M_PI * i / TABLE_SIZE;
        sine_table[i] = (int16_t)(32767.5f * sinf(angle));
    }
    sine_table_initialized = true;
}

// Trigger a beep sound
void trigger_beep(void) {
    s_beep_in_progress = true;
    s_beep_duration = 0;
    s_beep_index = 0;
    if (!sine_table_initialized) {
        init_sine_table();
    }
    ESP_LOGI(TAG, "Beep triggered (trigger_beep)");
}

// Check if enough time has passed since last operation
bool is_operation_time_ok(void) {
    uint32_t current_time = (uint32_t)(esp_timer_get_time() / 1000);
    if (current_time - s_last_operation_time < BT_OPERATION_DELAY_MS) {
        ESP_LOGW(TAG, "Operation attempted too soon after previous one");
        return false;
    }
    s_last_operation_time = current_time;
    return true;
}

// Write audio data
esp_err_t bluetooth_write_audio(const uint8_t* data, size_t* written) {
    if (!data || !written || !*written) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_media_state != APP_AV_MEDIA_STATE_STARTED) {
        ESP_LOGW(TAG, "A2DP media not started");
        return ESP_ERR_INVALID_STATE;
    }

    return esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY);
}

// Update bluetooth_send_beep function to actively start audio streaming
esp_err_t bluetooth_send_beep(void) {
    ESP_LOGI(TAG, "Sending beep");
    
    if (s_a2d_state != APP_AV_STATE_CONNECTED) {
        ESP_LOGW(TAG, "Not connected: cannot send beep");
        return ESP_ERR_INVALID_STATE;
    }
    
    // First make sure audio is started
    if (s_media_state != APP_AV_MEDIA_STATE_STARTED) {
        ESP_LOGI(TAG, "Starting media for beep");
        esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START);
        
        // Wait for media to start
        int timeout = 10;  // 1 second max wait
        while (s_media_state != APP_AV_MEDIA_STATE_STARTED && timeout > 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
            timeout--;
        }
        
        if (s_media_state != APP_AV_MEDIA_STATE_STARTED) {
            ESP_LOGW(TAG, "Failed to start media for beep");
            return ESP_ERR_TIMEOUT;
        }
    }
    
    // Now trigger the beep
    trigger_beep();
    
    // Make sure beep is heard by keeping audio stream active
    ESP_LOGI(TAG, "Beep triggered, active for ~1 second");
    
    return ESP_OK;
}

// Add this function to dump buffer statistics periodically
void bt_app_audio_dump_stats(void) {
    if (s_buffer_mutex == NULL) return;
    
    if (xSemaphoreTake(s_buffer_mutex, 10 / portTICK_PERIOD_MS) == pdTRUE) {
        int free_count = 0;
        for (int i = 0; i < AUDIO_BUFFER_COUNT; i++) {
            if (!s_buffer_in_use[i]) free_count++;
        }
        
        ESP_LOGI(TAG, "Buffer stats: free=%d/%d, allocs=%d, releases=%d, fails=%d", 
                free_count, AUDIO_BUFFER_COUNT, s_buffer_allocations, 
                s_buffer_releases, s_buffer_allocation_failures);
        xSemaphoreGive(s_buffer_mutex);
    }
}

// Initialize the audio buffer system
void bt_app_audio_init(void) {
    s_buffer_mutex = xSemaphoreCreateMutex();
    if (s_buffer_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create audio buffer mutex");
        return;
    }
    
    // Start a monitoring task
    xTaskCreate(buffer_monitor_task, "buf_monitor", 2048, NULL, 1, NULL);
    
    ESP_LOGI(TAG, "Audio buffer system initialized with %d buffers of %d bytes each",
             AUDIO_BUFFER_COUNT, AUDIO_BUFFER_SIZE);
}

// Monitoring task for buffer usage
static void buffer_monitor_task(void *arg) {
    while (1) {
        bt_app_audio_dump_stats();
        vTaskDelay(pdMS_TO_TICKS(BT_AUDIO_MONITOR_INTERVAL_MS));
    }
}

// Get a pre-allocated buffer
uint8_t* bt_app_audio_get_buffer(void) {
    if (s_buffer_mutex == NULL) {
        bt_app_audio_init();
    }
    
    if (xSemaphoreTake(s_buffer_mutex, portMAX_DELAY) == pdTRUE) {
        for (int i = 0; i < AUDIO_BUFFER_COUNT; i++) {
            if (!s_buffer_in_use[i]) {
                s_buffer_in_use[i] = true;
                s_buffer_allocations++;
                xSemaphoreGive(s_buffer_mutex);
                ESP_LOGD(TAG, "Allocated audio buffer %d", i);
                return s_audio_buffers[i];
            }
        }
        
        // No buffers available
        s_buffer_allocation_failures++;
        xSemaphoreGive(s_buffer_mutex);
        
        // Signal congestion
        s_l2cap_congestion_flag = true;
        s_congestion_count++;
        s_last_congestion_time = (uint32_t)(esp_timer_get_time() / 1000);
        
        if (s_congestion_count >= MAX_CONGESTION_COUNT) {
            s_severe_congestion = true;
            ESP_LOGW(TAG, "Severe congestion: %d buffer allocation failures",
                   s_buffer_allocation_failures);
        }
    }
    
    return NULL;
}

// Release a pre-allocated buffer
void bt_app_audio_release_buffer(uint8_t* buffer) {
    if (buffer == NULL || s_buffer_mutex == NULL) {
        return;
    }
    
    if (xSemaphoreTake(s_buffer_mutex, portMAX_DELAY) == pdTRUE) {
        for (int i = 0; i < AUDIO_BUFFER_COUNT; i++) {
            if (buffer == s_audio_buffers[i]) {
                if (s_buffer_in_use[i]) {
                    s_buffer_in_use[i] = false;
                    s_buffer_releases++;
                    ESP_LOGD(TAG, "Released audio buffer %d", i);
                    
                    // If we were severely congested and now recovered
                    if (s_buffer_allocations - s_buffer_releases < AUDIO_BUFFER_COUNT/2) {
                        s_congestion_count = 0;
                    }
                }
                break;
            }
        }
        xSemaphoreGive(s_buffer_mutex);
    }
}

// Check audio buffer availability
int bt_app_audio_available_buffers(void) {
    if (s_buffer_mutex == NULL) {
        return 0;
    }
    
    int count = 0;
    if (xSemaphoreTake(s_buffer_mutex, 0) == pdTRUE) {
        for (int i = 0; i < AUDIO_BUFFER_COUNT; i++) {
            if (!s_buffer_in_use[i]) {
                count++;
            }
        }
        xSemaphoreGive(s_buffer_mutex);
    }
    
    return count;
}
