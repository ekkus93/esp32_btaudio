#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stddef.h>
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
#ifdef CONFIG_BT_MOCK_TESTING
#define AUDIO_RESAMPLE_MAX_RATIO     6    // Cover worst-case 8 kHz -> 48 kHz upsampling in tests
#else
#define AUDIO_RESAMPLE_MAX_RATIO     12   // Cover 8 kHz -> 96 kHz upsampling in production paths
#endif
#define AUDIO_WORK_BUFFER_BYTES      (AUDIO_BLOCK_SIZE * 8 * AUDIO_RESAMPLE_MAX_RATIO)

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
// Beep override state: number of bytes enqueued as beep that should bypass mute
size_t s_beep_remaining_bytes = 0;

// Diagnostics throttling state for high-frequency logging inside the
// audio processing task. We emit the first log immediately and then
// rate-limit updates to avoid starving the idle task and tripping the watchdog.
static TickType_t s_diag_next_log_tick = 0;
static size_t s_diag_last_conv_size = SIZE_MAX;
static size_t s_diag_last_frame_bytes = SIZE_MAX;
static int s_diag_last_src_rate = -1;
static int s_diag_last_dst_rate = -1;

static inline void audio_proc_mock_yield(void)
{
#ifdef CONFIG_BT_MOCK_TESTING
    vTaskDelay(1);
#endif
}

// Audio processing scratch buffers (kept off task stack to avoid overflow)
static uint8_t s_i2s_buffer[AUDIO_WORK_BUFFER_BYTES];
static uint8_t s_proc_buffer[AUDIO_WORK_BUFFER_BYTES];
static uint8_t s_proc_buffer2[AUDIO_WORK_BUFFER_BYTES];

static inline int audio_bytes_per_sample(audio_bit_depth_t bit_depth)
{
    switch (bit_depth) {
        case AUDIO_BIT_DEPTH_24:
        case AUDIO_BIT_DEPTH_32:
            return 4;
        case AUDIO_BIT_DEPTH_16:
        default:
            return 2;
    }
}

static size_t audio_calculate_buffer_capacity(const audio_config_t* config)
{
    int bytes_per_sample = audio_bytes_per_sample(config->bit_depth);
    if (bytes_per_sample <= 0) {
        bytes_per_sample = 2;
    }

    int channels = (config->channels == AUDIO_CHANNEL_MONO) ? 1 : 2;
    size_t frame_bytes = (size_t)bytes_per_sample * (size_t)channels;

#ifdef CONFIG_BT_MOCK_TESTING
    size_t block_bytes = (size_t)AUDIO_BLOCK_SIZE * frame_bytes;
    if (block_bytes == 0) {
        block_bytes = 4096;
    }
    /* Keep the mock buffer modest to fit within the Unity test app's RAM
     * budget while still holding several processing blocks. */
    size_t capacity = block_bytes * 8U;
#else
    (void)config;
    size_t capacity = AUDIO_BUFFER_SIZE;
#endif

    if (capacity == 0) {
        capacity = frame_bytes * AUDIO_BLOCK_SIZE;
    }

    /* Ringbuffer expects 4-byte aligned sizes. */
    capacity = (capacity + 3U) & ~((size_t)3U);
    return capacity;
}

#ifdef CONFIG_BT_MOCK_TESTING
typedef struct {
    bool enabled;
    uint32_t frame_counter;
} mock_i2s_state_t;

static mock_i2s_state_t s_mock_i2s_state = {0};

static size_t mock_generate_i2s_audio(uint8_t* buffer, size_t buffer_size);
#endif

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

