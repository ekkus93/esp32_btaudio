#include <string.h>
#include <inttypes.h> // Add for PRIu32 macros
#include <math.h>     // Add for sin() and M_PI
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h" // Add for RingbufHandle_t
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "bt_source.h"

static const char *TAG = "BT_STREAM_MGR";

/* Audio stream configuration */
#define BT_AUDIO_SAMPLE_RATE     44100
#define BT_AUDIO_BITS_PER_SAMPLE 16
#define BT_AUDIO_CHANNELS        2
#define BT_AUDIO_FRAME_SIZE      512 // In samples
#define BT_AUDIO_BUFFER_SIZE     (BT_AUDIO_FRAME_SIZE * BT_AUDIO_CHANNELS * (BT_AUDIO_BITS_PER_SAMPLE/8) * 8)

/* State tracking */
static bt_streaming_state_t s_streaming_state = BT_STREAMING_STATE_STOPPED;
static bt_streaming_info_t s_streaming_info = {0};
static TaskHandle_t s_audio_task_handle = NULL;

/* Audio buffer for streaming data */
static RingbufHandle_t s_audio_buffer = NULL;
static uint32_t s_stream_start_time = 0;

/* Callback function */
static bt_stream_callback_t s_stream_callback = NULL;
static void* s_stream_callback_data = NULL;

/* Function declarations */
static int32_t bt_audio_data_callback(uint8_t *data, int32_t len);
static void bt_audio_task(void *pvParameters);
static void update_streaming_state(bt_streaming_state_t new_state);
static uint32_t get_current_time_ms(void);

/*
 * A2DP source data callback - called by A2DP when it needs audio data
 * Changed return type to int32_t to match esp_a2d_source_data_cb_t
 */
static int32_t bt_audio_data_callback(uint8_t *data, int32_t len)
{
    if (s_streaming_state != BT_STREAMING_STATE_STREAMING && 
        s_streaming_state != BT_STREAMING_STATE_PAUSED) {
        /* Not streaming or paused - clear buffer */
        memset(data, 0, len);
        return len;
    }

    if (s_streaming_state == BT_STREAMING_STATE_PAUSED) {
        /* Paused - send silent audio */
        memset(data, 0, len);
        return len;
    }

    /* Read data from the audio buffer */
    size_t bytes_read = 0;
    void *item = xRingbufferReceiveUpTo(s_audio_buffer, &bytes_read, len, 0);
    
    if (item == NULL || bytes_read < len) {
        /* Buffer underrun - fill remaining with silence */
        if (item != NULL) {
            memcpy(data, item, bytes_read);
            memset(data + bytes_read, 0, len - bytes_read);
            vRingbufferReturnItem(s_audio_buffer, item);
        } else {
            memset(data, 0, len);
            bytes_read = 0;
        }
        ESP_LOGW(TAG, "Audio buffer underrun (%u/%u bytes)", 
                (unsigned int)bytes_read, (unsigned int)len);
    } else {
        /* We got all the data we needed */
        memcpy(data, item, len);
        vRingbufferReturnItem(s_audio_buffer, item);
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
static void bt_audio_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Audio task started");

    /* Buffer for audio processing */
    uint8_t audio_frame[BT_AUDIO_FRAME_SIZE * BT_AUDIO_CHANNELS * (BT_AUDIO_BITS_PER_SAMPLE/8)];

    while (1) {
        if (s_streaming_state == BT_STREAMING_STATE_STREAMING) {
            /* TODO: Get audio data from I2S or other source */
            /* This is where real audio acquisition would happen */
            /* For now, we generate a simple sine wave for testing */
            
            static uint32_t phase = 0;
            int16_t *samples = (int16_t *)audio_frame;
            
            for (int i = 0; i < BT_AUDIO_FRAME_SIZE; i++) {
                /* Generate simple sine wave for left and right channels */
                int16_t sample = (sin(phase * 2 * M_PI / 128) * 10000);
                samples[i*2] = sample;        // Left channel
                samples[i*2+1] = sample;      // Right channel
                phase = (phase + 1) % 128;    // Update phase
            }

            /* Send audio data to the buffer */
            UBaseType_t res = xRingbufferSend(s_audio_buffer, audio_frame, sizeof(audio_frame), pdMS_TO_TICKS(10));
            if (res != pdTRUE) {
                ESP_LOGW(TAG, "Failed to send audio data to buffer");
            }
        }
        
        /* Sleep to maintain frame rate */
        vTaskDelay(pdMS_TO_TICKS(10)); // ~100Hz frame rate
    }
}

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
    
    memcpy(info, &s_streaming_info, sizeof(bt_streaming_info_t));
    return ESP_OK;
}

void bt_streaming_manager_init(void)
{
    /* Initialize streaming state */
    s_streaming_state = BT_STREAMING_STATE_STOPPED;
    memset(&s_streaming_info, 0, sizeof(bt_streaming_info_t));
    s_streaming_info.state = BT_STREAMING_STATE_STOPPED;
    
    /* Create audio buffer */
    s_audio_buffer = xRingbufferCreate(BT_AUDIO_BUFFER_SIZE, RINGBUF_TYPE_BYTEBUF);
    if (s_audio_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to create audio buffer");
        return;
    }
    
    /* Create audio processing task */
    BaseType_t ret = xTaskCreate(bt_audio_task, "bt_audio_task", 4096, NULL, 5, &s_audio_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create audio task: %d", ret);
        return;
    }
    
    /* Register data callback */
    esp_a2d_source_register_data_callback(bt_audio_data_callback);
    
    ESP_LOGI(TAG, "Streaming manager initialized");
}
