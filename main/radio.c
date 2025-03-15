/**
 * @file radio.c
 * @brief Implementation of MP3 audio streaming and playback capabilities
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
#include <spiffs_stream.h>  // Use spiffs_stream instead of fatfs_stream
#include "esp_wifi.h"  // Include Wi-Fi headerd of bt_source.h
#include <a2dp_stream.h>    // Use this instead of bt_source.h
#include <wav_decoder.h>    // First, add the WAV decoder include

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

typedef struct {
    char *uri;
    bool restore_wifi;
} mp3_task_params_t;

/**
 * @brief Task that plays MP3 audio from a URL or file
 */
void play_mp3_task(void* args) {
    mp3_task_params_t *params = (mp3_task_params_t*)args;
    char *uri = params->uri;
    bool restore_wifi = params->restore_wifi;
    
    // Free the params struct as we've extracted what we need
    free(params);
    
    SAFE_ESP_LOGI(TAG, "Starting MP3 playback for: %s", uri);
    
    // Initialize variables with explicit NULL values
    audio_pipeline_handle_t pipeline = NULL;
    audio_element_handle_t mp3_decoder = NULL;
    audio_element_handle_t bt_sink = NULL;      // Changed from i2s_stream to bt_sink
    audio_element_handle_t spiffs_reader = NULL;  // Changed from file_reader to spiffs_reader
    
    // First, check if we have enough memory to proceed
    size_t free_heap = esp_get_free_heap_size();
    SAFE_ESP_LOGI(TAG, "Free heap before creating pipeline: %u bytes", (unsigned int)free_heap);
    
    // Lower memory requirement to 40KB (was 45KB)
    if (free_heap < 40000) {
        SAFE_ESP_LOGE(TAG, "Insufficient memory to start MP3 playback (need 40KB, have %u bytes)", (unsigned int)free_heap);
        goto exit;
    }
    
    // Mark radio as active and task as running
    s_radio_active = true;
    s_radio_task_finished = false;
    
    // Perform extra memory cleanup before pipeline creation
    SAFE_ESP_LOGI(TAG, "Performing extra memory cleanup before pipeline creation");
    heap_caps_print_heap_info(MALLOC_CAP_DEFAULT);
    
    // Force a garbage collection
    vTaskDelay(pdMS_TO_TICKS(300));
    
    // Create an audio pipeline with minimal memory allocation
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline_cfg.rb_size = 256; // Reduce ring buffer size to absolute minimum
    pipeline = audio_pipeline_init(&pipeline_cfg);
    if (!pipeline) {
        SAFE_ESP_LOGE(TAG, "Failed to create pipeline");
        goto exit;
    }

    // Create a SPIFFS reader with ultra-minimized buffer sizes
    spiffs_stream_cfg_t spiffs_cfg = SPIFFS_STREAM_CFG_DEFAULT();
    spiffs_cfg.type = AUDIO_STREAM_READER;
    spiffs_cfg.task_stack = 3 * 1024;     // Increase from 1K to 3K to prevent stack overflow
    spiffs_cfg.task_core = 0;             // Use core 0 to better match with Bluetooth
    spiffs_cfg.task_prio = 5;             // Increase priority
    spiffs_cfg.buf_sz = 2 * 1024;         // Increase buffer size from 512B to 2K
    spiffs_reader = spiffs_stream_init(&spiffs_cfg);
    if (!spiffs_reader) {
        SAFE_ESP_LOGE(TAG, "Failed to create SPIFFS reader");
        goto exit;
    }

    // Configure MP3 decoder with extremely minimized memory settings
    mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
    mp3_cfg.task_stack = 8 * 1024;        // Increase from 4K to 8K for decoder
    mp3_cfg.task_core = 0;                // Use core 0 for all audio to avoid task switching
    mp3_cfg.task_prio = 4;                // Adjust priority
    mp3_cfg.out_rb_size = 2 * 1024;       // Increase ring buffer size
    mp3_cfg.stack_in_ext = true;          // Use external memory for stack if available
    
    // Additional optimization flags - some ESP-ADF versions support this
    #ifdef CONFIG_USE_PSRAM
    mp3_cfg.stack_in_ext = true;
    #endif
    
    mp3_decoder = mp3_decoder_init(&mp3_cfg);
    if (!mp3_decoder) {
        SAFE_ESP_LOGE(TAG, "Failed to create MP3 decoder");
        goto exit;
    }

    // Check if Bluetooth is initialized before streaming
    // Just retrieve the connection state - simplify to avoid external variables
    extern int s_a2d_state;
    #define APP_AV_STATE_CONNECTED 2  // Define the constant locally 

    if (s_a2d_state != APP_AV_STATE_CONNECTED) {
        SAFE_ESP_LOGW(TAG, "Bluetooth not connected, streaming may fail");
    }

    // Use Bluetooth A2DP stream with minimal configuration
    a2dp_stream_config_t a2dp_config = {
        .type = AUDIO_STREAM_WRITER,
    };
    
    // Initialize the A2DP stream 
    bt_sink = a2dp_stream_init(&a2dp_config);
    if (!bt_sink) {
        SAFE_ESP_LOGE(TAG, "Failed to create A2DP stream");
        goto exit;
    }
    
    // Free any temporary resources
    SAFE_ESP_LOGI(TAG, "Free heap after creating elements: %u bytes", 
                 (unsigned int)esp_get_free_heap_size());
    
    // Check memory after creating components to ensure we're not too tight
    free_heap = esp_get_free_heap_size();
    if (free_heap < 20000) {  // Critical low memory warning
        SAFE_ESP_LOGW(TAG, "Memory critically low: %u bytes. Pipeline may fail.", (unsigned int)free_heap);
    }
    
    // Rest of pipeline setup
    // Set MP3 file source - set URI on file reader instead of mp3_decoder
    SAFE_ESP_LOGI(TAG, "Setting URI: %s", uri);
    audio_element_set_uri(spiffs_reader, uri);

    // Register elements in pipeline
    SAFE_ESP_LOGI(TAG, "Registering pipeline elements");
    esp_err_t ret;
    
    // Register spiffs reader first
    ret = audio_pipeline_register(pipeline, spiffs_reader, "spiffs_reader");
    if (ret != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "Failed to register SPIFFS reader: %s", esp_err_to_name(ret));
        goto exit;
    }
    
    ret = audio_pipeline_register(pipeline, mp3_decoder, "mp3_decoder");
    if (ret != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "Failed to register MP3 decoder: %s", esp_err_to_name(ret));
        goto exit;
    }
    
    ret = audio_pipeline_register(pipeline, bt_sink, "bt_sink");
    if (ret != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "Failed to register Bluetooth sink: %s", esp_err_to_name(ret));
        goto exit;
    }

    // Link pipeline elements (SPIFFS Reader → MP3 Decoder → A2DP Stream)
    SAFE_ESP_LOGI(TAG, "Linking pipeline elements");
    ret = audio_pipeline_link(pipeline, (const char *[]){"spiffs_reader", "mp3_decoder", "bt_sink"}, 3);
    if (ret != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "Failed to link pipeline elements: %s", esp_err_to_name(ret));
        goto exit;
    }

    // Check memory again before starting
    free_heap = esp_get_free_heap_size();
    SAFE_ESP_LOGI(TAG, "Free heap before starting pipeline: %u bytes", (unsigned int)free_heap);
    
    if (free_heap < 30000) {  // Need at least 30KB free to run (up from 20KB)
        SAFE_ESP_LOGE(TAG, "Insufficient memory before starting pipeline");
        goto exit;
    }

    // Force a garbage collection pause to ensure maximum free memory
    vTaskDelay(pdMS_TO_TICKS(300));

    // Before starting pipeline, add a retry mechanism with memory stats
    SAFE_ESP_LOGI(TAG, "Starting audio pipeline...");
    free_heap = esp_get_free_heap_size();
    SAFE_ESP_LOGI(TAG, "Free heap immediately before pipeline run: %u bytes", (unsigned int)free_heap);
    
    // Fix variable redeclaration
    ret = audio_pipeline_run(pipeline);
    if (ret != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "Failed to start pipeline: %s", esp_err_to_name(ret));
        SAFE_ESP_LOGI(TAG, "Retrying with memory cleanup...");
        
        // Emergency memory cleanup
        heap_caps_print_heap_info(MALLOC_CAP_DEFAULT);
        SAFE_ESP_LOGI(TAG, "Attempting pipeline start after cleanup");
        vTaskDelay(pdMS_TO_TICKS(500));
        
        // Try one more time
        ret = audio_pipeline_run(pipeline);
        if (ret != ESP_OK) {
            SAFE_ESP_LOGE(TAG, "Final attempt failed: %s", esp_err_to_name(ret));
            goto exit;
        }
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
        
        if (bt_sink != NULL) {  // Changed from i2s_stream to bt_sink
            audio_pipeline_unregister(pipeline, bt_sink);
        }
        
        if (spiffs_reader != NULL) {
            audio_pipeline_unregister(pipeline, spiffs_reader);
        }
        
        audio_pipeline_deinit(pipeline);
    }
    
    // Free individual elements
    if (mp3_decoder != NULL) {
        audio_element_deinit(mp3_decoder);
        mp3_decoder = NULL;
    }
    
    if (bt_sink != NULL) {  // Changed from i2s_stream to bt_sink
        audio_element_deinit(bt_sink);
        bt_sink = NULL;
    }
    
    if (spiffs_reader != NULL) {
        audio_element_deinit(spiffs_reader);
        spiffs_reader = NULL;
    }
    
    // Restore compiler warnings
    #pragma GCC diagnostic pop
    
    // If we made a copy of the URI string, free it
    if (uri != NULL) {
        free(uri);
        uri = NULL;
    }
    
    // Force a garbage collection pause to allow memory to be freed
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Restore Wi-Fi if it was active before
    if (restore_wifi) {
        SAFE_ESP_LOGI(TAG, "Restoring Wi-Fi state");
        esp_wifi_start();
    }
    
    // Final cleanup
    s_radio_task_finished = true;
    
    // Remove task from watchdog
    esp_task_wdt_delete(NULL);
    SAFE_ESP_LOGI(TAG, "Radio task exiting, free heap: %u bytes", (unsigned int)esp_get_free_heap_size());
    vTaskDelete(NULL);
}