#ifdef CONFIG_BT_MOCK_TESTING
static size_t mock_generate_i2s_audio(uint8_t* buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0) {
        return 0;
    }

    int bytes_per_sample = audio_bytes_per_sample(s_audio_config.bit_depth);
    int channels = s_audio_config.channels;
    if (channels <= 0) {
        channels = AUDIO_CHANNEL_STEREO;
    }

    size_t frame_bytes = (size_t)bytes_per_sample * (size_t)channels;
    if (frame_bytes == 0) {
        return 0;
    }

    size_t max_frames = buffer_size / frame_bytes;
    if (max_frames == 0) {
        return 0;
    }

    size_t frames_to_write = AUDIO_BLOCK_SIZE;
    if (frames_to_write > max_frames) {
        frames_to_write = max_frames;
    }

    size_t bytes_to_write = frames_to_write * frame_bytes;

    /* Fill with a simple deterministic ramp so downstream checks can
     * observe non-zero data without relying on real hardware. */
    uint8_t seed = (uint8_t)(s_mock_i2s_state.frame_counter & 0xFF);
    for (size_t i = 0; i < bytes_to_write; ++i) {
        buffer[i] = (uint8_t)(seed + i);
    }

    s_mock_i2s_state.frame_counter += frames_to_write;
    return bytes_to_write;
}
#endif

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

    size_t buffer_capacity = audio_calculate_buffer_capacity(config);

    // Create audio buffer
    s_audio_buffer = xRingbufferCreate(buffer_capacity, RINGBUF_TYPE_BYTEBUF);
    if (s_audio_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to create audio buffer (%zu bytes)", buffer_capacity);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Audio buffer created (%zu bytes)", buffer_capacity);

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

    /* Reset diagnostic throttling state for a fresh session. */
    s_diag_next_log_tick = 0;
    s_diag_last_conv_size = SIZE_MAX;
    s_diag_last_frame_bytes = SIZE_MAX;
    s_diag_last_src_rate = -1;
    s_diag_last_dst_rate = -1;

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
#ifdef CONFIG_BT_MOCK_TESTING
        s_i2s_rx_handle = NULL;
        s_mock_i2s_state.enabled = false;
#else
        i2s_channel_disable(s_i2s_rx_handle);
        i2s_del_channel(s_i2s_rx_handle);
        s_i2s_rx_handle = NULL;
#endif
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
#ifdef CONFIG_BT_MOCK_TESTING
    s_mock_i2s_state.enabled = true;
#else
    esp_err_t ret = i2s_channel_enable(s_i2s_rx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable I2S RX: %d", ret);
        return ret;
    }
#endif

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
#ifdef CONFIG_BT_MOCK_TESTING
    s_mock_i2s_state.enabled = false;
#else
    esp_err_t ret = i2s_channel_disable(s_i2s_rx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to disable I2S RX: %d", ret);
        return ret;
    }
