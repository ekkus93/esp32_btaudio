/**
 * @file radio.c
 * @brief Implementation of MP3 audio streaming and playback capabilities
 * 
 * This module provides functionality to stream MP3 audio from URLs or play MP3 files
 * stored in SPIFFS. It handles audio decoding, Bluetooth A2DP audio output, and 
 * resource management for audio tasks. The implementation uses ESP-ADF (Audio Development
 * Framework) components for the audio pipeline.
 */
#include "radio.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_spiffs.h"  
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bluetooth/bt_app_audio.h"
#include "spiffs_utils.h"  
#include <string.h>
#include <inttypes.h>  // Added to get PRIu32
#undef PRIu32
#define PRIu32 "u"    // Override to force unsigned printing
#include "esp_task_wdt.h" 
#include "custom_log.h"  
#include <audio_pipeline.h>
#include <esp_peripherals.h>
#include "freertos/semphr.h"
#include <mp3_decoder.h>
#include <i2s_stream.h>
#include <bluetooth_service.h>

// Remove direct include of periph_bt.h and instead conditionally include or define stubs:
#if __has_include("periph_bt.h")
#include "periph_bt.h"
#else
/**
 * Minimal stub definition for periph_bt_cfg_t and periph_bt_init() when
 * the periph_bt.h header is not available. Allows compilation to proceed
 * without the actual Bluetooth peripheral implementation.
 */
typedef struct {
    const char *device_name;
    int mode;
} periph_bt_cfg_t;
#define BLUETOOTH_A2DP_SINK 1
static inline void *periph_bt_init(const periph_bt_cfg_t *cfg) { (void)cfg; return NULL; }
#endif

// NEW: Add missing prototype for audio_pipeline_wait_for_event
extern esp_err_t audio_pipeline_wait_for_event(audio_pipeline_handle_t pipeline,
                                               audio_event_iface_msg_t *msg,
                                               TickType_t timeout);

// NEW: Declare external mutex helper functions and owner variable.
// Ensure that in main/esp32_btaudio4/esp32_btaudio/main/esp32_btaudio.c,
// s_bt_mutex_owner is not declared as static.
extern bool take_bt_resource_mutex(TickType_t timeout);
extern void give_bt_resource_mutex(void);
extern TaskHandle_t s_bt_mutex_owner;

#define TAG "RADIO"
#define BUFFER_SIZE 256       // Further reduced buffer size
#define PCM_RING_BUFFER_SIZE (2 * 1024)  // Further reduced buffer size
#define MP3_BUFFER_SIZE (2 * 1024)       // Further reduced buffer size

/**
 * Module-level state variables to track radio operations and resource usage
 */
static bool s_radio_active = false;          // Indicates if radio module is currently active
static bool s_radio_streaming_active = false; // Indicates if streaming is in progress
static uint8_t s_mp3_buffer[MP3_BUFFER_SIZE] __attribute__((unused, aligned(4))); // Buffer for MP3 data
static size_t s_mp3_head __attribute__((unused)) = 0, s_mp3_tail __attribute__((unused)) = 0; // Buffer pointers

static TaskHandle_t s_radio_task_handle = NULL; // Handle to the radio playback task

// Mutexes for protecting shared resources
static SemaphoreHandle_t s_radio_mutex = NULL; // Protects radio state variables
static SemaphoreHandle_t s_mp3_mutex __attribute__((unused)) = NULL; // Protects MP3 buffer access

static volatile bool s_radio_task_finished = true; // Initially true, indicates task completion state

// Helper to format size_t values for logging
#define SIZE_FMT "%u"
#define SIZE_CAST(x) ((unsigned int)(x))

/**
 * @brief Task that plays MP3 audio from a URL or file
 * 
 * This task sets up and manages an audio pipeline to decode MP3 data and output
 * it through the Bluetooth A2DP interface. It uses ESP-ADF components to handle
 * the audio processing.
 *
 * @param url Pointer to a string containing the URL or file path to play
 */