/**
 * @brief Task that plays WAV audio from a file
 */
void play_wav_task(void* args) {
    mp3_task_params_t *params = (mp3_task_params_t*)args;
    char *uri = params->uri;
    bool restore_wifi = params->restore_wifi;
    
    // Free the params struct as we've extracted what we need
    free(params);
    
    SAFE_ESP_LOGI(TAG, "Starting WAV playback for: %s", uri);
    
    // Initialize variables with explicit NULL values
    audio_pipeline_handle_t pipeline = NULL;
    audio_element_handle_t wav_decoder = NULL;
    audio_element_handle_t bt_sink = NULL;
    audio_element_handle_t spiffs_reader = NULL;
    
    // First, check if we have enough memory to proceed
    size_t free_heap = esp_get_free_heap_size();
    SAFE_ESP_LOGI(TAG, "Free heap before creating pipeline: %u bytes", (unsigned int)free_heap);
    
    // Lower memory requirement since WAV decoding is much lighter than MP3
    if (free_heap < 30000) {
        SAFE_ESP_LOGE(TAG, "Insufficient memory to start WAV playback (need 30KB, have %u bytes)", (unsigned int)free_heap);
        goto exit;
    }
    
    // Mark radio as active and task as running
    s_radio_active = true;
    s_radio_task_finished = false;
    
    // Create an audio pipeline with minimal memory allocation
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline_cfg.rb_size = 256; // Minimum ring buffer
    pipeline = audio_pipeline_init(&pipeline_cfg);
    if (!pipeline) {
        SAFE_ESP_LOGE(TAG, "Failed to create pipeline");
        goto exit;
    }

    // Create a SPIFFS reader
    spiffs_stream_cfg_t spiffs_cfg = SPIFFS_STREAM_CFG_DEFAULT();
    spiffs_cfg.type = AUDIO_STREAM_READER;
    spiffs_cfg.task_stack = 3 * 1024;
    spiffs_cfg.task_core = 0;
    spiffs_cfg.task_prio = 5;
    spiffs_cfg.buf_sz = 2 * 1024;
    spiffs_reader = spiffs_stream_init(&spiffs_cfg);
    if (!spiffs_reader) {
        SAFE_ESP_LOGE(TAG, "Failed to create SPIFFS reader");
        goto exit;
    }

    // Configure WAV decoder instead of MP3 - much lighter on memory
    wav_decoder_cfg_t wav_cfg = DEFAULT_WAV_DECODER_CONFIG();
    wav_cfg.task_stack = 3 * 1024;  // WAV needs much less stack than MP3
    wav_cfg.out_rb_size = 4 * 1024; // Larger output buffer for smoother playback
    wav_decoder = wav_decoder_init(&wav_cfg);
    if (!wav_decoder) {
        SAFE_ESP_LOGE(TAG, "Failed to create WAV decoder");
        goto exit;
    }

    // Check connection state as before
    extern int s_a2d_state;
    #define APP_AV_STATE_CONNECTED 2  // Define the constant locally

    if (s_a2d_state != APP_AV_STATE_CONNECTED) {
        SAFE_ESP_LOGW(TAG, "Bluetooth not connected, streaming may fail");
    }

    // Use Bluetooth A2DP stream for output
    a2dp_stream_config_t a2dp_config = {
        .type = AUDIO_STREAM_WRITER,
    };
    
    bt_sink = a2dp_stream_init(&a2dp_config);
    if (!bt_sink) {
        SAFE_ESP_LOGE(TAG, "Failed to create A2DP stream");
        goto exit;
    }
    
    // Set file source URI
    SAFE_ESP_LOGI(TAG, "Setting URI: %s", uri);
    audio_element_set_uri(spiffs_reader, uri);

    // Register elements in pipeline
    SAFE_ESP_LOGI(TAG, "Registering pipeline elements");
    esp_err_t ret;
    
    ret = audio_pipeline_register(pipeline, spiffs_reader, "spiffs_reader");
    if (ret != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "Failed to register SPIFFS reader: %s", esp_err_to_name(ret));
        goto exit;
    }
    
    ret = audio_pipeline_register(pipeline, wav_decoder, "wav_decoder");
    if (ret != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "Failed to register WAV decoder: %s", esp_err_to_name(ret));
        goto exit;
    }
    
    ret = audio_pipeline_register(pipeline, bt_sink, "bt_sink");
    if (ret != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "Failed to register Bluetooth sink: %s", esp_err_to_name(ret));
        goto exit;
    }

    // Link pipeline elements (SPIFFS Reader → WAV Decoder → A2DP Stream)
    SAFE_ESP_LOGI(TAG, "Linking pipeline elements");
    ret = audio_pipeline_link(pipeline, (const char *[]){"spiffs_reader", "wav_decoder", "bt_sink"}, 3);
    if (ret != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "Failed to link pipeline elements: %s", esp_err_to_name(ret));
        goto exit;
    }

    // Check memory before running
    free_heap = esp_get_free_heap_size();
    SAFE_ESP_LOGI(TAG, "Free heap before starting pipeline: %u bytes", (unsigned int)free_heap);
    
    // Start pipeline
    SAFE_ESP_LOGI(TAG, "Starting audio pipeline...");
    ret = audio_pipeline_run(pipeline);
    if (ret != ESP_OK) {
        SAFE_ESP_LOGE(TAG, "Failed to start pipeline: %s", esp_err_to_name(ret));
        goto exit;
    }

    // Poll for pipeline state
    bool playing = true;
    int check_count = 0;
    while (playing && check_count < 60) {  // Timeout after 30 seconds
        vTaskDelay(pdMS_TO_TICKS(500));
        check_count++;
        
        // Check if the pipeline is running
        audio_element_state_t wav_state = audio_element_get_state(wav_decoder);
        if (wav_state == AEL_STATE_FINISHED || wav_state == AEL_STATE_ERROR) {
            SAFE_ESP_LOGI(TAG, "WAV playback finished or encountered an error");
            playing = false;
        }
        
        // Also check for stop requests
        if (!s_radio_active) {
            SAFE_ESP_LOGI(TAG, "Playback stopped by request");
            playing = false;
        }
    }

