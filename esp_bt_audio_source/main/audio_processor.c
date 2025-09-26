#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "esp_log.h"
#include "driver/i2s_std.h"  // Use the current I2S driver
#include "driver/gpio.h"
#include "esp_timer.h"
#include "audio_processor.h"
#include "nvs_storage.h"

static const char *TAG = "AUDIO_PROC";

// Audio buffer sizes
#define AUDIO_BUFFER_SIZE            (48000 * 4 * 2) // Max 1 second of stereo 32-bit audio at 48kHz
#define AUDIO_PROCESSING_STACK_SIZE  4096
#define AUDIO_BLOCK_SIZE             512  // Process audio in blocks of 512 samples

// Audio processing task handle
static TaskHandle_t s_audio_task_handle = NULL;

// Ring buffer for audio data
static RingbufHandle_t s_audio_buffer = NULL;

// Audio configuration
static audio_config_t s_audio_config = {
    .sample_rate = AUDIO_SAMPLE_RATE_44K,
    .bit_depth = AUDIO_BIT_DEPTH_16,
    .channels = AUDIO_CHANNEL_STEREO,
    .volume = 80,  // Default volume 80%
    .mute = false,
    .i2s_port = I2S_NUM_0,
    .i2s_bclk_pin = GPIO_NUM_26,
    .i2s_ws_pin = GPIO_NUM_25,
    .i2s_din_pin = GPIO_NUM_22,
    .i2s_dout_pin = GPIO_NUM_NC,
};

// Audio statistics
static audio_stats_t s_audio_stats = {0};

// I2S configuration
static i2s_chan_handle_t s_i2s_rx_handle = NULL;

// Processing state
static bool s_is_initialized = false;
static bool s_is_running = false;
static uint8_t s_volume_gain = 80; // Internal volume as percentage

// Forward declarations of internal functions
static void audio_processing_task(void *pvParameters);
static esp_err_t configure_i2s(const audio_config_t* config);
static void apply_volume(void* buffer, size_t size, uint8_t volume);
static esp_err_t convert_audio_format(void* src, void* dst, size_t src_size, 
                                      audio_bit_depth_t src_bit_depth, audio_bit_depth_t dst_bit_depth,
                                      size_t* dst_size);
static esp_err_t resample_audio(void* src, void* dst, size_t src_size, 
                               audio_sample_rate_t src_rate, audio_sample_rate_t dst_rate,
                               size_t* dst_size);

/**
 * @brief Initialize the audio processor
 */
esp_err_t audio_processor_init(const audio_config_t* config)
{
    if (s_is_initialized) {
        ESP_LOGW(TAG, "Audio processor already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (config == NULL) {
        ESP_LOGE(TAG, "Null config provided");
        return ESP_ERR_INVALID_ARG;
    }

    // Copy configuration
    memcpy(&s_audio_config, config, sizeof(audio_config_t));

    // Create audio buffer
    s_audio_buffer = xRingbufferCreate(AUDIO_BUFFER_SIZE, RINGBUF_TYPE_BYTEBUF);
    if (s_audio_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to create audio buffer");
        return ESP_ERR_NO_MEM;
    }

    // Configure I2S
    esp_err_t ret = configure_i2s(config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure I2S: %d", ret);
        vRingbufferDelete(s_audio_buffer);
        s_audio_buffer = NULL;
        return ret;
    }

    // Initialize statistics
    memset(&s_audio_stats, 0, sizeof(audio_stats_t));

    // Initialize NVS storage helper (best effort)
    nvs_storage_init();

    // Create audio processing task
    BaseType_t task_ret = xTaskCreate(audio_processing_task, "audio_proc", 
                                     AUDIO_PROCESSING_STACK_SIZE, NULL, 5, &s_audio_task_handle);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create audio processing task");
        vRingbufferDelete(s_audio_buffer);
        s_audio_buffer = NULL;
        i2s_del_channel(s_i2s_rx_handle);
        return ESP_FAIL;
    }

    s_is_initialized = true;
    s_volume_gain = config->volume;

    ESP_LOGI(TAG, "Audio processor initialized: %dHz, %d-bit, %d channels", 
             config->sample_rate, config->bit_depth, config->channels);

    return ESP_OK;
}