#endif

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

    // If muted and no beep override is pending, just fill with zeros
    extern size_t s_beep_remaining_bytes; /* declared below */
    if (s_audio_config.mute && s_beep_remaining_bytes == 0) {
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

    /* If part of this data was scheduled as a beep, decrement remaining
     * counter so subsequent reads know when to re-enable mute behavior. */
    if (s_beep_remaining_bytes > 0) {
        if (read_size >= s_beep_remaining_bytes) s_beep_remaining_bytes = 0;
        else s_beep_remaining_bytes -= read_size;
    }

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
#ifdef CONFIG_BT_MOCK_TESTING
    (void)config;
    s_mock_i2s_state.enabled = false;
    s_mock_i2s_state.frame_counter = 0;
    s_i2s_rx_handle = (i2s_chan_handle_t)&s_mock_i2s_state;
    return ESP_OK;
#else
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
#endif
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
 * @brief Enqueue a short beep tone into the audio pipeline.
 *
 * This generates a simple square-wave beep at 1kHz and pushes the PCM
 * data into the audio ring buffer so it will be played even when the
 * normal I2S source is silent/muted. The function increments
 * s_beep_remaining_bytes so reads can track and bypass mute for the
 * duration of the beep.
 */
esp_err_t audio_processor_beep(uint32_t duration_ms)
{
    if (!s_is_initialized) {
        ESP_LOGW(TAG, "audio_processor_beep: not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (duration_ms == 0) return ESP_OK;

    int sample_rate = s_audio_config.sample_rate;
    int bytes_per_sample = audio_bytes_per_sample(s_audio_config.bit_depth);
    if (bytes_per_sample <= 0) bytes_per_sample = 2;
    int channels = (s_audio_config.channels == AUDIO_CHANNEL_MONO) ? 1 : 2;
    size_t frame_bytes = (size_t)bytes_per_sample * (size_t)channels;

    size_t total_frames = ((size_t)sample_rate * (size_t)duration_ms) / 1000U;
    if (total_frames == 0) return ESP_OK;

    size_t total_bytes = total_frames * frame_bytes;

    /* Cap per-chunk generation to our work buffer size to avoid large
     * stack or temporary allocations. AUDIO_WORK_BUFFER_BYTES is defined
     * above and represents a safe chunk size. */
    size_t max_chunk = (size_t)AUDIO_WORK_BUFFER_BYTES;
    size_t bytes_remaining = total_bytes;

    /* Use a simple square wave at 1kHz. */
    const int tone_hz = 1000;
    size_t samples_per_period = (size_t)sample_rate / (size_t)tone_hz;
    if (samples_per_period == 0) samples_per_period = 1;

    while (bytes_remaining > 0) {
        size_t chunk = bytes_remaining > max_chunk ? max_chunk : bytes_remaining;
        size_t frames = chunk / frame_bytes;

        /* Fill the proc buffer with the tone samples. Support 16-bit and
         * 32-bit containers (24-bit stored in 32-bit). Other depths fall
         * back to a 16-bit-like representation. */
        if (s_audio_config.bit_depth == AUDIO_BIT_DEPTH_16) {
            int16_t* out = (int16_t*)s_proc_buffer;
            for (size_t f = 0; f < frames; ++f) {
                int16_t sample = ((f % samples_per_period) < (samples_per_period/2)) ? 30000 : -30000;
                for (int ch = 0; ch < channels; ++ch) {
                    *out++ = sample;
                }
            }
        } else {
            /* 32-bit container (32- or 24-bit) */
            int32_t* out32 = (int32_t*)s_proc_buffer;
            for (size_t f = 0; f < frames; ++f) {
                int32_t sample = ((f % samples_per_period) < (samples_per_period/2)) ? 0x3FFFFFFF : -0x3FFFFFFF;
                for (int ch = 0; ch < channels; ++ch) {
                    *out32++ = sample;
                }
            }
        }

        /* Try to push into the ring buffer; if it fails we bail but still
         * mark the remaining bytes so reads behave consistently. */
        BaseType_t sent = xRingbufferSend(s_audio_buffer, s_proc_buffer, (size_t)frames * frame_bytes, 0);
        if (sent != pdTRUE) {
            ESP_LOGW(TAG, "audio_processor_beep: ringbuffer full, queued %zu/%zu bytes", total_bytes - bytes_remaining, total_bytes);
            break;
        }

        bytes_remaining -= (size_t)frames * frame_bytes;
    }

    /* Mark remaining bytes so reads will bypass mute until beep data is
     * consumed. Note that if the ringbuffer didn't accept the whole beep
     * we still set the counter for the portion that was queued. */
    size_t queued = total_bytes - bytes_remaining;
    s_beep_remaining_bytes += queued;

    ESP_LOGI(TAG, "audio_processor_beep: queued %zu bytes (%u ms)", queued, (unsigned)duration_ms);
    return ESP_OK;
}

/**
 * @brief Audio processing task
 */
static void audio_processing_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Audio processing task started");

    // Task timing measurements for CPU load calculation
    int64_t task_start_time, task_end_time;
    int64_t total_time = 0, processing_time = 0;
    const int LOAD_CALC_INTERVALS = 50; // Calculate load every N iterations
    int interval_counter = 0;

    size_t bytes_read, conv_size;
    static int s_debug_log_count = 0;
    
    while (1) {
        // If not running, just sleep
        if (!s_is_running || !s_is_initialized) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        task_start_time = esp_timer_get_time();

        // Read audio from I2S (with a modest timeout)
#ifdef CONFIG_BT_MOCK_TESTING
    bytes_read = mock_generate_i2s_audio(s_i2s_buffer, sizeof(s_i2s_buffer));
        esp_err_t ret = (bytes_read > 0) ? ESP_OK : ESP_ERR_TIMEOUT;
#else
    esp_err_t ret = i2s_channel_read(s_i2s_rx_handle, s_i2s_buffer, sizeof(s_i2s_buffer),
                                         &bytes_read, 50 / portTICK_PERIOD_MS);
#endif

        if (ret != ESP_OK) {
#ifdef CONFIG_BT_MOCK_TESTING
            vTaskDelay(pdMS_TO_TICKS(5));
#else
            ESP_LOGW(TAG, "I2S read failed: %d", ret);
            vTaskDelay(pdMS_TO_TICKS(10));
#endif
            continue;
        }

        if (bytes_read == 0) {
            // No data available yet
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        // Count the samples
        int bytes_per_sample = audio_bytes_per_sample(s_audio_config.bit_depth);
        if (bytes_per_sample <= 0) {
            bytes_per_sample = 2;
        }
        int channels = s_audio_config.channels;
        if (channels <= 0) {
            channels = AUDIO_CHANNEL_STEREO;
        }
        int samples_count = bytes_read / (bytes_per_sample * channels);
        s_audio_stats.samples_processed += samples_count;

        // Process audio (format conversion, resampling, etc.)
        // Use convert_audio_format and resample_audio helpers so they are exercised.
        // First, attempt format conversion (may be a no-op if formats match)
        if (bytes_read > sizeof(s_proc_buffer)) {
            ESP_LOGW(TAG, "I2S read size %zu exceeds proc buffer %zu, truncating", bytes_read, sizeof(s_proc_buffer));
            bytes_read = sizeof(s_proc_buffer);
            s_audio_stats.conversion_errors++;
        }
        esp_err_t cret = convert_audio_format(s_i2s_buffer, s_proc_buffer, bytes_read,
                                             s_audio_config.bit_depth, s_audio_config.bit_depth,
                                             &conv_size);
        if (s_debug_log_count < 5) {
            ESP_LOGI(TAG, "convert_audio_format: bytes_read=%zu conv_size=%zu bit_depth=%d channels=%d",
                     bytes_read, conv_size, s_audio_config.bit_depth, s_audio_config.channels);
            s_debug_log_count++;
        }
        if (cret != ESP_OK) {
            s_audio_stats.conversion_errors++;
            // Skip this buffer
            audio_proc_mock_yield();
            continue;
        }

        // Then attempt resampling (may be a no-op if rates match)
        size_t res_size = 0;
        if (conv_size > sizeof(s_proc_buffer2)) {
            ESP_LOGW(TAG, "conv_size %zu exceeds proc_buffer2 %zu, truncating", conv_size, sizeof(s_proc_buffer2));
            conv_size = sizeof(s_proc_buffer2);
            s_audio_stats.conversion_errors++;
        }
        esp_err_t rret = resample_audio(s_proc_buffer, s_proc_buffer2, conv_size,
                                        s_audio_config.sample_rate, s_audio_config.sample_rate,
                                        &res_size);
        if (s_debug_log_count < 10) {
            ESP_LOGI(TAG, "resample_audio: conv_size=%zu res_size=%zu src_rate=%d dst_rate=%d",
                     conv_size, res_size, s_audio_config.sample_rate, s_audio_config.sample_rate);
            s_debug_log_count++;
        }

        /* Diagnostic: log detailed resampler inputs immediately before calling
         * resample_audio so we capture sizes and rates if a crash occurs inside
         * the resampler. This prints buffer addresses, conv_size, bytes/frame
         * and constants used for bounds checks. */
        {
            int diag_bytes_per_sample = audio_bytes_per_sample(s_audio_config.bit_depth);
            int diag_channels = (s_audio_config.channels == AUDIO_CHANNEL_MONO) ? 1 : 2;
            size_t diag_frame_bytes = (size_t)diag_bytes_per_sample * (size_t)diag_channels;
            int diag_src_rate = s_audio_config.sample_rate;
            int diag_dst_rate = s_audio_config.sample_rate;
            size_t diag_samples = (diag_frame_bytes > 0) ? (conv_size / diag_frame_bytes) : 0;

            bool diag_changed = (conv_size != s_diag_last_conv_size) ||
                                (diag_frame_bytes != s_diag_last_frame_bytes) ||
                                (diag_src_rate != s_diag_last_src_rate) ||
                                (diag_dst_rate != s_diag_last_dst_rate);
            TickType_t now_ticks = xTaskGetTickCount();
            bool should_log_diag = false;

            if (diag_changed) {
                should_log_diag = true;
            } else {
                bool window_elapsed = (s_diag_next_log_tick == 0) ||
                                      ((int32_t)(now_ticks - s_diag_next_log_tick) >= 0);
                if (window_elapsed) {
                    should_log_diag = true;
                }
            }

            if (should_log_diag) {
                ESP_LOGI(TAG, "DIAG resample inputs: proc_buf=%p proc_buf2=%p conv_size=%zu frame_bytes=%zu samples=%zu src_rate=%d dst_rate=%d AUDIO_WORK_BUFFER_BYTES=%zu",
                         (void*)s_proc_buffer, (void*)s_proc_buffer2, conv_size, diag_frame_bytes, diag_samples, diag_src_rate, diag_dst_rate, (size_t)AUDIO_WORK_BUFFER_BYTES);
                s_diag_next_log_tick = now_ticks + pdMS_TO_TICKS(1000);
                if (s_diag_next_log_tick == 0) {
                    s_diag_next_log_tick = 1;
                }
            }

            s_diag_last_conv_size = conv_size;
            s_diag_last_frame_bytes = diag_frame_bytes;
            s_diag_last_src_rate = diag_src_rate;
            s_diag_last_dst_rate = diag_dst_rate;
        }
        if (rret != ESP_OK) {
            s_audio_stats.conversion_errors++;
            audio_proc_mock_yield();
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
                audio_proc_mock_yield();
                continue;
            }
        }

        // Add processed audio to the buffer
    UBaseType_t buf_ret = xRingbufferSend(s_audio_buffer, s_proc_buffer2, conv_size, 0);
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

    /* The mock generator returns immediately, so pace the loop to
     * emulate hardware latency and keep the task watchdog satisfied. */
    audio_proc_mock_yield();
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
        size_t copy_size = src_size;
        if (copy_size > AUDIO_WORK_BUFFER_BYTES) {
            ESP_LOGW(TAG, "convert_audio_format: copy truncated from %zu to %u bytes", copy_size, (unsigned)AUDIO_WORK_BUFFER_BYTES);
            copy_size = AUDIO_WORK_BUFFER_BYTES;
            s_audio_stats.conversion_errors++;
        }
        memcpy(dst, src, copy_size);
        *dst_size = copy_size;
        return ESP_OK;
    }

    // Calculate sample counts
    int src_bytes_per_sample = audio_bytes_per_sample(src_bit_depth);
    int dst_bytes_per_sample = audio_bytes_per_sample(dst_bit_depth);
    if (src_bytes_per_sample <= 0 || dst_bytes_per_sample <= 0) {
        return ESP_ERR_INVALID_ARG;
    }
    int src_sample_count = src_size / src_bytes_per_sample;
    size_t calculated = (size_t)src_sample_count * (size_t)dst_bytes_per_sample;
    if (calculated > AUDIO_WORK_BUFFER_BYTES) {
        ESP_LOGW(TAG, "convert_audio_format: dst size %zu exceeds buffer %u, truncating", calculated, (unsigned)AUDIO_WORK_BUFFER_BYTES);
        calculated = AUDIO_WORK_BUFFER_BYTES;
        s_audio_stats.conversion_errors++;
    }
    *dst_size = calculated;

    // Handle different conversion scenarios
    if (src_bit_depth == AUDIO_BIT_DEPTH_16 && dst_bit_depth == AUDIO_BIT_DEPTH_32) {
        // 16-bit to 32-bit
        int16_t* src_samples = (int16_t*)src;
        int32_t* dst_samples = (int32_t*)dst;
        
        for (int i = 0; i < src_sample_count; i++) {
            size_t idx = (size_t)i;
            if ((idx + 1) * sizeof(int32_t) > *dst_size) break;
            // Scale up with proper bit shift (16 bits to 32 bits)
            dst_samples[i] = ((int32_t)src_samples[i]) << 16;
        }
    }
    else if (src_bit_depth == AUDIO_BIT_DEPTH_32 && dst_bit_depth == AUDIO_BIT_DEPTH_16) {
        // 32-bit to 16-bit
        int32_t* src_samples = (int32_t*)src;
        int16_t* dst_samples = (int16_t*)dst;
        
        for (int i = 0; i < src_sample_count; i++) {
            size_t idx = (size_t)i;
            if ((idx + 1) * sizeof(int16_t) > *dst_size) break;
            // Scale down with proper bit shift and dithering
            dst_samples[i] = (int16_t)(src_samples[i] >> 16);
        }
    }
    else if (src_bit_depth == AUDIO_BIT_DEPTH_24 && dst_bit_depth == AUDIO_BIT_DEPTH_16) {
        // 24-bit to 16-bit (assuming 24-bit is stored in 32-bit containers)
        int32_t* src_samples = (int32_t*)src;
        int16_t* dst_samples = (int16_t*)dst;
        
        for (int i = 0; i < src_sample_count; i++) {
            size_t idx = (size_t)i;
            if ((idx + 1) * sizeof(int16_t) > *dst_size) break;
            // Scale down with proper bit shift
            dst_samples[i] = (int16_t)(src_samples[i] >> 8);
        }
    }
    else if (src_bit_depth == AUDIO_BIT_DEPTH_16 && dst_bit_depth == AUDIO_BIT_DEPTH_24) {
        // 16-bit to 24-bit (stored in 32-bit containers)
        int16_t* src_samples = (int16_t*)src;
        int32_t* dst_samples = (int32_t*)dst;
        
        for (int i = 0; i < src_sample_count; i++) {
            size_t idx = (size_t)i;
            if ((idx + 1) * sizeof(int32_t) > *dst_size) break;
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
    if (dst_size == NULL) {
        ESP_LOGE(TAG, "resample_audio: null dst_size");
        return ESP_ERR_INVALID_ARG;
    }
    *dst_size = 0;

    if (src == NULL || dst == NULL) {
        ESP_LOGE(TAG, "resample_audio: null buffer src=%p dst=%p src_size=%zu", src, dst, src_size);
        return ESP_ERR_INVALID_ARG;
    }

    if (src_size == 0) {
        return ESP_OK;
    }

    // Sanity clamp: never write more than our work buffers
    if (src_size > AUDIO_WORK_BUFFER_BYTES) {
        ESP_LOGW(TAG, "resample_audio: src_size (%zu) exceeds AUDIO_WORK_BUFFER_BYTES (%d), truncating", src_size, AUDIO_WORK_BUFFER_BYTES);
        src_size = AUDIO_WORK_BUFFER_BYTES;
        s_audio_stats.conversion_errors++;
    }

    // (diagnostic logging will be emitted after validation of config and sizes)

    if (src == NULL || dst == NULL || dst_size == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (src_rate <= 0 || dst_rate <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    int channels = s_audio_config.channels;
    if (channels != AUDIO_CHANNEL_MONO && channels != AUDIO_CHANNEL_STEREO) {
        channels = AUDIO_CHANNEL_STEREO;
    }

    int bytes_per_sample = audio_bytes_per_sample(s_audio_config.bit_depth);
    if (bytes_per_sample <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t frame_bytes = (size_t)bytes_per_sample * (size_t)channels;
    if (frame_bytes == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (src_size > AUDIO_WORK_BUFFER_BYTES) {
        ESP_LOGW(TAG, "Resample input truncated from %zu to %u bytes", src_size, (unsigned)AUDIO_WORK_BUFFER_BYTES);
        src_size = AUDIO_WORK_BUFFER_BYTES;
        s_audio_stats.conversion_errors++;
    }

    size_t src_sample_count = src_size / (size_t)bytes_per_sample;
    if (src_sample_count < (size_t)channels) {
        *dst_size = 0;
        return ESP_OK;
    }

    size_t src_frame_count = src_sample_count / (size_t)channels;
    if (src_frame_count < 2) {
        if (src_size == 0) {
            *dst_size = 0;
            return ESP_OK;
        }
        if (dst == NULL || src == NULL) {
            ESP_LOGE(TAG, "resample_audio: null src/dst on small-frame pass-through");
            return ESP_ERR_INVALID_ARG;
        }
        if (src_size > AUDIO_WORK_BUFFER_BYTES) {
            ESP_LOGW(TAG, "resample_audio: pass-through truncated %zu -> %u", src_size, (unsigned)AUDIO_WORK_BUFFER_BYTES);
            src_size = AUDIO_WORK_BUFFER_BYTES;
            s_audio_stats.conversion_errors++;
        }
        memmove(dst, src, src_size);
        *dst_size = src_size;
        return ESP_OK;
    }

    if (src_rate == dst_rate) {
        if (src_size == 0) {
            *dst_size = 0;
            return ESP_OK;
        }
        if (dst == NULL || src == NULL) {
            ESP_LOGE(TAG, "resample_audio: null src/dst on rate-equal copy");
            return ESP_ERR_INVALID_ARG;
        }
        if (src_size > AUDIO_WORK_BUFFER_BYTES) {
            ESP_LOGW(TAG, "resample_audio: rate-equal copy truncated %zu -> %u", src_size, (unsigned)AUDIO_WORK_BUFFER_BYTES);
            src_size = AUDIO_WORK_BUFFER_BYTES;
            s_audio_stats.conversion_errors++;
        }
        memmove(dst, src, src_size);
        *dst_size = src_size;
        return ESP_OK;
    }

    double ratio = (double)dst_rate / (double)src_rate;
    if (!(ratio > 0.0) || !isfinite(ratio)) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t max_dst_frames = AUDIO_WORK_BUFFER_BYTES / frame_bytes;
    if (max_dst_frames == 0) {
        return ESP_ERR_INVALID_SIZE;
    }

    size_t ideal_dst_frames = (size_t)floor((double)src_frame_count * ratio);
    if (ideal_dst_frames == 0) {
        ideal_dst_frames = 1;
    }

    bool truncated = false;
    size_t dst_frame_count = ideal_dst_frames;
    if (dst_frame_count > max_dst_frames) {
        dst_frame_count = max_dst_frames;
        truncated = true;
    }

    size_t dst_sample_count = dst_frame_count * (size_t)channels;
    size_t dst_bytes = dst_sample_count * (size_t)bytes_per_sample;

    if (dst_bytes > AUDIO_WORK_BUFFER_BYTES) {
        dst_frame_count = max_dst_frames;
        dst_sample_count = dst_frame_count * (size_t)channels;
        dst_bytes = dst_sample_count * (size_t)bytes_per_sample;
        truncated = true;
    }

    if (dst_frame_count == 0) {
        *dst_size = 0;
        return ESP_ERR_INVALID_SIZE;
    }

    ESP_LOGI(TAG, "resample_audio DIAG: src=%p dst=%p src_size=%zu bytes_per_sample=%d channels=%d src_frames=%zu dst_frames=%zu ideal_dst_frames=%zu dst_bytes=%zu frame_bytes=%zu ratio=%.6f max_dst_frames=%zu",
             src, dst, src_size, bytes_per_sample, channels, src_frame_count, dst_frame_count, ideal_dst_frames, dst_bytes, frame_bytes, ratio, max_dst_frames);

    if (dst_bytes == 0) {
        *dst_size = 0;
        return ESP_ERR_INVALID_SIZE;
    }

    int dst_frames = (int)dst_frame_count;
    int src_frames = (int)src_frame_count;

    /* Use a mapping that spans [0, src_frames-1] so interpolation doesn't
     * attempt to read src_frame+1 past the end. For dst_frame_count==1 we
     * copy the first frame. For each dst frame compute a source position t in
     * [0, src_frames-1] using (dst*(src_frames-1))/(dst_frames-1). If t hits
     * the final source frame, emit the exact sample without interpolation. */

    if (s_audio_config.bit_depth == AUDIO_BIT_DEPTH_16) {
        int16_t* src_samples = (int16_t*)src;
        int16_t* dst_samples = (int16_t*)dst;

        for (int dstFrame = 0; dstFrame < dst_frames; ++dstFrame) {
            double t;
            if (dst_frames > 1) {
                t = (double)dstFrame * (double)(src_frames - 1) / (double)(dst_frames - 1);
            } else {
                t = 0.0;
            }
            int s0 = (int)floor(t);
            double frac = t - s0;
            int s1 = s0 + 1;
            if (s0 >= src_frames - 1) {
                s0 = src_frames - 1;
                s1 = s0; /* will read only s0 */
                frac = 0.0;
            }

            for (int ch = 0; ch < channels; ++ch) {
                int src_idx1 = s0 * channels + ch;
                int src_idx2 = s1 * channels + ch;
                int dst_idx = dstFrame * channels + ch;

                size_t dst_byte_off = (size_t)dst_idx * sizeof(int16_t);
                size_t src_byte_off2 = (size_t)src_idx2 * sizeof(int16_t);
                if (dst_byte_off + sizeof(int16_t) > dst_bytes || src_byte_off2 + sizeof(int16_t) > src_size) {
                    /* If interpolation would read past src or write past dst,
                     * fallback to copying the nearest available sample. */
                    if ((size_t)src_idx1 * sizeof(int16_t) + sizeof(int16_t) <= src_size && dst_byte_off + sizeof(int16_t) <= dst_bytes) {
                        dst_samples[dst_idx] = src_samples[src_idx1];
                    }
                    continue;
                }

                if (s1 == s0) {
                    dst_samples[dst_idx] = src_samples[src_idx1];
                } else {
                    dst_samples[dst_idx] = (int16_t)((1.0 - frac) * src_samples[src_idx1] + frac * src_samples[src_idx2]);
                }
            }
        }
    } else if (s_audio_config.bit_depth == AUDIO_BIT_DEPTH_32) {
        int32_t* src_samples = (int32_t*)src;
        int32_t* dst_samples = (int32_t*)dst;

        for (int dstFrame = 0; dstFrame < dst_frames; ++dstFrame) {
            double t;
            if (dst_frames > 1) {
                t = (double)dstFrame * (double)(src_frames - 1) / (double)(dst_frames - 1);
            } else {
                t = 0.0;
            }
            int s0 = (int)floor(t);
            double frac = t - s0;
            int s1 = s0 + 1;
            if (s0 >= src_frames - 1) {
                s0 = src_frames - 1;
                s1 = s0;
                frac = 0.0;
            }

            for (int ch = 0; ch < channels; ++ch) {
                int src_idx1 = s0 * channels + ch;
                int src_idx2 = s1 * channels + ch;
                int dst_idx = dstFrame * channels + ch;

                size_t dst_byte_off = (size_t)dst_idx * sizeof(int32_t);
                size_t src_byte_off2 = (size_t)src_idx2 * sizeof(int32_t);
                if (dst_byte_off + sizeof(int32_t) > dst_bytes || src_byte_off2 + sizeof(int32_t) > src_size) {
                    if ((size_t)src_idx1 * sizeof(int32_t) + sizeof(int32_t) <= src_size && dst_byte_off + sizeof(int32_t) <= dst_bytes) {
                        dst_samples[dst_idx] = src_samples[src_idx1];
                    }
                    continue;
                }

                if (s1 == s0) {
                    dst_samples[dst_idx] = src_samples[src_idx1];
                } else {
                    dst_samples[dst_idx] = (int32_t)((1.0 - frac) * src_samples[src_idx1] + frac * src_samples[src_idx2]);
                }
            }
        }
    } else {
        ESP_LOGE(TAG, "Unsupported bit depth for resampling: %d", s_audio_config.bit_depth);
        s_audio_stats.conversion_errors++;
        return ESP_ERR_NOT_SUPPORTED;
    }

    *dst_size = dst_bytes;
    if (truncated) s_audio_stats.conversion_errors++;
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

/**
 * @brief Check if a beep is currently active (for testing)
 */
bool audio_processor_is_beep_active(void) {
    return s_beep_remaining_bytes > 0;
}

#ifdef CONFIG_BT_MOCK_TESTING
/**
 * @brief Inject audio data directly into the ring buffer (for testing only)
 */
esp_err_t audio_processor_test_inject_audio_data(const uint8_t* data, size_t size)
{
    if (!s_is_initialized) {
        ESP_LOGE(TAG, "audio_processor_test_inject_audio_data: not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (data == NULL || size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    // Check available space in the buffer
    size_t free_size = xRingbufferGetCurFreeSize(s_audio_buffer);
    if (free_size < size) {
        ESP_LOGW(TAG, "audio_processor_test_inject_audio_data: not enough space (%zu < %zu)", free_size, size);
        return ESP_ERR_NO_MEM;
    }

    // Send data to ring buffer
    BaseType_t sent = xRingbufferSend(s_audio_buffer, data, size, 0);
    if (sent != pdTRUE) {
        ESP_LOGE(TAG, "audio_processor_test_inject_audio_data: failed to send to ringbuffer");
        return ESP_FAIL;
    }

    return ESP_OK;
}
#endif
