/**
 * @file bt_app_audio.c
 * @brief Bluetooth audio management including buffer allocation and beep generation
 * 
 * This module handles audio buffer management for Bluetooth streaming, providing
 * a pre-allocated buffer pool to prevent memory fragmentation during audio operations.
 * It also implements beep generation via sine wave synthesis and monitors buffer usage.
 */
#include "bluetooth/bt_app_audio.h"
#include "bluetooth/bt_app_global.h"
#include "custom_log.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include <math.h>

#define TAG "BT_APP_AUDIO"

// Increase buffer count and add better management
#define AUDIO_BUFFER_COUNT 32   // Increase from 16 to 32 
#define BT_AUDIO_MONITOR_INTERVAL_MS 5000 // Rename to avoid conflict

/**
 * Buffer pool management system for audio data
 * Pre-allocating buffers helps prevent heap fragmentation during audio streaming
 */
static uint8_t s_audio_buffers[AUDIO_BUFFER_COUNT][AUDIO_BUFFER_SIZE];
static bool s_buffer_in_use[AUDIO_BUFFER_COUNT] = {false};
static SemaphoreHandle_t s_buffer_mutex = NULL;

/**
 * Buffer usage statistics to track allocation efficiency and detect potential leaks
 */
static volatile int s_buffer_allocations = 0;
static volatile int s_buffer_releases = 0;
static volatile int s_buffer_allocation_failures = 0;

// Forward declarations
static void buffer_monitor_task(void *arg);

/**
 * @brief Generates a beep notification sound
 * 
 * Initializes sine wave generation and starts audio streaming to produce
 * an audible notification beep over the connected Bluetooth device.
 *
 * @return ESP_OK if beep was triggered successfully, error code otherwise
 */
esp_err_t trigger_beep(void) {
    // Reset beep state
    s_beep_index = 0;
    s_beep_duration = 0;
    s_beep_in_progress = true;
    
    // Ensure sine table is initialized
    if (!sine_table_initialized) {
        SAFE_ESP_LOGI(TAG, "Initializing sine table");
        for (int i = 0; i < TABLE_SIZE; i++) {
            sine_table[i] = (int16_t)(8000.0f * sinf(2.0f * M_PI * i / TABLE_SIZE));
        }
        sine_table_initialized = true;
    }
    
    SAFE_ESP_LOGI(TAG, "Beep triggered! beep_in_progress=%d", s_beep_in_progress);
    
    // Start media streaming
    if (s_a2d_state == APP_AV_STATE_CONNECTED) {
        SAFE_ESP_LOGI(TAG, "Starting media for beep");
        esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START);
        return ESP_OK;
    } else {
        SAFE_ESP_LOGW(TAG, "Cannot beep: not connected");
        s_beep_in_progress = false;
        return ESP_ERR_INVALID_STATE;
    }
}

/**
 * @brief Checks if enough time has passed since last Bluetooth operation
 * 
 * Prevents operations from occurring too rapidly, which can cause instability
 * in the Bluetooth stack.
 *
 * @return true if enough time has passed, false otherwise
 */
bool is_operation_time_ok(void) {
    uint32_t current_time = (uint32_t)(esp_timer_get_time() / 1000);
    if (current_time - s_last_operation_time < BT_OPERATION_DELAY_MS) {
        ESP_LOGW(TAG, "Operation attempted too soon after previous one");
        return false;
    }
    s_last_operation_time = current_time;
    return true;
}

/**
 * @brief Write audio data to the Bluetooth audio stream
 * 
 * Attempts to write audio data to the connected Bluetooth device.
 * Checks if media streaming is active before attempting to write.
 *
 * @param data Pointer to audio data to be written
 * @param written Pointer to size_t containing the number of bytes to write
 *                and will be updated with actual bytes written
 * @return ESP_OK if successful or appropriate error code
 */
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

