#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/ringbuf.h"
#include "esp_log.h"
#include "driver/i2s_std.h"  // Use the current I2S driver
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_err.h"
#include "audio_processor.h"
#include "nvs_storage.h"
#include "esp_heap_caps.h"
#if (defined(CONFIG_SPIRAM) && CONFIG_SPIRAM) || (defined(CONFIG_SPIRAM_SUPPORT) && CONFIG_SPIRAM_SUPPORT)
#include "esp_psram.h"
#endif

static const char *TAG = "AUDIO_PROC";

// Audio buffer sizes
/* If SPIRAM is available at build-time use the full-size buffer (1s stereo
 * 32-bit @48kHz). If SPIRAM is NOT enabled in sdkconfig, pick a DRAM-safe
 * fallback so the firmware can boot on DRAM-only boards. */
#ifdef CONFIG_SPIRAM
#define AUDIO_BUFFER_SIZE            (48000 * 4 * 2) // Max 1 second of stereo 32-bit audio at 48kHz
#else
#define AUDIO_BUFFER_SIZE            (131072) // 128KB DRAM-safe fallback
#endif
#define AUDIO_PROCESSING_STACK_SIZE  4096
#define AUDIO_BLOCK_SIZE             128  // Process audio in smaller blocks (reduced from 512) to shorten resample work per iteration
#ifdef CONFIG_BT_MOCK_TESTING
#define AUDIO_RESAMPLE_MAX_RATIO     6    // Cover worst-case 8 kHz -> 48 kHz upsampling in tests
#else
#define AUDIO_RESAMPLE_MAX_RATIO     12   // Cover 8 kHz -> 96 kHz upsampling in production paths
#endif
#define AUDIO_WORK_BUFFER_BYTES      (AUDIO_BLOCK_SIZE * 8 * AUDIO_RESAMPLE_MAX_RATIO)

// Audio processing task handle
static TaskHandle_t s_audio_task_handle = NULL; /* I2S reader task */
static TaskHandle_t s_audio_worker_handle = NULL; /* Worker that performs convert/resample */
typedef struct {
    void* ptr;
    size_t len;
    size_t capacity;
    bool synth_fill;
} i2s_block_t;

static QueueHandle_t s_i2s_queue = NULL; /* Queue of i2s_block_t */
static QueueHandle_t s_i2s_free_queue = NULL; /* Queue of free raw block pointers */
static void **s_i2s_pool = NULL; /* Array of pointers for freeing at deinit */
#define I2S_RAW_POOL_DEFAULT_COUNT 8U
#define I2S_RAW_POOL_DRAM_COUNT    3U

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
/* Force synthetic audio at runtime when requested by user. This forces the
 * audio path to use the built-in synth generator and avoids I2S timeouts
 * when the external I2S source is absent. The user can still toggle this
 * at runtime via audio_processor_set_synth_mode(). */
static bool s_force_synth = true;
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

/**
 * @brief Enable or disable runtime synthetic audio mode.
 *
 * This API toggles a runtime flag; it does not change build-time
 * configuration. Use this to override I2S behavior when testing or when
 * hardware is absent.
 */
void audio_processor_set_synth_mode(bool enable)
{
    s_force_synth = enable ? true : false;
    ESP_LOGI(TAG, "audio_processor: synth mode %s", s_force_synth ? "ENABLED" : "DISABLED");
}

bool audio_processor_is_synth_mode_enabled(void)
{
    return s_force_synth;
}

// Audio processing scratch buffers (allocated on heap at init to avoid
// large .bss usage which can overflow DRAM on resource-constrained boards)
static uint8_t *s_i2s_buffer = NULL;
static uint8_t *s_proc_buffer = NULL;
static uint8_t *s_proc_buffer2 = NULL;
/* Single contiguous work block to reduce fragmentation and improve the
 * chance of a successful allocation on low-memory boards. We allocate one
 * block and carve it into three sub-buffers (i2s, proc, proc2). */
static uint8_t *s_work_block = NULL;

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

#if defined(CONFIG_AUDIO_USE_SYNTH_SOURCE)
/* Simple synthetic generator state used when CONFIG_AUDIO_USE_SYNTH_SOURCE
 * is enabled. We use a very cheap square-wave generator implemented with
 * a 32-bit phase accumulator to minimize CPU use and avoid expensive
 * trig/math calls. */