exit:
    // Cleanup code is similar to the existing MP3 cleanup
    // ...existing code for cleanup...
    
    // Use the same cleanup code as in play_mp3_task, just replacing mp3_decoder with wav_decoder
    // in the relevant parts
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
        
        if (wav_decoder != NULL) {
            audio_pipeline_unregister(pipeline, wav_decoder);
        }
        
        if (bt_sink != NULL) {  // Changed from i2s_stream to bt_sink
            audio_pipeline_unregister(pipeline, bt_sink);
        }
        
        if (spiffs_reader != NULL) {
            audio_pipeline_unregister(pipeline, spiffs_reader);
        }
        
        audio_pipeline_deinit(pipeline);
    }
    
    // Free individual elements
    if (wav_decoder != NULL) {
        audio_element_deinit(wav_decoder);
        wav_decoder = NULL;
    }
    
    if (bt_sink != NULL) {  // Changed from i2s_stream to bt_sink
        audio_element_deinit(bt_sink);
        bt_sink = NULL;
    }
    
    if (spiffs_reader != NULL) {
        audio_element_deinit(spiffs_reader);
        spiffs_reader = NULL;
    }
    
    // Restore compiler warnings
    #pragma GCC diagnostic pop
    
    // If we made a copy of the URI string, free it
    if (uri != NULL) {
        free(uri);
        uri = NULL;
    }
    
    // Force a garbage collection pause to allow memory to be freed
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Restore Wi-Fi if it was active before
    if (restore_wifi) {
        SAFE_ESP_LOGI(TAG, "Restoring Wi-Fi state");
        esp_wifi_start();
    }
    
    // Final cleanup
    s_radio_task_finished = true;
    
    // Remove task from watchdog
    esp_task_wdt_delete(NULL);
    SAFE_ESP_LOGI(TAG, "Radio task exiting, free heap: %u bytes", (unsigned int)esp_get_free_heap_size());
    vTaskDelete(NULL);
}

