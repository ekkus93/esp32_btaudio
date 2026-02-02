#include <string.h>
#include <inttypes.h> // Add for PRIu32 macros
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "bt_source.h"
#include "audio_processor.h"
#include "mem_util.h"

static const char *TAG = "BT_STREAM_MGR";

/* State tracking */
static bt_streaming_state_t s_streaming_state = BT_STREAMING_STATE_STOPPED;
static bt_streaming_info_t s_streaming_info = {0};

/* Audio buffer for streaming data */
/* Use the shared audio processor for I2S capture and buffering.
 * bt_streaming_manager consumes audio via audio_processor_read() so
 * it does not maintain its own queue. */
static uint32_t s_stream_start_time = 0;

/* Callback function */
static bt_stream_callback_t s_stream_callback = NULL;
static void* s_stream_callback_data = NULL;

/* Function declarations */
static int32_t bt_audio_data_callback(uint8_t *data, int32_t len);
/* audio task removed: audio_processor performs capture; bt_streaming_manager consumes via audio_processor_read() */
static void update_streaming_state(bt_streaming_state_t new_state);
static uint32_t get_current_time_ms(void);

/*
 * A2DP source data callback - called by A2DP when it needs audio data
 * Changed return type to int32_t to match esp_a2d_source_data_cb_t
 */
static int32_t bt_audio_data_callback(uint8_t *data, int32_t len)
{
    /* If not streaming, return silence immediately */
    if (s_streaming_state != BT_STREAMING_STATE_STREAMING &&
        s_streaming_state != BT_STREAMING_STATE_PAUSED) {
        safe_memset(data, (size_t)len, 0, (size_t)len);
        return len;
    }

    if (s_streaming_state == BT_STREAMING_STATE_PAUSED) {
        safe_memset(data, (size_t)len, 0, (size_t)len);
        return len;
    }

    /* Read from shared audio_processor. audio_processor_read() will
     * return available bytes up to 'len' from its internal audio_queue. */
    size_t bytes_read = 0;
    esp_err_t result = audio_processor_read(data, (size_t)len, &bytes_read);
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "audio_processor_read error: %d", result);
        safe_memset(data, (size_t)len, 0, (size_t)len);
        bytes_read = 0;
    } else if (bytes_read < (size_t)len) {
        /* Underflow — zero-fill remainder */
        safe_memset(data + bytes_read, (size_t)(len - bytes_read), 0, (size_t)(len - bytes_read));
        ESP_LOGW(TAG, "Audio buffer underrun (%zu/%d bytes)", bytes_read, (int)len);
    }

    /* Update streaming statistics */
    s_streaming_info.bytes_sent += len;
    s_streaming_info.packets_sent++;
    
    /* Calculate streaming duration */
    uint32_t current_time = get_current_time_ms();
    if (s_stream_start_time > 0) {
        s_streaming_info.stream_duration = current_time - s_stream_start_time;
    }
    
    return len;
}

/*
 * Audio task for processing audio input
 */
/* bt_streaming_manager no longer runs its own I2S capture task. The
 * audio capture and buffering are performed by the audio_processor
 * component; bt_streaming_manager consumes audio via
 * audio_processor_read() in the A2DP data callback above. */

/*
 * Update streaming state and streaming info
 */
static void update_streaming_state(bt_streaming_state_t new_state)
{
    ESP_LOGI(TAG, "Streaming state changing: %d -> %d", s_streaming_state, new_state);
    s_streaming_state = new_state;
    s_streaming_info.state = new_state;
    
    /* Handle state transitions */
    switch (new_state) {
        case BT_STREAMING_STATE_STARTING:
            /* Reset statistics */
            s_streaming_info.bytes_sent = 0;
            s_streaming_info.packets_sent = 0;
            s_streaming_info.packet_errors = 0;
            s_streaming_info.stream_duration = 0;
            s_stream_start_time = get_current_time_ms();
            s_streaming_info.paused = false;
            break;
            
        case BT_STREAMING_STATE_STREAMING:
            if (s_stream_start_time == 0) {
                s_stream_start_time = get_current_time_ms();
            }
            s_streaming_info.paused = false;
            break;
            
        case BT_STREAMING_STATE_PAUSED:
            s_streaming_info.paused = true;
            break;
            
        case BT_STREAMING_STATE_STOPPED:
            s_stream_start_time = 0;
            s_streaming_info.paused = false;
            break;
            
        case BT_STREAMING_STATE_ERROR:
            ESP_LOGE(TAG, "Streaming error occurred");
            break;
            
        default:
            break;
    }
    
    /* Notify application via callback */
    if (s_stream_callback) {
        s_stream_callback(s_streaming_state == BT_STREAMING_STATE_STREAMING, 
                         &s_streaming_info, s_stream_callback_data);
    }
}

/*
 * Get current time in milliseconds
 */