static uint32_t s_synth_phase_acc = 0;
static uint32_t s_synth_phase_step = 0;
static size_t synth_generate_audio(uint8_t* buffer, size_t buffer_size);
#endif

// Forward declarations of internal functions
static void i2s_reader_task(void *pvParameters);
static void audio_worker_task(void *pvParameters);
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

#if defined(CONFIG_AUDIO_USE_SYNTH_SOURCE)
static size_t synth_generate_audio(uint8_t* buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0) return 0;

    int bytes_per_sample = audio_bytes_per_sample(s_audio_config.bit_depth);
    if (bytes_per_sample <= 0) bytes_per_sample = 2;
    int channels = (s_audio_config.channels == AUDIO_CHANNEL_MONO) ? 1 : 2;
    size_t frame_bytes = (size_t)bytes_per_sample * (size_t)channels;
    if (frame_bytes == 0) return 0;

    size_t max_frames = buffer_size / frame_bytes;
    if (max_frames == 0) return 0;

    /* Generate up to AUDIO_BLOCK_SIZE frames per iteration to match the
     * existing processing granularity. */
    size_t frames_to_write = AUDIO_BLOCK_SIZE;
    if (frames_to_write > max_frames) frames_to_write = max_frames;

    /* Use a square wave at 440Hz (A4). Square wave generation only needs
     * a phase and a comparison—no trig calls—so it's extremely cheap. */
    const float freq = 440.0f; /* A4 tone */
    const float sample_rate = (float)s_audio_config.sample_rate;

    /* Compute phase step in 32.8 fixed-point (24 fractional bits). */
    if (sample_rate > 0.0f) {
        s_synth_phase_step = (uint32_t)((freq * (1ULL << 24)) / (uint32_t)sample_rate);
    } else {
        s_synth_phase_step = (uint32_t)((freq * (1ULL << 24)) / 44100U);
    }

    if (s_audio_config.bit_depth == AUDIO_BIT_DEPTH_16) {
        int16_t* out = (int16_t*)buffer;
        for (size_t f = 0; f < frames_to_write; ++f) {
            /* Top 8 bits give fractional phase; use top bit of those to
             * determine the sign for the square wave (128 entry wrap). */
            uint32_t idx = s_synth_phase_acc >> 24; /* 0..255 */
            int16_t sample = (idx < 128) ? 12000 : -12000;
            for (int ch = 0; ch < channels; ++ch) {
                *out++ = sample;
            }
            s_synth_phase_acc += s_synth_phase_step;
        }
        return frames_to_write * frame_bytes;
    } else {
        /* 32-bit container (32-bit or 24-bit stored in 32-bit) */
        int32_t* out32 = (int32_t*)buffer;
        for (size_t f = 0; f < frames_to_write; ++f) {
            uint32_t idx = s_synth_phase_acc >> 24;
        int32_t sample = (idx < 128) ? (12000 << 16) : -((int32_t)(12000 << 16));
            for (int ch = 0; ch < channels; ++ch) {
                *out32++ = sample;
            }
            s_synth_phase_acc += s_synth_phase_step;
        }
        return frames_to_write * frame_bytes;
    }
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

#ifdef CONFIG_SPIRAM
    ESP_LOGI(TAG, "Build-time: SPIRAM enabled - AUDIO_BUFFER_SIZE=%zu, buffer_capacity=%zu", (size_t)AUDIO_BUFFER_SIZE, buffer_capacity);
#else
    ESP_LOGI(TAG, "Build-time: SPIRAM disabled - using DRAM-safe AUDIO_BUFFER_SIZE=%zu, buffer_capacity=%zu", (size_t)AUDIO_BUFFER_SIZE, buffer_capacity);
