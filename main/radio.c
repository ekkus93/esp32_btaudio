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
#include <audio_element.h>
#include <esp_peripherals.h>
#include "freertos/semphr.h"
#include <mp3_decoder.h>
#include <i2s_stream.h>
#include <bluetooth_service.h>

// Conditionally include or define stubs for periph_bt
#if __has_include("periph_bt.h")
#include "periph_bt.h"
#else
typedef struct {
    const char *device_name;
    int mode;
} periph_bt_cfg_t;
#define BLUETOOTH_A2DP_SINK 1
static inline void *periph_bt_init(const periph_bt_cfg_t *cfg) { (void)cfg; return NULL; }
#endif

// External function declarations
extern bool take_bt_resource_mutex(TickType_t timeout);
extern void give_bt_resource_mutex(void);
extern TaskHandle_t s_bt_mutex_owner;

#define TAG "RADIO"
#define BUFFER_SIZE 256       // Buffer size for data processing
#define PCM_RING_BUFFER_SIZE (2 * 1024)  // Size for PCM ring buffer
#define MP3_BUFFER_SIZE (2 * 1024)       // Size for MP3 buffer

// Module-level state variables
static bool s_radio_active = false;          // Indicates if radio module is currently active
static bool s_radio_streaming_active = false; // Indicates if streaming is in progress

static TaskHandle_t s_radio_task_handle = NULL; // Handle to the radio playback task

// Mutexes for protecting shared resources
static SemaphoreHandle_t s_radio_mutex = NULL; // Protects radio state variables

static volatile bool s_radio_task_finished = true; // Initially true, indicates task completion state

/**
 * @brief Task that plays MP3 audio from a URL or file
 * 
 * This task sets up and manages an audio pipeline to decode MP3 data and output
 * it through the I2S interface.
 *
 * @param url Pointer to a string containing the URL or file path to play
 */
void play_mp3_task(void* url) {
    char *uri = (char*)url;
    SAFE_ESP_LOGI(TAG, "Starting MP3 playback for: %s", uri);
    
    // Initialize variables with explicit NULL values
    audio_pipeline_handle_t pipeline = NULL;
    audio_element_handle_t mp3_decoder = NULL;
    audio_element_handle_t i2s_stream = NULL;
    
    // First, check if we have enough memory to proceed
    size_t free_heap = esp_get_free_heap_size();
    SAFE_ESP_LOGI(TAG, "Free heap before creating pipeline: %u bytes", (unsigned int)free_heap);
    
    // We need at least 40KB of free heap to safely play MP3s
    if (free_heap < 40000) {
        SAFE_ESP_LOGE(TAG, "Insufficient memory to start MP3 playback (need 40KB, have %u bytes)", (unsigned int)free_heap);
        goto exit;
    }
    
    // Mark radio as active and task as running
    s_radio_active = true;
    s_radio_task_finished = false;
    
    // Create an audio pipeline with minimal memory allocation
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline_cfg.rb_size = 2 * 1024; // Smaller ring buffer
    pipeline = audio_pipeline_init(&pipeline_cfg);
    if (!pipeline) {
        SAFE_ESP_LOGE(TAG, "Failed to create pipeline");
        goto exit;
    }

    // Configure MP3 decoder with memory-optimized settings
    mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
    mp3_cfg.task_stack = 8 * 1024;    // Increase task stack for decoder
    mp3_cfg.task_core = 0;           // Run on core 0 (away from BT stack)
    mp3_cfg.out_rb_size = 4 * 1024;  // Smaller output ring buffer
    mp3_decoder = mp3_decoder_init(&mp3_cfg);
    if (!mp3_decoder) {
        SAFE_ESP_LOGE(TAG, "Failed to create MP3 decoder");
        goto exit;
    }

    // Use I2S stream instead of BT sink to avoid memory contention
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.type = AUDIO_STREAM_WRITER;
    i2s_cfg.task_stack = 4 * 1024;   // Sufficient stack size
    i2s_cfg.task_core = 0;           // Run on core 0
    i2s_cfg.task_prio = 5;           // Lower priority
    i2s_cfg.out_rb_size = 4 * 1024;  // Smaller ring buffer
    i2s_cfg.use_alc = false;         // No automatic level control
    i2s_cfg.volume = 50;
    
    i2s_stream = i2s_stream_init(&i2s_cfg);
    if (!i2s_stream) {
        SAFE_ESP_LOGE(TAG, "Failed to create I2S stream");
        goto exit;
    }

    // Pause briefly to allow memory allocations to complete
    vTaskDelay(pdMS_TO_TICKS(50));

    // Set MP3 file source
    SAFE_ESP_LOGI(TAG, "Setting URI: %s", uri);
    audio_element_set_uri(mp3_decoder, uri);

    // Register elements in pipeline
    SAFE_ESP_LOGI(TAG, "Registering pipeline elements");
    esp_err_t ret;
    ret = audio_pipeline_register(pipeline, mp3_decoder, "mp3_decoder");
    if (ret != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "Failed to register MP3 decoder: %s", esp_err_to_name(ret));
        goto exit;
    }
    
    ret = audio_pipeline_register(pipeline, i2s_stream, "i2s_output");
    if (ret != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "Failed to register I2S stream: %s", esp_err_to_name(ret));
        goto exit;
    }

    // Link pipeline elements (MP3 Decoder → I2S Stream)
    SAFE_ESP_LOGI(TAG, "Linking pipeline elements");
    ret = audio_pipeline_link(pipeline, (const char *[]){"mp3_decoder", "i2s_output"}, 2);
    if (ret != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "Failed to link pipeline elements: %s", esp_err_to_name(ret));
        goto exit;
    }

    // Check memory again before starting
    free_heap = esp_get_free_heap_size();
    SAFE_ESP_LOGI(TAG, "Free heap before starting pipeline: %u bytes", (unsigned int)free_heap);
    
    if (free_heap < 20000) {  // Need at least 20KB free to run
        SAFE_ESP_LOGE(TAG, "Insufficient memory before starting pipeline");
        goto exit;
    }

    // Start pipeline
    SAFE_ESP_LOGI(TAG, "Starting audio pipeline...");
    if ((ret = audio_pipeline_run(pipeline)) != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "Failed to start pipeline: %s", esp_err_to_name(ret));
        goto exit;
    }

    // Poll for pipeline state
    bool playing = true;
    int check_count = 0;
    while (playing && check_count < 60) {  // Timeout after 30 seconds
        vTaskDelay(pdMS_TO_TICKS(500));  // Check every 0.5 seconds
        check_count++;
        
        // Check if the pipeline is running
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
        
        // Log memory usage periodically
        if (check_count % 10 == 0) {
            SAFE_ESP_LOGI(TAG, "Free heap during playback: %u bytes", (unsigned int)esp_get_free_heap_size());
        }
    }