/**
 * @brief Deinitialize the audio processor
 */
esp_err_t audio_processor_deinit(void)
{
    if (!s_is_initialized) {
        return ESP_OK; // Already deinitialized
    }

    // Stop processing first
    if (s_is_running) {
        esp_err_t ret = audio_processor_stop();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to stop audio processor: %d", ret);
            return ret;
        }
    }

    // Delete processing task
    if (s_audio_task_handle != NULL) {
        vTaskDelete(s_audio_task_handle);
        s_audio_task_handle = NULL;
    }

    // Delete audio buffer
    if (s_audio_buffer != NULL) {
        vRingbufferDelete(s_audio_buffer);
        s_audio_buffer = NULL;
    }

    // Delete I2S driver
    if (s_i2s_rx_handle != NULL) {
        i2s_channel_disable(s_i2s_rx_handle);
        i2s_del_channel(s_i2s_rx_handle);
        s_i2s_rx_handle = NULL;
    }

    s_is_initialized = false;
    ESP_LOGI(TAG, "Audio processor deinitialized");

    return ESP_OK;
}

/**
 * @brief Start audio processing
 */
esp_err_t audio_processor_start(void)
{
    if (!s_is_initialized) {
        ESP_LOGE(TAG, "Audio processor not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_is_running) {
        ESP_LOGW(TAG, "Audio processor already running");
        return ESP_OK;
    }

    // Enable I2S RX
    esp_err_t ret = i2s_channel_enable(s_i2s_rx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable I2S RX: %d", ret);
        return ret;
    }

    s_is_running = true;
    ESP_LOGI(TAG, "Audio processor started");

    return ESP_OK;
}

/**
 * @brief Stop audio processing
 */
esp_err_t audio_processor_stop(void)
{
    if (!s_is_initialized) {
        ESP_LOGE(TAG, "Audio processor not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_is_running) {
        ESP_LOGW(TAG, "Audio processor already stopped");
        return ESP_OK;
    }

    // Disable I2S RX
    esp_err_t ret = i2s_channel_disable(s_i2s_rx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to disable I2S RX: %d", ret);
        return ret;
    }

    s_is_running = false;
    ESP_LOGI(TAG, "Audio processor stopped");

    return ESP_OK;
}

/**
 * @brief Set the output sample rate
 */
esp_err_t audio_processor_set_sample_rate(audio_sample_rate_t sample_rate)
{
    if (!s_is_initialized) {
        ESP_LOGE(TAG, "Audio processor not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Check if we need to change anything
    if (s_audio_config.sample_rate == sample_rate) {
        return ESP_OK; // Nothing to do
    }

    bool was_running = s_is_running;
    
    // Stop processing if needed
    if (was_running) {
        esp_err_t ret = audio_processor_stop();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to stop audio processor: %d", ret);
            return ret;
        }
    }

    // Update configuration
    s_audio_config.sample_rate = sample_rate;

    // Reconfigure I2S
    esp_err_t ret = configure_i2s(&s_audio_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reconfigure I2S: %d", ret);
        return ret;
    }

    // Restart if needed
    if (was_running) {
        ret = audio_processor_start();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to restart audio processor: %d", ret);
            return ret;
        }
    }

    ESP_LOGI(TAG, "Audio sample rate changed to %d Hz", sample_rate);
    return ESP_OK;
}

/**
 * @brief Set audio volume
 */
esp_err_t audio_processor_set_volume(uint8_t volume)
{
    if (!s_is_initialized) {
        ESP_LOGE(TAG, "Audio processor not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Clamp volume to 0-100
    if (volume > 100) {
        volume = 100;
    }

    s_volume_gain = volume;
    s_audio_config.volume = volume;

    // Persist new volume
    nvs_storage_set_volume(s_volume_gain);

    ESP_LOGI(TAG, "Audio volume set to %d%%", volume);
    return ESP_OK;
}

/**
 * @brief Mute or unmute audio
 */
esp_err_t audio_processor_set_mute(bool mute)
{
    if (!s_is_initialized) {
        ESP_LOGE(TAG, "Audio processor not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    s_audio_config.mute = mute;
    ESP_LOGI(TAG, "Audio %s", mute ? "muted" : "unmuted");

    return ESP_OK;
}

/**
 * @brief Get current audio configuration
 */
esp_err_t audio_processor_get_config(audio_config_t* config)
{
    if (!s_is_initialized) {
        ESP_LOGE(TAG, "Audio processor not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (config == NULL) {
        ESP_LOGE(TAG, "Null config pointer");
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(config, &s_audio_config, sizeof(audio_config_t));
    return ESP_OK;
}

/**
 * @brief Get audio processing statistics
 */
esp_err_t audio_processor_get_stats(audio_stats_t* stats)
{
    if (!s_is_initialized) {
        ESP_LOGE(TAG, "Audio processor not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (stats == NULL) {
        ESP_LOGE(TAG, "Null stats pointer");
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(stats, &s_audio_stats, sizeof(audio_stats_t));
    return ESP_OK;
}

/**
 * @brief Read processed audio data
 */
esp_err_t audio_processor_read(uint8_t* buffer, size_t size, size_t* bytes_read)
{
    if (!s_is_initialized) {
        ESP_LOGE(TAG, "Audio processor not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (buffer == NULL || bytes_read == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }

    // If muted, just fill with zeros
    if (s_audio_config.mute) {
        memset(buffer, 0, size);
        *bytes_read = size;
        return ESP_OK;
    }

    // Read from ring buffer
    size_t read_size = 0;
    void* item = xRingbufferReceiveUpTo(s_audio_buffer, &read_size, size, 0);
    
    if (item == NULL) {
        // Buffer is empty
        *bytes_read = 0;
        s_audio_stats.buffer_underruns++;
        return ESP_OK;
    }

    // Copy data to output buffer
    memcpy(buffer, item, read_size);
    vRingbufferReturnItem(s_audio_buffer, item);

    // Apply volume if not at maximum
    if (s_volume_gain < 100) {
        apply_volume(buffer, read_size, s_volume_gain);
    }

    *bytes_read = read_size;

    // Update statistics
    s_audio_stats.current_buffer_level = xRingbufferGetCurFreeSize(s_audio_buffer);
    if (s_audio_stats.current_buffer_level > s_audio_stats.peak_buffer_level) {
        s_audio_stats.peak_buffer_level = s_audio_stats.current_buffer_level;
    }

    return ESP_OK;
}

/*******************************
 * Internal functions
 *******************************/

/**
 * @brief Configure I2S driver using modern API
 */
static esp_err_t configure_i2s(const audio_config_t* config)
{
    // If already configured, delete channel first
    if (s_i2s_rx_handle != NULL) {
        i2s_channel_disable(s_i2s_rx_handle);
        i2s_del_channel(s_i2s_rx_handle);
        s_i2s_rx_handle = NULL;
    }

    // Configure I2S channel with modern API
    i2s_chan_config_t chan_cfg = {
        .id = config->i2s_port,
        /* Default to SLAVE: this device is typically the I2S consumer
         * and the audio producer supplies BCLK/WS. Change to MASTER if
         * you want this device to generate clocks. */
        .role = I2S_ROLE_SLAVE,
        /* Keep DMA descriptor counts modest to reduce RAM usage while
         * avoiding underruns. These values are reasonable defaults for
         * A2DP use-cases; tweak if you see buffer underruns/overruns. */
        .dma_desc_num = 4,
        .dma_frame_num = 64,
        .auto_clear = true,
    };
    
    // Create RX channel
    ESP_LOGI(TAG, "Creating I2S RX channel");
    esp_err_t ret = i2s_new_channel(&chan_cfg, NULL, &s_i2s_rx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2S channel: %d", ret);
        return ret;
    }

    // Convert bit depth to I2S format
    i2s_data_bit_width_t bit_width;
    switch (config->bit_depth) {
        case AUDIO_BIT_DEPTH_16:
            bit_width = I2S_DATA_BIT_WIDTH_16BIT;
            break;
        case AUDIO_BIT_DEPTH_24:
            bit_width = I2S_DATA_BIT_WIDTH_24BIT;
            break;
        case AUDIO_BIT_DEPTH_32:
            bit_width = I2S_DATA_BIT_WIDTH_32BIT;
            break;
        default:
            bit_width = I2S_DATA_BIT_WIDTH_16BIT;
            break;
    }

    // Configure RX channel in standard mode
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(config->sample_rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            bit_width,
            config->channels == AUDIO_CHANNEL_MONO ? I2S_SLOT_MODE_MONO : I2S_SLOT_MODE_STEREO
        ),
        .gpio_cfg = {
            /* Don't request MCLK output by default; many codecs don't need it
             * when the producer is the I2S master. Set to GPIO_NUM_NC if unused. */
            .mclk = GPIO_NUM_NC,
            .bclk = config->i2s_bclk_pin,
            .ws = config->i2s_ws_pin,
            .din = config->i2s_din_pin,
            .dout = config->i2s_dout_pin,    // May be NC when RX-only
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    
    ret = i2s_channel_init_std_mode(s_i2s_rx_handle, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure I2S channel: %d", ret);
        i2s_del_channel(s_i2s_rx_handle);
        s_i2s_rx_handle = NULL;
        return ret;
    }

    return ESP_OK;
}

esp_err_t audio_processor_get_status(audio_status_t* status)
{
    if (status == NULL) return ESP_ERR_INVALID_ARG;
    status->initialized = s_is_initialized;
    status->running = s_is_running;
    status->volume = s_volume_gain;
    status->mute = s_audio_config.mute;
    status->sample_rate = s_audio_config.sample_rate;
    status->bit_depth = s_audio_config.bit_depth;
    status->channels = s_audio_config.channels;
    return ESP_OK;
}

esp_err_t audio_processor_set_i2s_pins(int bclk_pin, int ws_pin, int din_pin, int dout_pin)
{
    if (!s_is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    bool was_running = s_is_running;
    if (was_running) {
        esp_err_t ret = audio_processor_stop();
        if (ret != ESP_OK) return ret;
    }

    s_audio_config.i2s_bclk_pin = bclk_pin;
    s_audio_config.i2s_ws_pin = ws_pin;
    s_audio_config.i2s_din_pin = din_pin;
    s_audio_config.i2s_dout_pin = dout_pin;

    // Persist pins
    nvs_storage_set_i2s_pins(bclk_pin, ws_pin, din_pin, dout_pin);

    esp_err_t ret = configure_i2s(&s_audio_config);

    if (was_running && ret == ESP_OK) {
        ret = audio_processor_start();
    }

    return ret;
}

/**
 * @brief Audio processing task
 */
static void audio_processing_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Audio processing task started");

    // Buffer for raw I2S input
    uint8_t i2s_buffer[AUDIO_BLOCK_SIZE * 8]; // Larger buffer to accommodate different bit depths
    // Buffers for processed audio (two buffers to avoid overlap during conversion/resampling)
    uint8_t proc_buffer[AUDIO_BLOCK_SIZE * 8];
    uint8_t proc_buffer2[AUDIO_BLOCK_SIZE * 8];

    // Task timing measurements for CPU load calculation
    int64_t task_start_time, task_end_time;
    int64_t total_time = 0, processing_time = 0;
    const int LOAD_CALC_INTERVALS = 50; // Calculate load every N iterations
    int interval_counter = 0;

    size_t bytes_read, conv_size;
    
    while (1) {
        // If not running, just sleep
        if (!s_is_running || !s_is_initialized) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        task_start_time = esp_timer_get_time();

        // Read audio from I2S (with a modest timeout)
        esp_err_t ret = i2s_channel_read(s_i2s_rx_handle, i2s_buffer, sizeof(i2s_buffer), 
                                         &bytes_read, 50 / portTICK_PERIOD_MS);

        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "I2S read failed: %d", ret);
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        if (bytes_read == 0) {
            // No data available yet
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        // Count the samples
    int bytes_per_sample = s_audio_config.bit_depth / 8;
    if (bytes_per_sample <= 0) bytes_per_sample = 2; // fallback
    int samples_count = bytes_read / (bytes_per_sample * s_audio_config.channels);
        s_audio_stats.samples_processed += samples_count;

        // Process audio (format conversion, resampling, etc.)
        // Use convert_audio_format and resample_audio helpers so they are exercised.
        // First, attempt format conversion (may be a no-op if formats match)
        esp_err_t cret = convert_audio_format(i2s_buffer, proc_buffer, bytes_read,
                                             s_audio_config.bit_depth, s_audio_config.bit_depth,
                                             &conv_size);
        if (cret != ESP_OK) {
            s_audio_stats.conversion_errors++;
            // Skip this buffer
            continue;
        }

        // Then attempt resampling (may be a no-op if rates match)
        size_t res_size = 0;
        esp_err_t rret = resample_audio(proc_buffer, proc_buffer2, conv_size,
                                        s_audio_config.sample_rate, s_audio_config.sample_rate,
                                        &res_size);
        if (rret != ESP_OK) {
            s_audio_stats.conversion_errors++;
            continue;
        }

        // Final processed buffer is in proc_buffer2 with size res_size
        conv_size = res_size;

        // Check available space in the buffer
        size_t free_size = xRingbufferGetCurFreeSize(s_audio_buffer);
        if (free_size < conv_size) {
            s_audio_stats.buffer_overruns++;
            // Skip this batch if buffer is too full
            if (free_size < (conv_size / 2)) {
                continue;
            }
        }

        // Add processed audio to the buffer
        UBaseType_t buf_ret = xRingbufferSend(s_audio_buffer, proc_buffer2, conv_size, 0);
        if (buf_ret != pdTRUE) {
            s_audio_stats.buffer_overruns++;
        }

        task_end_time = esp_timer_get_time();
        processing_time += (task_end_time - task_start_time);
        total_time += (task_end_time - task_start_time);

        // Calculate CPU load periodically
        if (++interval_counter >= LOAD_CALC_INTERVALS) {
            s_audio_stats.cpu_load = (float)processing_time / total_time;
            processing_time = 0;
            total_time = 0;
            interval_counter = 0;
        }
    }
}

/**
 * @brief Apply volume gain to audio buffer
 */
static void apply_volume(void* buffer, size_t size, uint8_t volume)
{
    if (volume >= 100) {
        return; // No change needed
    }

    if (volume == 0) {
        // Muted
        memset(buffer, 0, size);
        return;
    }

    // Apply volume based on bit depth
    float gain = volume / 100.0f;
    
    if (s_audio_config.bit_depth == AUDIO_BIT_DEPTH_16) {
        int16_t* samples = (int16_t*)buffer;
        int sample_count = size / sizeof(int16_t);
        
        for (int i = 0; i < sample_count; i++) {
            samples[i] = (int16_t)(samples[i] * gain);
        }
    } 
    else if (s_audio_config.bit_depth == AUDIO_BIT_DEPTH_24 ||
             s_audio_config.bit_depth == AUDIO_BIT_DEPTH_32) {
        int32_t* samples = (int32_t*)buffer;
        int sample_count = size / sizeof(int32_t);
        
        for (int i = 0; i < sample_count; i++) {
            samples[i] = (int32_t)(samples[i] * gain);
        }
    }
}

/**
 * @brief Convert audio from one bit depth to another
 */
static esp_err_t convert_audio_format(void* src, void* dst, size_t src_size, 
                                      audio_bit_depth_t src_bit_depth, audio_bit_depth_t dst_bit_depth,
                                      size_t* dst_size)
{
    if (src_bit_depth == dst_bit_depth) {
        // Same format, just copy
        memcpy(dst, src, src_size);
        *dst_size = src_size;
        return ESP_OK;
    }

    // Calculate sample counts
    int src_sample_count = src_size / (src_bit_depth / 8);
    int dst_bytes_per_sample = dst_bit_depth / 8;
    *dst_size = src_sample_count * dst_bytes_per_sample;

    // Handle different conversion scenarios
    if (src_bit_depth == AUDIO_BIT_DEPTH_16 && dst_bit_depth == AUDIO_BIT_DEPTH_32) {
        // 16-bit to 32-bit
        int16_t* src_samples = (int16_t*)src;
        int32_t* dst_samples = (int32_t*)dst;
        
        for (int i = 0; i < src_sample_count; i++) {
            // Scale up with proper bit shift (16 bits to 32 bits)
            dst_samples[i] = ((int32_t)src_samples[i]) << 16;
        }
    }
    else if (src_bit_depth == AUDIO_BIT_DEPTH_32 && dst_bit_depth == AUDIO_BIT_DEPTH_16) {
        // 32-bit to 16-bit
        int32_t* src_samples = (int32_t*)src;
        int16_t* dst_samples = (int16_t*)dst;
        
        for (int i = 0; i < src_sample_count; i++) {
            // Scale down with proper bit shift and dithering
            dst_samples[i] = (int16_t)(src_samples[i] >> 16);
        }
    }
    else if (src_bit_depth == AUDIO_BIT_DEPTH_24 && dst_bit_depth == AUDIO_BIT_DEPTH_16) {
        // 24-bit to 16-bit (assuming 24-bit is stored in 32-bit containers)
        int32_t* src_samples = (int32_t*)src;
        int16_t* dst_samples = (int16_t*)dst;
        
        for (int i = 0; i < src_sample_count; i++) {
            // Scale down with proper bit shift
            dst_samples[i] = (int16_t)(src_samples[i] >> 8);
        }
    }
    else if (src_bit_depth == AUDIO_BIT_DEPTH_16 && dst_bit_depth == AUDIO_BIT_DEPTH_24) {
        // 16-bit to 24-bit (stored in 32-bit containers)
        int16_t* src_samples = (int16_t*)src;
        int32_t* dst_samples = (int32_t*)dst;
        
        for (int i = 0; i < src_sample_count; i++) {
            // Scale up with proper bit shift
            dst_samples[i] = ((int32_t)src_samples[i]) << 8;
        }
    }
    else {
        // Unsupported conversion
        ESP_LOGE(TAG, "Unsupported format conversion: %d to %d", src_bit_depth, dst_bit_depth);
        s_audio_stats.conversion_errors++;
        return ESP_ERR_NOT_SUPPORTED;
    }

    return ESP_OK;
}

/**
 * @brief Simple resampling function
 *
 * Note: This is a basic linear interpolation resampler.
 * For production use, consider a higher quality algorithm like polyphase or FFT-based resampling.
 */
static esp_err_t resample_audio(void* src, void* dst, size_t src_size, 
                               audio_sample_rate_t src_rate, audio_sample_rate_t dst_rate,
                               size_t* dst_size)
{
    if (src_rate == dst_rate) {
        // Same rate, just copy
        memcpy(dst, src, src_size);
        *dst_size = src_size;
        return ESP_OK;
    }

    int channels = s_audio_config.channels;
    int bytes_per_sample = s_audio_config.bit_depth / 8;
    int src_sample_count = src_size / bytes_per_sample;
    int src_frame_count = src_sample_count / channels;

    // Calculate destination size
    double ratio = (double)dst_rate / src_rate;
    int dst_frame_count = (int)(src_frame_count * ratio);
    int dst_sample_count = dst_frame_count * channels;
    *dst_size = dst_sample_count * bytes_per_sample;

    if (s_audio_config.bit_depth == AUDIO_BIT_DEPTH_16) {
        int16_t* src_samples = (int16_t*)src;
        int16_t* dst_samples = (int16_t*)dst;
        
        for (int dstFrame = 0; dstFrame < dst_frame_count; dstFrame++) {
            // Calculate the source frame position (floating point)
            double srcFrameF = dstFrame / ratio;
            int srcFrame = (int)srcFrameF;
            double frac = srcFrameF - srcFrame;
            
            // Handle boundary case
            if (srcFrame >= src_frame_count - 1) {
                srcFrame = src_frame_count - 2;
                frac = 1.0;
            }
            
            // Linear interpolation for each channel
            for (int ch = 0; ch < channels; ch++) {
                int src_idx1 = srcFrame * channels + ch;
                int src_idx2 = (srcFrame + 1) * channels + ch;
                int dst_idx = dstFrame * channels + ch;
                
                // Linear interpolation
                dst_samples[dst_idx] = (int16_t)((1.0 - frac) * src_samples[src_idx1] + 
                                               frac * src_samples[src_idx2]);
            }
        }
    }
    else if (s_audio_config.bit_depth == AUDIO_BIT_DEPTH_32) {
        int32_t* src_samples = (int32_t*)src;
        int32_t* dst_samples = (int32_t*)dst;
        
        for (int dstFrame = 0; dstFrame < dst_frame_count; dstFrame++) {
            // Calculate the source frame position (floating point)
            double srcFrameF = dstFrame / ratio;
            int srcFrame = (int)srcFrameF;
            double frac = srcFrameF - srcFrame;
            
            // Handle boundary case
            if (srcFrame >= src_frame_count - 1) {
                srcFrame = src_frame_count - 2;
                frac = 1.0;
            }
            
            // Linear interpolation for each channel
            for (int ch = 0; ch < channels; ch++) {
                int src_idx1 = srcFrame * channels + ch;
                int src_idx2 = (srcFrame + 1) * channels + ch;
                int dst_idx = dstFrame * channels + ch;
                
                // Linear interpolation
                dst_samples[dst_idx] = (int32_t)((1.0 - frac) * src_samples[src_idx1] + 
                                               frac * src_samples[src_idx2]);
            }
        }
    }
    else {
        // Unsupported bit depth for resampling
        ESP_LOGE(TAG, "Unsupported bit depth for resampling: %d", s_audio_config.bit_depth);
        s_audio_stats.conversion_errors++;
        return ESP_ERR_NOT_SUPPORTED;
    }

    return ESP_OK;
}

/**
 * @brief Set the audio bit depth
 */
esp_err_t audio_processor_set_bit_depth(audio_bit_depth_t bit_depth)
{
    if (!s_is_initialized) {
        ESP_LOGE(TAG, "Audio processor not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Check if we need to change anything
    if (s_audio_config.bit_depth == bit_depth) {
        return ESP_OK; // Nothing to do
    }

    bool was_running = s_is_running;
    
    // Stop processing if needed
    if (was_running) {
        esp_err_t ret = audio_processor_stop();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to stop audio processor: %d", ret);
            return ret;
        }
    }

    // Update configuration
    s_audio_config.bit_depth = bit_depth;

    // Reconfigure I2S
    esp_err_t ret = configure_i2s(&s_audio_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reconfigure I2S: %d", ret);
        return ret;
    }

    // Restart if needed
    if (was_running) {
        ret = audio_processor_start();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to restart audio processor: %d", ret);
            return ret;
        }
    }

    ESP_LOGI(TAG, "Audio bit depth changed to %d bits", bit_depth);
    return ESP_OK;
}