void play_mp3_task(void* url) {
    SAFE_ESP_LOGI(TAG, "Starting MP3 playback for: %s", (char*)url);
    
    // Create an audio pipeline
    audio_pipeline_handle_t pipeline;
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline = audio_pipeline_init(&pipeline_cfg);
    if (!pipeline) {
        SAFE_ESP_LOGE(TAG, "Failed to create pipeline");
        goto task_exit;
    }

    // Configure MP3 decoder with default settings
    mp3_decoder_cfg_t mp3_dec_cfg = DEFAULT_MP3_DECODER_CONFIG();

    // Create an MP3 decoder element
    audio_element_handle_t mp3_decoder = mp3_decoder_init(&mp3_dec_cfg);

    // Create an I2S stream to send audio to Bluetooth
    audio_element_handle_t i2s_stream = i2s_stream_init(&(i2s_stream_cfg_t) {
        .type = AUDIO_STREAM_WRITER,
        .out_rb_size = 8 * 1024,
        .use_alc = false,
        .volume = 50,
        .uninstall_drv = false
    });

    // Set MP3 file source from SPIFFS or URL
    audio_element_set_uri(mp3_decoder, (const char*)url);

    // Register elements in pipeline
    audio_pipeline_register(pipeline, mp3_decoder, "mp3_decoder");
    audio_pipeline_register(pipeline, i2s_stream, "i2s_output");

    // Link pipeline elements (MP3 Decoder → I2S Stream)
    audio_pipeline_link(pipeline, (const char *[]){"mp3_decoder", "i2s_output"}, 2);

    // Initialize Bluetooth A2DP Sink
    esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    esp_periph_set_handle_t set = esp_periph_set_init(&periph_cfg);
    periph_bt_cfg_t bt_cfg = {
        .device_name = "ESP32_Bluetooth_Audio",
        .mode = BLUETOOTH_A2DP_SINK
    };
    esp_periph_handle_t bt_handle = periph_bt_init(&bt_cfg);
    esp_periph_start(set, bt_handle);

    // Start pipeline
    SAFE_ESP_LOGI(TAG, "Starting audio pipeline...");
    audio_pipeline_run(pipeline);

    // Instead of using audio_pipeline_wait_for_event, use a simple polling approach
    vTaskDelay(pdMS_TO_TICKS(1000));  // Give time for pipeline to start
    
    // Poll for pipeline state periodically using a simpler approach
    bool playing = true;
    while (playing) {
        vTaskDelay(pdMS_TO_TICKS(500));  // Check every 0.5 seconds
        
        // Check if the pipeline is running by polling the pipeline state
        audio_element_state_t mp3_state = audio_element_get_state(mp3_decoder);
        if (mp3_state == AEL_STATE_FINISHED || mp3_state == AEL_STATE_ERROR) {
            SAFE_ESP_LOGI(TAG, "MP3 playback finished or encountered an error");
            playing = false;
        }
        
        // Also check for stop requests
        if (!s_radio_active) {
            SAFE_ESP_LOGI(TAG, "Playback stopped by request");
            playing = false;
        }
    }

    // Cleanup and release all resources
    SAFE_ESP_LOGI(TAG, "Cleaning up audio resources");
    audio_pipeline_stop(pipeline);
    audio_pipeline_wait_for_stop(pipeline);
    audio_pipeline_terminate(pipeline);
    audio_pipeline_unregister(pipeline, mp3_decoder);
    audio_pipeline_unregister(pipeline, i2s_stream);
    audio_pipeline_deinit(pipeline);
    audio_element_deinit(mp3_decoder);
    audio_element_deinit(i2s_stream);
    esp_periph_set_stop_all(set);
    esp_periph_set_destroy(set);
    
task_exit:
    // Delete task from watchdog and mark as finished
    esp_task_wdt_delete(NULL);
    s_radio_task_finished = true;
    vTaskDelete(NULL);
}

/**
 * @brief Play an MP3 file from SPIFFS storage
 * 
 * This function validates input, checks if radio is already active,
 * ensures SPIFFS is mounted, and then creates a task to play the MP3 file.
 *
 * @param file_name Name of the MP3 file stored in SPIFFS
 * @return ESP_OK if task creation succeeds, appropriate error code otherwise
 */