exit:
    // Cleanup and release all resources
    SAFE_ESP_LOGI(TAG, "Cleaning up audio resources");
    
    // Stop radio active flag first to ensure other functions know we're shutting down
    s_radio_active = false;
    
    // Use #pragma directive to silence the uninitialized variable warning
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
    
    // Clean up the pipeline if it was initialized
    if (pipeline != NULL) {
        audio_pipeline_stop(pipeline);
        audio_pipeline_wait_for_stop(pipeline);
        audio_pipeline_terminate(pipeline);
        
        if (mp3_decoder != NULL) {
            audio_pipeline_unregister(pipeline, mp3_decoder);
        }
        
        if (i2s_stream != NULL) {
            audio_pipeline_unregister(pipeline, i2s_stream);
        }
        
        audio_pipeline_deinit(pipeline);
    }
    
    // Free individual elements
    if (mp3_decoder != NULL) {
        audio_element_deinit(mp3_decoder);
        mp3_decoder = NULL;
    }
    
    if (i2s_stream != NULL) {
        audio_element_deinit(i2s_stream);
        i2s_stream = NULL;
    }
    
    // Restore compiler warnings
    #pragma GCC diagnostic pop
    
    // If we made a copy of the URI string, free it
    if (uri != url && uri != NULL) {
        free(uri);
        uri = NULL;
    }
    
    // Force a garbage collection pause to allow memory to be freed
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Final cleanup
    s_radio_task_finished = true;
    
    // Remove task from watchdog
    esp_task_wdt_delete(NULL);
    SAFE_ESP_LOGI(TAG, "Radio task exiting, free heap: %u bytes", (unsigned int)esp_get_free_heap_size());
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
    
    // Check available memory before proceeding
    size_t free_heap = esp_get_free_heap_size();
    if (free_heap < 30000) {  // Need at least 30KB free to start
        SAFE_ESP_LOGE(TAG, "Insufficient memory to start playback: %u bytes available", (unsigned int)free_heap);
        return ESP_ERR_NO_MEM;
    }
    
    // Initialize radio mutex if not already created
    if (!s_radio_mutex) {
        s_radio_mutex = xSemaphoreCreateMutex();
        if (!s_radio_mutex) {
            SAFE_ESP_LOGE(TAG, "Failed to create radio mutex");
            return ESP_ERR_NO_MEM;
        }
    }

    // Check if radio is already active
    if (xSemaphoreTake(s_radio_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        SAFE_ESP_LOGE(TAG, "Failed to take radio mutex");
        return ESP_ERR_TIMEOUT;
    }
    
    if (s_radio_active) {
        xSemaphoreGive(s_radio_mutex);
        SAFE_ESP_LOGW(TAG, "Radio already playing");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Mark radio as active
    s_radio_active = true;
    xSemaphoreGive(s_radio_mutex);

    // Validate file name
    if (!file_name) {
        SAFE_ESP_LOGE(TAG, "Invalid file name");
        s_radio_active = false;
        return ESP_ERR_INVALID_ARG;
    }

    // Reset task finished flag
    s_radio_task_finished = false;
    
    // Ensure SPIFFS is mounted and ready
    if (!is_spiffs_mounted()) {
        SAFE_ESP_LOGI(TAG, "SPIFFS not mounted, attempting to mount");
        esp_err_t ret = mount_spiffs_fs();
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            SAFE_ESP_LOGI(TAG, "Direct mount failed, trying full initialization");
            ret = init_spiffs();
            if (ret != ESP_OK) {
                SAFE_ESP_LOGE(TAG, "SPIFFS initialization failed: %s", esp_err_to_name(ret));
                s_radio_active = false;
                return ret;
            }
        }
    }

    SAFE_ESP_LOGI(TAG, "SPIFFS mount status: %s", is_spiffs_mounted() ? "Mounted" : "Not mounted");

    if (!is_spiffs_mounted()) {
        SAFE_ESP_LOGE(TAG, "SPIFFS is not mounted, cannot play sound");
        s_radio_active = false;
        return ESP_ERR_INVALID_STATE;
    }

    // Build full path to file
    char *fullpath = malloc(128); // Allocate memory for the path
    if (!fullpath) {
        SAFE_ESP_LOGE(TAG, "Failed to allocate memory for file path");
        s_radio_active = false;
        return ESP_ERR_NO_MEM;
    }
    
    int path_len = snprintf(fullpath, 128, "%s/%s", SPIFFS_BASE_PATH, file_name);
    if (path_len < 0 || path_len >= 128) {
        SAFE_ESP_LOGE(TAG, "Path too long or snprintf error for file: %s", file_name);
        free(fullpath);
        s_radio_active = false;
        return ESP_FAIL;
    }

    // Create task to play the MP3 file with higher stack size
    if (xTaskCreate(play_mp3_task, "radio_task", 12 * 1024, fullpath, 10, &s_radio_task_handle) != pdPASS) {
        SAFE_ESP_LOGE(TAG, "Failed to create radio task");
        free(fullpath);
        s_radio_active = false;
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

    // Check if radio is already active
    if (xSemaphoreTake(s_radio_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        SAFE_ESP_LOGE(TAG, "Failed to take radio mutex");
        return ESP_ERR_TIMEOUT;
    }
    
    if (s_radio_active) {
        xSemaphoreGive(s_radio_mutex);
        SAFE_ESP_LOGW(TAG, "Radio already playing");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Mark radio as active
    s_radio_active = true;
    xSemaphoreGive(s_radio_mutex);

    // Create a copy of the URL string to ensure it remains valid
    char *url_copy = strdup(url);
    if (!url_copy) {
        SAFE_ESP_LOGE(TAG, "Failed to allocate memory for URL");
        s_radio_active = false;
        return ESP_ERR_NO_MEM;
    }

    // Create task to play the MP3 stream
    if (xTaskCreate(play_mp3_task, "radio_task", 8192, url_copy, 5, &s_radio_task_handle) != pdPASS) {
        SAFE_ESP_LOGE(TAG, "Failed to create radio task");
        free(url_copy);
        s_radio_active = false;
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

    // Mark radio as inactive to signal the task to stop
    s_radio_active = false;

    // Wait for pending operations to complete
    vTaskDelay(pdMS_TO_TICKS(500));

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