#endif

    // Create audio buffer. Try progressively smaller sizes at runtime if
    // allocation fails (useful for DRAM-only boards where the compile-time
    // DRAM-safe fallback may still be too large).
    size_t try_capacity = buffer_capacity;
    const size_t min_floor = 4 * 1024; // Allow down to 4 KiB for Unity/mock builds

    /* Ensure we never shrink below a single processing block and keep a
     * modest floor so the stream retains some buffering depth on DRAM-only
     * boards. */
    size_t frame_bytes = (size_t)audio_bytes_per_sample(s_audio_config.bit_depth);
    if (frame_bytes == 0U) {
        frame_bytes = 2U;
    }
    size_t channel_count = (s_audio_config.channels == AUDIO_CHANNEL_MONO) ? 1U : 2U;
    if (channel_count == 0U) {
        channel_count = 2U;
    }
    frame_bytes *= channel_count;
    if (frame_bytes == 0U) {
        frame_bytes = 4U;
    }
    size_t block_requirement = frame_bytes * (size_t)AUDIO_BLOCK_SIZE;
    if (block_requirement == 0U) {
        block_requirement = 1024U;
    }

    size_t desired_min = block_requirement;
    if (desired_min < min_floor) {
        desired_min = min_floor;
    }
    size_t min_capacity = (buffer_capacity < desired_min) ? buffer_capacity : desired_min;
    if (min_capacity == 0U) {
        min_capacity = desired_min;
    }

    s_audio_buffer = NULL;
    while (true) {
        if (try_capacity < min_capacity) {
            try_capacity = min_capacity;
        }

        ESP_LOGI(TAG, "Attempting to create audio buffer of %zu bytes", try_capacity);
        s_audio_buffer = xRingbufferCreate(try_capacity, RINGBUF_TYPE_BYTEBUF);
        if (s_audio_buffer != NULL) {
            ESP_LOGI(TAG, "Audio buffer created (%zu bytes)", try_capacity);
            break;
        }

        ESP_LOGW(TAG, "xRingbufferCreate(%zu) failed, trying smaller size", try_capacity);
        if (try_capacity == min_capacity) {
            break;
        }

        try_capacity = try_capacity / 2U;
        try_capacity = (try_capacity + 3U) & ~((size_t)3U);
    }

    if (s_audio_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to create audio buffer (tried down to %zu bytes)", try_capacity);
        return ESP_ERR_NO_MEM;
    }
    
    /* Allocate work buffers on the heap. Prefer SPIRAM/8-bit-capable memory
     * when available (heap_caps) to reduce internal DRAM pressure. These
     * buffers are moderately large and kept persistent for the life of the
     * audio processor. */
    /* Prefer allocating large, persistent audio work buffers in PSRAM
     * when available to reduce DRAM pressure. Fall back to DRAM (8-bit
     * capable) if PSRAM allocation fails. */
    /* Allocate work buffers on the heap. Prefer SPIRAM/8-bit-capable memory
     * when available (heap_caps) to reduce internal DRAM pressure. These
     * buffers are moderately large and kept persistent for the life of the
     * audio processor. If allocations fail (common on DRAM-only boards) try
     * progressively smaller buffer sizes for all three work buffers so the
     * system can boot with reduced capability. */
    size_t try_work_bytes = (size_t)AUDIO_WORK_BUFFER_BYTES;
    const size_t min_work_bytes = 4 * 1024; // 4KB minimum per buffer
    bool work_allocated = false;

    while (try_work_bytes >= min_work_bytes) {
        ESP_LOGI(TAG, "Attempting audio work buffers of %zu bytes each (combined allocation)", try_work_bytes);

        size_t combined = try_work_bytes * 3U;

        /* Try a single contiguous allocation first (prefer PSRAM). This
         * reduces fragmentation and the number of heap headers compared to
         * three separate allocations. */
        s_work_block = heap_caps_malloc(combined, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_work_block == NULL) {
            ESP_LOGW(TAG, "PSRAM combined allocation failed; falling back to DRAM for %zu bytes", combined);
            s_work_block = heap_caps_malloc(combined, MALLOC_CAP_DEFAULT | MALLOC_CAP_8BIT);
        }

        if (s_work_block != NULL) {
            /* Carve the single block into three equal sub-buffers */
            s_i2s_buffer = s_work_block;
            s_proc_buffer = s_work_block + try_work_bytes;
            s_proc_buffer2 = s_work_block + (try_work_bytes * 2U);

            ESP_LOGI(TAG, "Audio work combined block allocated: %zu bytes (3 x %zu)", combined, try_work_bytes);
            work_allocated = true;
            break;
        }

        ESP_LOGW(TAG, "Combined work buffer allocation failed for %zu bytes each; reducing size and retrying", try_work_bytes);
        try_work_bytes = try_work_bytes / 2U;
        /* Keep 4-byte alignment */
        try_work_bytes = (try_work_bytes + 3U) & ~((size_t)3U);
    }

    if (!work_allocated) {
        ESP_LOGE(TAG, "Failed to allocate audio work buffers (tried down to %zu bytes each)", min_work_bytes);
        vRingbufferDelete(s_audio_buffer);
        s_audio_buffer = NULL;
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

    // Create queue used to hand raw I2S blocks from the fast reader to the
    // lower-priority worker. Queue stores i2s_block_t which contains
    // a pointer to a preallocated raw buffer and its length.
    const size_t i2s_queue_len = 8;
    s_i2s_queue = xQueueCreate(i2s_queue_len, sizeof(i2s_block_t));
    if (s_i2s_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create I2S queue");
        vRingbufferDelete(s_audio_buffer);
        s_audio_buffer = NULL;
        i2s_del_channel(s_i2s_rx_handle);
        return ESP_ERR_NO_MEM;
    }

    size_t pool_target = I2S_RAW_POOL_DEFAULT_COUNT;
#if (defined(CONFIG_SPIRAM) && CONFIG_SPIRAM) || (defined(CONFIG_SPIRAM_SUPPORT) && CONFIG_SPIRAM_SUPPORT)
    bool psram_ready = esp_psram_is_initialized();
    if (!psram_ready) {
        pool_target = I2S_RAW_POOL_DRAM_COUNT;
    }
#else
    const bool psram_ready = false;
    pool_target = I2S_RAW_POOL_DRAM_COUNT;
#endif
    if (pool_target == 0U) {
        pool_target = 1U;
    }

    /* Create free-list queue and attempt to preallocate a pool of raw blocks
     * to avoid slow heap allocations during the real-time reader. Prefer
     * PSRAM for the blocks; fall back to DRAM if PSRAM is unavailable. */
    s_i2s_free_queue = xQueueCreate((UBaseType_t)pool_target, sizeof(void*));
    if (s_i2s_free_queue != NULL) {
        s_i2s_pool = heap_caps_malloc(sizeof(void*) * pool_target, MALLOC_CAP_DEFAULT);
        if (s_i2s_pool != NULL) {
            size_t allocated = 0;
            for (size_t i = 0; i < pool_target; ++i) {
                void* blk = heap_caps_malloc(AUDIO_WORK_BUFFER_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                if (blk == NULL) {
                    blk = heap_caps_malloc(AUDIO_WORK_BUFFER_BYTES, MALLOC_CAP_DEFAULT | MALLOC_CAP_8BIT);
                }
                if (blk == NULL) {
                    ESP_LOGW(TAG, "Prealloc pool: failed to allocate block %zu of %u", i, (unsigned)pool_target);
                    break;
                }
                s_i2s_pool[i] = blk;
                if (xQueueSend(s_i2s_free_queue, &s_i2s_pool[i], 0) != pdTRUE) {
                    /* Unexpected: free queue should accept these */
                    heap_caps_free(blk);
                    s_i2s_pool[i] = NULL;
                    break;
                }
                allocated++;
            }
            if (allocated == 0) {
                /* Preallocation failed; free structures and continue with
                 * on-demand allocation fallback in the reader. */
                heap_caps_free(s_i2s_pool);
                s_i2s_pool = NULL;
                vQueueDelete(s_i2s_free_queue);
                s_i2s_free_queue = NULL;
                ESP_LOGW(TAG, "Prealloc pool disabled due to allocation failures; reader will fallback to heap allocations");
            } else if (allocated < pool_target) {
                ESP_LOGW(TAG, "Prealloc pool partially allocated (%u/%u); continuing", (unsigned)allocated, (unsigned)pool_target);
            } else {
                ESP_LOGI(TAG, "Preallocated %u raw I2S blocks (%s)", (unsigned)pool_target, psram_ready ? "PSRAM" : "DRAM");
            }
        } else {
            vQueueDelete(s_i2s_free_queue);
            s_i2s_free_queue = NULL;
            ESP_LOGW(TAG, "Failed to allocate pool pointer array; skipping prealloc");
        }
    } else {
        ESP_LOGW(TAG, "Failed to create i2s free queue; reader will fallback to heap allocations");
    }

    // Create fast I2S reader task (keeps reads short) and a lower-priority
    // worker task that performs conversion/resampling and pushes to ringbuffer.
    BaseType_t task_ret = xTaskCreate(i2s_reader_task, "i2s_reader",
                                     AUDIO_PROCESSING_STACK_SIZE, NULL, 6, &s_audio_task_handle);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create i2s reader task");
        vQueueDelete(s_i2s_queue);
        s_i2s_queue = NULL;
        vRingbufferDelete(s_audio_buffer);
        s_audio_buffer = NULL;
        i2s_del_channel(s_i2s_rx_handle);
        return ESP_FAIL;
    }

    task_ret = xTaskCreate(audio_worker_task, "audio_worker",
                                     AUDIO_PROCESSING_STACK_SIZE, NULL, 4, &s_audio_worker_handle);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create audio worker task");
        vTaskDelete(s_audio_task_handle);
        s_audio_task_handle = NULL;
        vQueueDelete(s_i2s_queue);
        s_i2s_queue = NULL;
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

    // Delete reader and worker tasks
    if (s_audio_task_handle != NULL) {
        vTaskDelete(s_audio_task_handle);
        s_audio_task_handle = NULL;
    }
    if (s_audio_worker_handle != NULL) {
        vTaskDelete(s_audio_worker_handle);
        s_audio_worker_handle = NULL;
    }

    // Flush and delete I2S queue, free any queued raw blocks
        if (s_i2s_queue != NULL) {
            i2s_block_t item = {0};
            while (xQueueReceive(s_i2s_queue, &item, 0) == pdTRUE) {
                if (item.ptr) {
                    heap_caps_free(item.ptr);
                }
                item.ptr = NULL;
                item.len = 0;
            }
            vQueueDelete(s_i2s_queue);
            s_i2s_queue = NULL;
        }

        /* Free any blocks still in the free pool queue, then delete the free
         * queue and free the pool pointer array. */
        if (s_i2s_free_queue != NULL) {
            void* p = NULL;
            while (xQueueReceive(s_i2s_free_queue, &p, 0) == pdTRUE) {
                if (p) heap_caps_free(p);
            }
            vQueueDelete(s_i2s_free_queue);
            s_i2s_free_queue = NULL;
        }

        if (s_i2s_pool != NULL) {
            heap_caps_free(s_i2s_pool);
            s_i2s_pool = NULL;
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

    /* Free heap-allocated work buffers (we allocated a single combined
     * block and carved it into three sub-buffers). Free only the block. */
    if (s_work_block) { heap_caps_free(s_work_block); s_work_block = NULL; }
    s_i2s_buffer = NULL;
    s_proc_buffer = NULL;
    s_proc_buffer2 = NULL;

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
 * @brief Fast I2S reader task
 *
 * This task performs minimal, time-bounded work: read from I2S (or synth)
 * into the shared `s_i2s_buffer`, allocate a small heap block, copy the
 * raw data into it and enqueue the block to `s_i2s_queue` for the
 * lower-priority worker to perform conversion/resampling.
 */
static void i2s_reader_task(void *pvParameters)
{
    ESP_LOGI(TAG, "I2S reader task started");

    static int consecutive_i2s_failures = 0;

    while (1) {
        if (!s_is_running || !s_is_initialized) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        size_t bytes_read = 0;
        esp_err_t last_i2s_ret = ESP_OK;
        bool have_frame = false;

        int bytes_per_sample = audio_bytes_per_sample(s_audio_config.bit_depth);
        if (bytes_per_sample <= 0) bytes_per_sample = 2;
        int channels = s_audio_config.channels;
        if (channels != AUDIO_CHANNEL_MONO && channels != AUDIO_CHANNEL_STEREO) channels = AUDIO_CHANNEL_STEREO;
        size_t frame_bytes = (size_t)bytes_per_sample * (size_t)channels;

        size_t ideal_read = AUDIO_WORK_BUFFER_BYTES;
        if (frame_bytes > 0) {
            ideal_read = (ideal_read / frame_bytes) * frame_bytes;
            if (ideal_read == 0) {
                ideal_read = frame_bytes;
            }
        }

        size_t synth_target_bytes = frame_bytes * (size_t)AUDIO_BLOCK_SIZE;
        if (synth_target_bytes == 0 || synth_target_bytes > AUDIO_WORK_BUFFER_BYTES) {
            synth_target_bytes = ideal_read;
        }

        size_t read_request = ideal_read;

        if (s_force_synth) {
            bytes_read = synth_target_bytes;
            have_frame = (bytes_read > 0);
            last_i2s_ret = ESP_OK;
            consecutive_i2s_failures = 0;
        } else {
            last_i2s_ret = i2s_channel_read(s_i2s_rx_handle, s_i2s_buffer, read_request, &bytes_read, 0);
            if (last_i2s_ret == ESP_OK && bytes_read > 0) {
                have_frame = true;
                consecutive_i2s_failures = 0;
            } else if (last_i2s_ret != ESP_OK) {
                consecutive_i2s_failures++;
            } else {
                last_i2s_ret = ESP_ERR_INVALID_SIZE;
                consecutive_i2s_failures++;
            }

            const int FAILURE_THRESHOLD = 20;
            if (consecutive_i2s_failures >= FAILURE_THRESHOLD) {
                ESP_LOGW(TAG, "I2S read failing repeatedly (%d); enabling runtime synth mode", consecutive_i2s_failures);
                s_force_synth = true;
            }
        }

        if (!have_frame) {
#ifndef CONFIG_BT_MOCK_TESTING
            if (last_i2s_ret != ESP_OK) {
                ESP_LOGW(TAG, "I2S read failed: %d (%s) requested=%zu aligned_frame_bytes=%zu got_bytes=%zu",
                         last_i2s_ret, esp_err_to_name(last_i2s_ret), read_request, frame_bytes, bytes_read);
            }
#endif
            audio_proc_mock_yield();
            if (s_force_synth) {
                TickType_t delay_ticks = pdMS_TO_TICKS(1);
                if (delay_ticks == 0) {
                    delay_ticks = 1;
                }
                vTaskDelay(delay_ticks);
            } else {
                taskYIELD();
            }
            continue;
        }

        if (bytes_read > AUDIO_WORK_BUFFER_BYTES) {
            bytes_read = AUDIO_WORK_BUFFER_BYTES;
        }

        bool backpressure = false;

        if (s_i2s_queue != NULL && s_i2s_free_queue != NULL && uxQueueSpacesAvailable(s_i2s_queue) > 0U) {
            void* pooled_ptr = NULL;
            if (xQueueReceive(s_i2s_free_queue, &pooled_ptr, 0) == pdTRUE && pooled_ptr != NULL) {
                i2s_block_t blk = {
                    .ptr = pooled_ptr,
                    .len = s_force_synth ? 0 : bytes_read,
                    .capacity = bytes_read,
                    .synth_fill = s_force_synth,
                };
                if (!blk.synth_fill && blk.len > 0) {
                    memcpy(blk.ptr, s_i2s_buffer, blk.len);
                }
                if (xQueueSend(s_i2s_queue, &blk, 0) != pdTRUE) {
                    (void)xQueueSend(s_i2s_free_queue, &pooled_ptr, 0);
                    s_audio_stats.buffer_overruns++;
                    backpressure = true;
                }
            } else {
                s_audio_stats.buffer_overruns++;
                backpressure = true;
            }
        } else {
            s_audio_stats.buffer_overruns++;
            backpressure = true;
        }

        audio_proc_mock_yield();
        if (s_force_synth || backpressure) {
            TickType_t delay_ticks = pdMS_TO_TICKS(1);
            if (delay_ticks == 0) {
                delay_ticks = 1;
            }
            vTaskDelay(delay_ticks);
        } else {
            taskYIELD();
        }
    }
}

/**
 * @brief Lower-priority worker that converts, resamples and pushes to ringbuffer
 */
static void audio_worker_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Audio worker task started");
    i2s_block_t blk = {0};

    while (1) {
        if (!s_is_running || !s_is_initialized) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        if (xQueueReceive(s_i2s_queue, &blk, pdMS_TO_TICKS(100)) != pdTRUE) {
            continue; // timeout
        }

        if (blk.ptr == NULL) {
            continue;
        }

        if (blk.synth_fill) {
            size_t target = blk.capacity;
            if (target > AUDIO_WORK_BUFFER_BYTES) {
                target = AUDIO_WORK_BUFFER_BYTES;
            }
            size_t generated = 0;
#if defined(CONFIG_AUDIO_USE_SYNTH_SOURCE)
            generated = synth_generate_audio(blk.ptr, target);
#elif defined(CONFIG_BT_MOCK_TESTING)
            generated = mock_generate_i2s_audio(blk.ptr, target);
#else
            if (target > 0) {
                memset(blk.ptr, 0, target);
            }
            generated = target;
#endif
            blk.len = generated;
            if (generated == 0) {
                if (s_i2s_free_queue != NULL) {
                    if (xQueueSend(s_i2s_free_queue, &blk.ptr, pdMS_TO_TICKS(10)) != pdTRUE) {
                        heap_caps_free(blk.ptr);
                    }
                } else {
                    heap_caps_free(blk.ptr);
                }
                blk.ptr = NULL;
                blk.len = 0;
                blk.capacity = 0;
                blk.synth_fill = false;
                continue;
            }
        }

        if (blk.len == 0) {
            if (s_i2s_free_queue != NULL) {
                if (xQueueSend(s_i2s_free_queue, &blk.ptr, pdMS_TO_TICKS(10)) != pdTRUE) {
                    heap_caps_free(blk.ptr);
                }
            } else {
                heap_caps_free(blk.ptr);
            }
            blk.ptr = NULL;
            blk.capacity = 0;
            blk.synth_fill = false;
            continue;
        }

        /* Convert */
        size_t conv_size = 0;
        esp_err_t cret = convert_audio_format(blk.ptr, s_proc_buffer, blk.len,
                                              s_audio_config.bit_depth, s_audio_config.bit_depth,
                                              &conv_size);
        if (cret != ESP_OK) {
            s_audio_stats.conversion_errors++;
            heap_caps_free(blk.ptr);
            blk.ptr = NULL;
            continue;
        }

        /* Resample */
        size_t res_size = 0;
        esp_err_t rret = resample_audio(s_proc_buffer, s_proc_buffer2, conv_size,
                                        s_audio_config.sample_rate, s_audio_config.sample_rate,
                                        &res_size);
        if (rret != ESP_OK) {
            s_audio_stats.conversion_errors++;
            heap_caps_free(blk.ptr);
            blk.ptr = NULL;
            continue;
        }

        /* Push to ringbuffer (xRingbufferSend copies data) */
        if (res_size > 0) {
            size_t free_size = xRingbufferGetCurFreeSize(s_audio_buffer);
            if (free_size < res_size) {
                s_audio_stats.buffer_overruns++;
                if (free_size < (res_size / 2)) {
                    /* Skip adding if buffer very full */
                    heap_caps_free(blk.ptr);
                    blk.ptr = NULL;
                    continue;
                }
            }

            BaseType_t sent = xRingbufferSend(s_audio_buffer, s_proc_buffer2, res_size, 0);
            if (sent != pdTRUE) {
                s_audio_stats.buffer_overruns++;
            }
        }

        /* Return the raw block to the free pool if present, otherwise free it */
        if (blk.ptr != NULL) {
            if (s_i2s_free_queue != NULL) {
                if (xQueueSend(s_i2s_free_queue, &blk.ptr, pdMS_TO_TICKS(10)) != pdTRUE) {
                    heap_caps_free(blk.ptr);
                }
            } else {
                heap_caps_free(blk.ptr);
            }
            blk.ptr = NULL;
            blk.len = 0;
            blk.capacity = 0;
            blk.synth_fill = false;
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
        /* Yield periodically while performing expensive resampling so the
         * RTOS can service the watchdog and other tasks. Processing large
         * buffers without yielding can trigger the task watchdog on idle
         * cores. We yield every 64 destination frames. */
        int yield_counter = 0;

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
            /* Periodically yield to keep the scheduler responsive. */
            if ((++yield_counter & 0x1) == 0) vTaskDelay(pdMS_TO_TICKS(1));
        }
    } else if (s_audio_config.bit_depth == AUDIO_BIT_DEPTH_32) {
        int32_t* src_samples = (int32_t*)src;
        int32_t* dst_samples = (int32_t*)dst;
        int yield_counter = 0;

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
            if ((++yield_counter & 0x3F) == 0) vTaskDelay(pdMS_TO_TICKS(1));
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

/**
 * @brief Diagnostic helper to check whether audio processing is active
 *
 * Returns true when the processor is in the running state (I2S RX enabled).
 */
bool audio_processor_is_running(void)
{
    return s_is_running;
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