esp_err_t mp3_play_file(const char* file_name) {
    SAFE_ESP_LOGI(TAG, "radio_play: called with file: %s", file_name);
    // Initialize radio mutex if not already created
    if (!s_radio_mutex) {
        s_radio_mutex = xSemaphoreCreateMutex();
        if (!s_radio_mutex) {
            SAFE_ESP_LOGE(TAG, "Failed to create radio mutex");
            return ESP_ERR_NO_MEM;
        }
    }

    // Check if radio is already active (mutex protected)
    if (xSemaphoreTake(s_radio_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        SAFE_ESP_LOGE(TAG, "Failed to take radio mutex");
        return ESP_ERR_TIMEOUT;
    }
    
    if (s_radio_active) {
        xSemaphoreGive(s_radio_mutex);
        SAFE_ESP_LOGW(TAG, "Radio already playing");
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreGive(s_radio_mutex);

    // Validate file name
    if (!file_name) {
        SAFE_ESP_LOGE(TAG, "Invalid file name");
        return ESP_ERR_INVALID_ARG;
    }

    // Ensure SPIFFS is mounted and ready
    if (!is_spiffs_mounted()) {
        esp_err_t ret = init_spiffs();
        if (ret != ESP_OK) {
            SAFE_ESP_LOGE(TAG, "SPIFFS initialization failed: %s", esp_err_to_name(ret));
            return ret;
        }
    }

    // Double-check SPIFFS mount status
    esp_err_t ret = mount_spiffs_fs();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        SAFE_ESP_LOGE(TAG, "SPIFFS mount failed, cannot play sound");
        return ret;
    }

    if (!is_spiffs_mounted()) {
        SAFE_ESP_LOGE(TAG, "SPIFFS is not mounted, cannot play sound");
        return ESP_ERR_INVALID_STATE;
    }

    // Build full path to file
    char fullpath[64];
    int path_len = snprintf(fullpath, sizeof(fullpath), "%s/%s", SPIFFS_BASE_PATH, file_name);
    // Check for path formatting errors
    if (path_len < 0 || path_len >= sizeof(fullpath)) {
        SAFE_ESP_LOGE(TAG, "Path too long or snprintf error for file: %s", file_name);
        return ESP_FAIL;
    }

    // Create task to play the MP3 file
    if (xTaskCreate(play_mp3_task, "radio_task", 8192, (void*)file_name, 5, &s_radio_task_handle) != pdPASS) {
        SAFE_ESP_LOGE(TAG, "Failed to create radio task");
        return ESP_ERR_NO_MEM;
    }
    
    return ESP_OK;
}

/**
 * @brief Play an MP3 stream from a URL
 * 
 * Similar to mp3_play_file but designed for streaming from URLs.
 * Checks if radio is already active and creates a task to stream the audio.
 *
 * @param url URL of the MP3 stream to play
 * @return ESP_OK if task creation succeeds, appropriate error code otherwise
 */
esp_err_t mp3_play_stream(const char *url) {
    SAFE_ESP_LOGI(TAG, "radio_play: called with URL: %s", url);
    // Initialize radio mutex if not already created
    if (!s_radio_mutex) {
        s_radio_mutex = xSemaphoreCreateMutex();
        if (!s_radio_mutex) {
            SAFE_ESP_LOGE(TAG, "Failed to create radio mutex");
            return ESP_ERR_NO_MEM;
        }
    }

    // Check if radio is already active (mutex protected)
    if (xSemaphoreTake(s_radio_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        SAFE_ESP_LOGE(TAG, "Failed to take radio mutex");
        return ESP_ERR_TIMEOUT;
    }
    
    if (s_radio_active) {
        xSemaphoreGive(s_radio_mutex);
        SAFE_ESP_LOGW(TAG, "Radio already playing");
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreGive(s_radio_mutex);

    // Create task to play the MP3 stream
    if (xTaskCreate(play_mp3_task, "radio_task", 8192, (void*)url, 5, &s_radio_task_handle) != pdPASS) {
        SAFE_ESP_LOGE(TAG, "Failed to create radio task");
        return ESP_ERR_NO_MEM;
    }
    
    return ESP_OK;
}

/**
 * @brief Stop any active radio playback
 * 
 * Takes control of the BT resource mutex and stops any active radio playback.
 * Allows some time for pending ACL transmissions to complete.
 *
 * @return ESP_OK if successful, appropriate error code otherwise
 */
esp_err_t radio_stop(void) {
    SAFE_ESP_LOGI(TAG, "radio_stop: called");
    // Acquire mutex before modifying shared resources
    if (!take_bt_resource_mutex(pdMS_TO_TICKS(100))) {
        if (s_bt_mutex_owner != NULL) {
            SAFE_ESP_LOGE(TAG, "Failed to take radio mutex; currently held by task: %p, name: %s",
                          s_bt_mutex_owner, pcTaskGetName(s_bt_mutex_owner));
        } else {
            SAFE_ESP_LOGE(TAG, "Failed to take radio mutex; owner unknown");
        }
        return ESP_ERR_TIMEOUT;
    }
    
    // Check if radio is not active
    if (!s_radio_active) {
        give_bt_resource_mutex();
        SAFE_ESP_LOGW(TAG, "Radio not active");
        return ESP_OK;
    }

    // Mark radio as inactive
    s_radio_active = false;

    // Wait for pending ACL transmissions to complete
    vTaskDelay(pdMS_TO_TICKS(500)); // Adjust delay as needed

    // Clear task handle and release mutex
    s_radio_task_handle = NULL;
    give_bt_resource_mutex();
    return ESP_OK;
}

/**
 * @brief Set the active state of radio streaming
 * 
 * Sets the internal flag to track if radio streaming is active.
 * Used by other components to notify the radio module of activity changes.
 *
 * @param active Boolean indicating if radio should be active
 */
void radio_set_active(bool active) {
    SAFE_ESP_LOGI(TAG, "radio_set_active: Changing active state to: %d", active);
    s_radio_streaming_active = active;
}

/**
 * @brief Check if the radio task has finished
 * 
 * Accessor function to allow external components to check if the radio
 * playback task has completed execution.
 *
 * @return true if the radio task has finished, false otherwise
 */
bool radio_task_is_finished(void) {
    return s_radio_task_finished;
}