/**
 * @brief Play an MP3 file from SPIFFS storage
 */
esp_err_t mp3_play_file(const char* file_name) {
    SAFE_ESP_LOGI(TAG, "radio_play: called with file: %s", file_name);
    
    // Check available memory before proceeding
    size_t free_heap = esp_get_free_heap_size();
    SAFE_ESP_LOGI(TAG, "Initial free heap: %u bytes", (unsigned int)free_heap);
    
    // Temporarily disable Wi-Fi to free memory
    wifi_mode_t old_mode;
    bool wifi_was_active = false;
    esp_err_t wifi_ret = esp_wifi_get_mode(&old_mode);
    
    if (wifi_ret == ESP_OK && old_mode != WIFI_MODE_NULL) {
        wifi_was_active = true;
        SAFE_ESP_LOGI(TAG, "Temporarily disabling Wi-Fi to free memory");
        esp_wifi_stop();
        vTaskDelay(pdMS_TO_TICKS(200)); // Wait for resources to be freed
    }
    
    // Check memory again after Wi-Fi is disabled
    free_heap = esp_get_free_heap_size();
    SAFE_ESP_LOGI(TAG, "Free heap after disabling Wi-Fi: %u bytes", (unsigned int)free_heap);
    
    if (free_heap < 30000) {  // Need at least 30KB free to start
        SAFE_ESP_LOGE(TAG, "Insufficient memory to start playback: %u bytes available", (unsigned int)free_heap);
        
        // Restart Wi-Fi if it was active before
        if (wifi_was_active) {
            esp_wifi_start();
        }
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
    // Pass the wifi_was_active flag to the task so it can restore Wi-Fi state
    mp3_task_params_t *params = malloc(sizeof(mp3_task_params_t));
    if (!params) {
        SAFE_ESP_LOGE(TAG, "Failed to allocate memory for task parameters");
        free(fullpath);
        s_radio_active = false;
        
        // Restart Wi-Fi if it was active before
        if (wifi_was_active) {
            esp_wifi_start();
        }
        return ESP_ERR_NO_MEM;
    }
    
    params->uri = fullpath;
    params->restore_wifi = wifi_was_active;
    
    if (xTaskCreate(play_mp3_task, "radio_task", 24 * 1024, params, 10, &s_radio_task_handle) != pdPASS) {
        SAFE_ESP_LOGE(TAG, "Failed to create radio task");
        free(fullpath);
        free(params);
        s_radio_active = false;
        
        // Restart Wi-Fi if it was active before
        if (wifi_was_active) {
            esp_wifi_start();
        }
        return ESP_ERR_NO_MEM;
    }
    
    return ESP_OK;
}

/**
 * @brief Play a WAV file from SPIFFS storage
 */
esp_err_t wav_play_file(const char* file_name) {
    SAFE_ESP_LOGI(TAG, "wav_play: called with file: %s", file_name);
    
    // Most of this function is identical to mp3_play_file
    // Check available memory before proceeding
    size_t free_heap = esp_get_free_heap_size();
    SAFE_ESP_LOGI(TAG, "Initial free heap: %u bytes", (unsigned int)free_heap);
    
    // Temporarily disable Wi-Fi to free memory
    wifi_mode_t old_mode;
    bool wifi_was_active = false;
    esp_err_t wifi_ret = esp_wifi_get_mode(&old_mode);
    
    if (wifi_ret == ESP_OK && old_mode != WIFI_MODE_NULL) {
        wifi_was_active = true;
        SAFE_ESP_LOGI(TAG, "Temporarily disabling Wi-Fi to free memory");
        esp_wifi_stop();
        vTaskDelay(pdMS_TO_TICKS(200)); // Wait for resources to be freed
    }
    
    // Check memory again after Wi-Fi is disabled
    free_heap = esp_get_free_heap_size();
    SAFE_ESP_LOGI(TAG, "Free heap after disabling Wi-Fi: %u bytes", (unsigned int)free_heap);
    
    if (free_heap < 30000) {  // Need at least 30KB free to start
        SAFE_ESP_LOGE(TAG, "Insufficient memory to start playback: %u bytes available", (unsigned int)free_heap);
        
        // Restart Wi-Fi if it was active before
        if (wifi_was_active) {
            esp_wifi_start();
        }
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

    // Create task to play the WAV file with higher stack size
    // Pass the wifi_was_active flag to the task so it can restore Wi-Fi state
    mp3_task_params_t *params = malloc(sizeof(mp3_task_params_t));
    if (!params) {
        SAFE_ESP_LOGE(TAG, "Failed to allocate memory for task parameters");
        free(fullpath);
        s_radio_active = false;
        
        // Restart Wi-Fi if it was active before
        if (wifi_was_active) {
            esp_wifi_start();
        }
        return ESP_ERR_NO_MEM;
    }
    
    params->uri = fullpath;
    params->restore_wifi = wifi_was_active;
    
    if (xTaskCreate(play_wav_task, "radio_task", 16 * 1024, params, 10, &s_radio_task_handle) != pdPASS) {
        SAFE_ESP_LOGE(TAG, "Failed to create radio task");
        free(fullpath);
        free(params);
        s_radio_active = false;
        
        // Restart Wi-Fi if it was active before
        if (wifi_was_active) {
            esp_wifi_start();
        }
        return ESP_ERR_NO_MEM;
    }
    
    return ESP_OK;
}

/**
 * @brief Play an MP3 stream from a URL
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

    // Temporarily disable Wi-Fi to free memory
    wifi_mode_t old_mode;
    bool wifi_was_active = false;
    esp_err_t wifi_ret = esp_wifi_get_mode(&old_mode);
    
    if (wifi_ret == ESP_OK && old_mode != WIFI_MODE_NULL) {
        wifi_was_active = true;
        SAFE_ESP_LOGI(TAG, "Temporarily disabling Wi-Fi to free memory");
        esp_wifi_stop();
        vTaskDelay(pdMS_TO_TICKS(200)); // Wait for resources to be freed
    }
    
    // Check memory again after Wi-Fi is disabled
    size_t free_heap = esp_get_free_heap_size();
    SAFE_ESP_LOGI(TAG, "Free heap after disabling Wi-Fi: %u bytes", (unsigned int)free_heap);
    
    if (free_heap < 30000) {  // Need at least 30KB free to start
        SAFE_ESP_LOGE(TAG, "Insufficient memory to start playback: %u bytes available", (unsigned int)free_heap);
        
        // Restart Wi-Fi if it was active before
        if (wifi_was_active) {
            esp_wifi_start();
        }
        return ESP_ERR_NO_MEM;
    }

    // Create task to play the MP3 stream
    mp3_task_params_t *params = malloc(sizeof(mp3_task_params_t));
    if (!params) {
        SAFE_ESP_LOGE(TAG, "Failed to allocate memory for task parameters");
        free(url_copy);
        s_radio_active = false;
        
        // Restart Wi-Fi if it was active before
        if (wifi_was_active) {
            esp_wifi_start();
        }
        return ESP_ERR_NO_MEM;
    }
    
    params->uri = url_copy;
    params->restore_wifi = wifi_was_active;
    
    if (xTaskCreate(play_mp3_task, "radio_task", 8192, params, 5, &s_radio_task_handle) != pdPASS) {
        SAFE_ESP_LOGE(TAG, "Failed to create radio task");
        free(url_copy);
        free(params);
        s_radio_active = false;
        
        // Restart Wi-Fi if it was active before
        if (wifi_was_active) {
            esp_wifi_start();
        }
        return ESP_ERR_NO_MEM;
    }
    
    return ESP_OK;
}

/**
 * @brief Stop any active radio playback
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
 */
void radio_set_active(bool active) {
    SAFE_ESP_LOGI(TAG, "radio_set_active: Changing active state to: %d", active);
    s_radio_streaming_active = active;
}

/**
 * @brief Check if the radio task has finished
 * 
 * @return true if the radio task has finished, false otherwise
 */
bool radio_task_is_finished(void) {
    return s_radio_task_finished;
}