/**
 * @brief Public function to send a notification beep
 * 
 * This is the API function exposed to other modules that want
 * to generate a notification beep sound.
 *
 * @return ESP_OK if beep was triggered successfully, error code otherwise
 */
esp_err_t bluetooth_send_beep(void) {
    SAFE_ESP_LOGI(TAG, "Sending beep");
    
    return trigger_beep();
}

/**
 * @brief Logs audio buffer usage statistics
 * 
 * Outputs information about buffer allocations, releases, and
 * usage to help diagnose memory issues or leaks.
 */
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

/**
 * @brief Initialize the audio buffer system and sine wave table
 * 
 * Sets up the pre-allocated audio buffer pool, initializes the sine wave
 * lookup table for generating tones, and starts the buffer monitoring task.
 */
void bt_app_audio_init(void) {
    // Initialize sine table at startup
    if (!sine_table_initialized) {
        SAFE_ESP_LOGI(TAG, "Initializing sine table with %d samples", TABLE_SIZE);
        for (int i = 0; i < TABLE_SIZE; i++) {
            sine_table[i] = (int16_t)(8000.0f * sinf(2.0f * M_PI * i / TABLE_SIZE));
        }
        sine_table_initialized = true;
        SAFE_ESP_LOGI(TAG, "Sine table initialized successfully");
    }
    
    // Initialize the buffer management system
    s_buffer_mutex = xSemaphoreCreateMutex();
    if (s_buffer_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create audio buffer mutex");
        return;
    }
    
    // Start a monitoring task to track buffer usage
    xTaskCreate(buffer_monitor_task, "buf_monitor", 2048, NULL, 1, NULL);
    
    ESP_LOGI(TAG, "Audio buffer system initialized with %d buffers of %d bytes each",
             AUDIO_BUFFER_COUNT, AUDIO_BUFFER_SIZE);
    SAFE_ESP_LOGI(TAG, "bt_app_audio_init: Initializing audio");
}

/**
 * @brief Task that periodically monitors and reports buffer usage
 * 
 * This background task runs at low priority and logs buffer statistics
 * at regular intervals to help diagnose memory issues.
 *
 * @param arg Unused task parameter
 */
static void buffer_monitor_task(void *arg) {
    while (1) {
        bt_app_audio_dump_stats();
        vTaskDelay(pdMS_TO_TICKS(BT_AUDIO_MONITOR_INTERVAL_MS));
    }
}

/**
 * @brief Get a pre-allocated audio buffer from the pool
 * 
 * Thread-safe function to obtain an available audio buffer.
 * Tracks allocations and handles congestion when buffers run low.
 *
 * @return Pointer to available buffer or NULL if none available
 */
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
        
        // No buffers available - track failure and signal congestion
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

/**
 * @brief Return a buffer to the pool when no longer needed
 * 
 * Thread-safe function to release a previously allocated buffer.
 * Updates usage statistics and helps clear congestion state.
 *
 * @param buffer Pointer to the buffer being released
 */
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

/**
 * @brief Get the number of available audio buffers
 * 
 * Counts and returns the number of free buffers in the pool.
 * Useful for checking resource availability before operations.
 *
 * @return Number of available buffers, 0 if buffer system not initialized
 */
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

/**
 * @brief Event callback for audio-related events
 * 
 * Currently only logs the event, but could be expanded to handle
 * various audio events.
 *
 * @param event Event identifier
 * @param param Event parameters
 */
void bt_app_audio_callback(uint16_t event, void *param) {
    SAFE_ESP_LOGI(TAG, "bt_app_audio_callback: event=%d", event);
}

/**
 * @brief Clean up audio resources when shutting down
 * 
 * Performs any necessary cleanup when the audio system is being shut down.
 * Currently just logs the action.
 */
void bt_app_audio_deinit(void) {
    SAFE_ESP_LOGI(TAG, "bt_app_audio_deinit: Deinitializing audio");
}