static uint32_t get_current_time_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

/**
 * Public functions
 */

esp_err_t bt_streaming_start(void)
{
    /* Check if already streaming */
    if (s_streaming_state == BT_STREAMING_STATE_STREAMING) {
        ESP_LOGW(TAG, "Already streaming");
        return ESP_OK;
    }
    
    /* Check if connected */
    if (bt_get_connection_state() != 1) {
        ESP_LOGE(TAG, "Cannot start streaming - not connected");
        return ESP_FAIL;
    }
    
    /* Start streaming */
    update_streaming_state(BT_STREAMING_STATE_STARTING);
    esp_err_t ret = esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start streaming: %d", ret);
        update_streaming_state(BT_STREAMING_STATE_ERROR);
        return ESP_FAIL;
    }
    
    /* State will be updated to STREAMING when the A2DP callback is invoked */
    return ESP_OK;
}

esp_err_t bt_streaming_stop(void)
{
    /* Check if already stopped */
    if (s_streaming_state == BT_STREAMING_STATE_STOPPED) {
        ESP_LOGW(TAG, "Already stopped");
        return ESP_OK;
    }
    
    /* Stop streaming */
    update_streaming_state(BT_STREAMING_STATE_STOPPING);
    esp_err_t ret = esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_STOP);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop streaming: %d", ret);
        return ESP_FAIL;
    }
    
    /* State will be updated to STOPPED when the A2DP callback is invoked */
    return ESP_OK;
}

esp_err_t bt_streaming_pause(void)
{
    /* Check if already paused */
    if (s_streaming_state == BT_STREAMING_STATE_PAUSED) {
        ESP_LOGW(TAG, "Already paused");
        return ESP_OK;
    }
    
    /* Check if streaming */
    if (s_streaming_state != BT_STREAMING_STATE_STREAMING) {
        ESP_LOGE(TAG, "Cannot pause - not streaming");
        return ESP_FAIL;
    }
    
    /* Pause streaming */
    update_streaming_state(BT_STREAMING_STATE_PAUSED);
    esp_err_t ret = esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_SUSPEND);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to pause streaming: %d", ret);
        /* Revert state if API call failed */
        update_streaming_state(BT_STREAMING_STATE_STREAMING);
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

esp_err_t bt_streaming_resume(void)
{
    /* Check if not paused */
    if (s_streaming_state != BT_STREAMING_STATE_PAUSED) {
        ESP_LOGE(TAG, "Cannot resume - not paused");
        return ESP_FAIL;
    }
    
    /* Resume streaming */
    esp_err_t ret = esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to resume streaming: %d", ret);
        return ESP_FAIL;
    }
    
    /* Update state immediately to avoid delay */
    update_streaming_state(BT_STREAMING_STATE_STREAMING);
    return ESP_OK;
}

esp_err_t bt_get_streaming_info(bt_streaming_info_t* info)
{
    if (info == NULL) {
        return ESP_FAIL;
    }
    
    safe_memcpy(info, sizeof(*info), &s_streaming_info, sizeof(bt_streaming_info_t));
    return ESP_OK;
}

void bt_streaming_manager_init(void)
{
    /* Initialize streaming state */
    s_streaming_state = BT_STREAMING_STATE_STOPPED;
    safe_memset(&s_streaming_info, sizeof(bt_streaming_info_t), 0, sizeof(bt_streaming_info_t));
    s_streaming_info.state = BT_STREAMING_STATE_STOPPED;
    /* The audio_processor is responsible for I2S capture and buffering.
     * Ensure it is initialized elsewhere (for example: `main.c` auto-starts
     * the audio processor at boot). We do not recreate buffers or capture
     * tasks here to avoid duplicate I2S access. */
    
    /* Register data callback */
    esp_a2d_source_register_data_callback(bt_audio_data_callback);
    
    ESP_LOGI(TAG, "Streaming manager initialized");
}

#ifdef UNIT_TEST
void bt_streaming_manager_reset_state_for_test(void)
{
    s_stream_start_time = 0;
    safe_memset(&s_streaming_info, sizeof(s_streaming_info), 0, sizeof(s_streaming_info));
    s_streaming_state = BT_STREAMING_STATE_STOPPED;
    s_streaming_info.state = BT_STREAMING_STATE_STOPPED;
    s_streaming_info.paused = false;
    s_stream_callback = NULL;
    s_stream_callback_data = NULL;
}

void bt_streaming_manager_force_state_for_test(bt_streaming_state_t state)
{
    s_streaming_state = state;
    s_streaming_info.state = state;
    s_streaming_info.paused = (state == BT_STREAMING_STATE_PAUSED);
}

void bt_streaming_manager_set_callback_for_test(bt_stream_callback_t cb, void *user_data)
{
    s_stream_callback = cb;
    s_stream_callback_data = user_data;
}
#endif
