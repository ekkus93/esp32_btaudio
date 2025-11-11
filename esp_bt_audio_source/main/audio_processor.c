#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
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
#include "bt_manager.h"

/* Connection manager helper exposed from bt_connection_manager.c */
extern int bt_get_streaming_state_int(void);
#if (defined(CONFIG_SPIRAM) && CONFIG_SPIRAM) || (defined(CONFIG_SPIRAM_SUPPORT) && CONFIG_SPIRAM_SUPPORT)
#include "esp_psram.h"
#endif

/* One-shot diagnostic: when set, the next beep/fallback generation
 * will emit small PCM snapshots to the serial log for diagnosis and
 * then clear the flag. This is intentionally low-impact and only
 * intended for short debug sessions. */
static volatile bool s_dump_next_beep_diag = false;
static const size_t DIAG_DUMP_BYTES = 64; /* bytes to dump for snapshots */

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

/* Dedicated small buffer for urgent beep audio so short tones can be
 * delivered even when the main pipeline is congested. Keep modest to
 * limit DRAM pressure. Reduce to 8 KiB by default to avoid DRAM
 * allocation failures in the Bluetooth stack (see runtime logs). */
#define BEEP_BUFFER_SIZE (8 * 1024)
/* Fade duration (ms) applied to the start and end of queued/fallback beeps
 * Option A: small crossfade to reduce clicks. Reasonable default: 8 ms. */
#define BEEP_FADE_MS 8

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
/* Tunable I2S parameters to trade latency vs RAM. Lower per-read sizes
 * reduce blocking time in the reader; more descriptors give the DMA more
 * headroom without needing very large per-descriptor frames. Adjust if
 * you have PSRAM or different timing requirements. */
#define I2S_DEFAULT_DMA_DESC_NUM 6U
#define I2S_DEFAULT_DMA_FRAME_NUM 32U
#define I2S_MAX_READ_BYTES (4 * 1024)

// Ring buffer for audio data
static RingbufHandle_t s_audio_buffer = NULL;
// Small low-latency buffer for urgent beeps
static RingbufHandle_t s_beep_buffer = NULL;

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
/* Remember previous synth mode when we temporarily enable synth for
 * fallback beep playback. This allows the reader to generate samples
 * locally while the fallback tone is active (useful on DRAM-only
 * boards or when I2S reads time out). */
static bool s_beep_prev_force_synth = false;
/* Track consecutive I2S read failures across the reader task so other
 * code (like the fallback restore logic) can detect whether the I2S
 * path is healthy. This prevents restoring synth mode when I2S is
 * currently failing, which caused choppy audio. */
static int s_i2s_consecutive_failures = 0;

/* Fallback on-the-fly beep generator state
 * Used when the main ringbuffer is full and we still want to play a
 * short beep. The reader will synthesize samples directly from this
 * state prior to pulling from the ringbuffer. Protected by a
 * simple portMUX critical section. */
static bool s_beep_fallback_active = false;
static size_t s_beep_fallback_frames_remaining = 0;
static uint32_t s_beep_fallback_phase_acc = 0;
static uint32_t s_beep_fallback_phase_step = 0;
/* Floating-point phase helpers for sine generation (used for nicer beep) */
static double s_synth_phase = 0.0;
static double s_beep_fallback_phase = 0.0;
static double s_beep_fallback_phase_inc = 0.0;
/* Track total frames scheduled for the fallback so we can compute
 * envelope progress for fade-in/out. This accumulates when multiple
 * beeps are enabled while a previous fallback is active. */
static size_t s_beep_fallback_total_frames = 0;
static portMUX_TYPE s_beep_lock = portMUX_INITIALIZER_UNLOCKED;

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

/* Helper to log heap free sizes for DRAM and PSRAM (when available).
 * Call this at diagnostic points to observe allocator pressure during
 * operations that previously triggered BT malloc failures. */
static void log_heap_stats(const char *ctx)
{
    size_t free_dram = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    size_t free_8bit = heap_caps_get_free_size(MALLOC_CAP_8BIT);
#if (defined(CONFIG_SPIRAM) && CONFIG_SPIRAM) || (defined(CONFIG_SPIRAM_SUPPORT) && CONFIG_SPIRAM_SUPPORT)
    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG, "%s: heap free DRAM=%zu 8BIT=%zu PSRAM=%zu", ctx, free_dram, free_8bit, free_psram);
#else
    ESP_LOGI(TAG, "%s: heap free DRAM=%zu 8BIT=%zu", ctx, free_dram, free_8bit);
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
    (void)config;
#else
    (void)config;
#endif
    size_t capacity = AUDIO_BUFFER_SIZE;
    if (capacity < AUDIO_WORK_BUFFER_BYTES) {
        capacity = AUDIO_WORK_BUFFER_BYTES;
    }

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

/* Diagnostic helper forward-declaration (defined later) */
static void diag_dump_bytes(const void* data, size_t len, const char* tag);

/* Runtime control: allow forcing DRAM-only allocations for debugging/static avoidance */
/* Temporarily default to true for Option 2: boot with DRAM-only allocations
 * (this avoids PSRAM placement for audio buffers so we can A/B the beep).
 * Revert this change after debugging or make persistent via Kconfig/NVS as needed. */
static bool s_dram_only_alloc = true;

void audio_processor_set_dram_only(bool enable)
{
    s_dram_only_alloc = enable ? true : false;
    ESP_LOGI(TAG, "audio_processor: DRAM-only allocations %s", s_dram_only_alloc ? "ENABLED" : "DISABLED");
}

/* Public helper to arm a one-shot diagnostic dump for the next beep
 * invocation. Call this before you issue `BEEP` to capture snapshots. */
void audio_processor_enable_next_beep_diag(void);

/*
 * Helper: attempt to enqueue `total_bytes` from `data` into the small
 * low-latency `s_beep_buffer` in aligned sub-chunks. This avoids trying
 * to push large contiguous blocks that are bigger than the beep buffer
 * capacity and reduces the chance we'll fall back to the on-the-fly
 * synth generator.
 *
 * Returns number of bytes successfully enqueued.
 */
static size_t beep_buffer_send_chunked(const uint8_t* data, size_t total_bytes, size_t frame_bytes)
{
    if (s_beep_buffer == NULL || data == NULL || total_bytes == 0 || frame_bytes == 0) return 0;

    /* Choose a small chunk size friendly to the beep buffer and DMA. Use
     * 4 KiB as a reasonable default and align down to frame size. */
    size_t chunk_bytes = 4 * 1024;
    if (chunk_bytes > (size_t)BEEP_BUFFER_SIZE) chunk_bytes = (size_t)BEEP_BUFFER_SIZE;
    /* Align chunk to frame size */
    chunk_bytes = (chunk_bytes / frame_bytes) * frame_bytes;
    if (chunk_bytes == 0) chunk_bytes = frame_bytes;

    size_t sent_total = 0;

    /* Short per-chunk wait to let the consumer free space */
    const TickType_t chunk_wait = pdMS_TO_TICKS(10);

    while (sent_total < total_bytes) {
        size_t remaining = total_bytes - sent_total;
        size_t this_chunk = remaining > chunk_bytes ? chunk_bytes : remaining;

        BaseType_t ok = xRingbufferSend(s_beep_buffer, data + sent_total, this_chunk, chunk_wait);
        if (ok == pdTRUE) {
            sent_total += this_chunk;
            continue;
        }

        /* If direct send failed, try freeing a little space by dropping
         * oldest items from the beep buffer (bounded attempts). */
        size_t dropped = 0;
        int drop_attempts = 0;
        const int max_drop_attempts = 4;
        while (xRingbufferGetCurFreeSize(s_beep_buffer) < this_chunk && drop_attempts < max_drop_attempts) {
            size_t rsz = 0;
            void* it = xRingbufferReceiveUpTo(s_beep_buffer, &rsz, this_chunk, 0);
            if (it == NULL || rsz == 0) break;
            vRingbufferReturnItem(s_beep_buffer, it);
            dropped += rsz;
            drop_attempts++;
        }

        if (dropped > 0) {
            log_heap_stats("beep-after-drop-beepbuf");
            ok = xRingbufferSend(s_beep_buffer, data + sent_total, this_chunk, 0);
            if (ok == pdTRUE) {
                sent_total += this_chunk;
                continue;
            }
        }

        /* Couldn't send this chunk promptly; give up to avoid blocking
         * the caller for long periods. Higher-level code will enable the
         * fallback synth for any remaining bytes. */
        break;
    }

    return sent_total;
}

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

    /* Use a sine wave for a smoother tone. Compute phase increment in
     * radians and maintain a persistent floating-point phase accumulator
     * to avoid quantization artifacts. Amplitude chosen to fit in 16-bit
     * while leaving headroom for downstream processing. */
    const double two_pi = 2.0 * M_PI;
    double phase_inc = (sample_rate > 0.0f) ? ((two_pi * freq) / (double)sample_rate) : ((two_pi * freq) / 44100.0);

    if (s_audio_config.bit_depth == AUDIO_BIT_DEPTH_16) {
        int16_t* out = (int16_t*)buffer;
        const double amp = 12000.0; /* amplitude for 16-bit */
        for (size_t f = 0; f < frames_to_write; ++f) {
            int16_t sample = (int16_t)(sinf(s_synth_phase) * amp);
            for (int ch = 0; ch < channels; ++ch) {
                *out++ = sample;
            }
            s_synth_phase += phase_inc;
            if (s_synth_phase >= two_pi) s_synth_phase -= two_pi;
        }
        return frames_to_write * frame_bytes;
    } else {
        /* 32-bit container (32-bit or 24-bit stored in 32-bit) */
        int32_t* out32 = (int32_t*)buffer;
        const double amp32 = 12000.0 * (1 << 16);
        for (size_t f = 0; f < frames_to_write; ++f) {
            int32_t sample = (int32_t)(sinf(s_synth_phase) * amp32);
            for (int ch = 0; ch < channels; ++ch) {
                *out32++ = sample;
            }
            s_synth_phase += phase_inc;
            if (s_synth_phase >= two_pi) s_synth_phase -= two_pi;
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

    /* Detect runtime PSRAM availability once so we can adjust DRAM-heavy
     * allocations on DRAM-only boards. If PSRAM is absent, reduce our
     * initial sizing targets to relieve allocator pressure (helps avoid
     * BT stack malloc failures seen when starting streaming). */
    bool runtime_psram_ready = false;
#if (defined(CONFIG_SPIRAM) && CONFIG_SPIRAM) || (defined(CONFIG_SPIRAM_SUPPORT) && CONFIG_SPIRAM_SUPPORT)
    runtime_psram_ready = esp_psram_is_initialized();
#endif
    /* Honor runtime override to force DRAM-only allocations for debugging */
    if (s_dram_only_alloc) {
        runtime_psram_ready = false;
        ESP_LOGI(TAG, "audio_processor: runtime DRAM-only override active; PSRAM will not be used");
    }
    if (!runtime_psram_ready) {
        ESP_LOGW(TAG, "Runtime: PSRAM not available — reducing DRAM allocation targets to relieve pressure");
    }

#ifdef CONFIG_SPIRAM
    ESP_LOGI(TAG, "Build-time: SPIRAM enabled - AUDIO_BUFFER_SIZE=%zu, buffer_capacity=%zu", (size_t)AUDIO_BUFFER_SIZE, buffer_capacity);
#else
    ESP_LOGI(TAG, "Build-time: SPIRAM disabled - using DRAM-safe AUDIO_BUFFER_SIZE=%zu, buffer_capacity=%zu", (size_t)AUDIO_BUFFER_SIZE, buffer_capacity);
#endif

    // Create audio buffer. Try progressively smaller sizes at runtime if
    // allocation fails (useful for DRAM-only boards where the compile-time
    // DRAM-safe fallback may still be too large).
    size_t try_capacity = buffer_capacity;
    /* Keep the initial target at the full computed capacity; the retry loop
     * below will progressively reduce the size if allocations fail. This
     * avoids preemptively constraining DRAM-only boards to 32 KiB and gives
     * more headroom for larger resampled audio blocks. */
    const size_t min_floor = (AUDIO_WORK_BUFFER_BYTES > (4U * 1024U)) ? AUDIO_WORK_BUFFER_BYTES : (4U * 1024U);

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

        /* Emit heap free diagnostics so we can see DRAM/PSRAM pressure in
         * the logs. This helps triage malloc failures observed in the
         * Bluetooth stack when large allocations occur. */
        size_t free_dram = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
        size_t free_psram = 0;
#if (defined(CONFIG_SPIRAM) && CONFIG_SPIRAM) || (defined(CONFIG_SPIRAM_SUPPORT) && CONFIG_SPIRAM_SUPPORT)
        free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
#endif
        ESP_LOGI(TAG, "Attempting to create audio buffer of %zu bytes (heap free DRAM=%zu PSRAM=%zu)", try_capacity, free_dram, free_psram);

        /* Prefer creating the large audio buffer in PSRAM when available to
         * reduce DRAM pressure. Fall back to the default allocator if PSRAM
         * creation fails or isn't present. */
    bool psram_ready_local = false;
#if (defined(CONFIG_SPIRAM) && CONFIG_SPIRAM) || (defined(CONFIG_SPIRAM_SUPPORT) && CONFIG_SPIRAM_SUPPORT)
    psram_ready_local = esp_psram_is_initialized();
#endif
    if (s_dram_only_alloc) psram_ready_local = false;
        if (psram_ready_local) {
            s_audio_buffer = xRingbufferCreateWithCaps(try_capacity, RINGBUF_TYPE_BYTEBUF,
                                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (s_audio_buffer != NULL) {
                ESP_LOGI(TAG, "Audio buffer created in PSRAM (%zu bytes)", try_capacity);
                break;
            }
            ESP_LOGW(TAG, "xRingbufferCreateWithCaps(PSRAM) failed for %zu, falling back to default allocator", try_capacity);
        }

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

    /* Create small beep buffer for urgent tones. Prefer allocating the
     * ring buffer storage in PSRAM (if available) to reduce DRAM pressure
     * that can lead to bluetooth stack malloc failures. Fall back to the
     * default allocator if PSRAM is not present or allocation fails. This
     * buffer is non-fatal - if creation fails we will still use the
     * on-the-fly synth fallback. */
    s_beep_buffer = NULL;
    bool psram_ready_local = false;
#if (defined(CONFIG_SPIRAM) && CONFIG_SPIRAM) || (defined(CONFIG_SPIRAM_SUPPORT) && CONFIG_SPIRAM_SUPPORT)
    psram_ready_local = esp_psram_is_initialized();
#endif
    if (s_dram_only_alloc) psram_ready_local = false;
    if (psram_ready_local) {
        /* Try to place beep buffer storage in PSRAM to avoid DRAM pressure */
        s_beep_buffer = xRingbufferCreateWithCaps((size_t)BEEP_BUFFER_SIZE, RINGBUF_TYPE_BYTEBUF,
                                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_beep_buffer != NULL) {
            ESP_LOGI(TAG, "Beep buffer created in PSRAM (%u bytes)", (unsigned)BEEP_BUFFER_SIZE);
        } else {
            ESP_LOGW(TAG, "audio_processor_init: failed to create beep buffer in PSRAM, falling back to default allocator");
        }
    }

    if (s_beep_buffer == NULL) {
        s_beep_buffer = xRingbufferCreate((size_t)BEEP_BUFFER_SIZE, RINGBUF_TYPE_BYTEBUF);
        if (s_beep_buffer == NULL) {
            ESP_LOGW(TAG, "audio_processor_init: failed to create beep buffer (%u bytes)", (unsigned)BEEP_BUFFER_SIZE);
        } else {
            ESP_LOGI(TAG, "Beep buffer created (%u bytes)", (unsigned)BEEP_BUFFER_SIZE);
        }
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
    /* Lower work buffer sizing on DRAM-only systems to reduce resident
     * DRAM usage (we'll still try progressively smaller sizes). */
    if (!runtime_psram_ready && try_work_bytes > 4096) {
        try_work_bytes = try_work_bytes / 2U;
    }
    /* Minimum per-work-buffer size. Lower this in the mock/unit-test build
     * to accommodate DRAM-only test images which have a much smaller heap
     * budget. Production builds keep the 4 KiB floor. */
#ifdef CONFIG_BT_MOCK_TESTING
    const size_t min_work_bytes = 1 * 1024; // 1KB minimum per buffer for unit tests
#else
    const size_t min_work_bytes = 4 * 1024; // 4KB minimum per buffer
#endif
    bool work_allocated = false;

    while (try_work_bytes >= min_work_bytes) {
        ESP_LOGI(TAG, "Attempting audio work buffers of %zu bytes each (combined allocation)", try_work_bytes);

        size_t combined = try_work_bytes * 3U;

        /* Try a single contiguous allocation first (prefer PSRAM unless
         * the runtime DRAM-only override is set). This reduces fragmentation
         * and the number of heap headers compared to three separate allocations. */
        if (!s_dram_only_alloc) {
            s_work_block = heap_caps_malloc(combined, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (s_work_block == NULL) {
                ESP_LOGW(TAG, "PSRAM combined allocation failed; falling back to DRAM for %zu bytes", combined);
                s_work_block = heap_caps_malloc(combined, MALLOC_CAP_DEFAULT | MALLOC_CAP_8BIT);
            }
        } else {
            /* Forced DRAM-only path */
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

#if (defined(CONFIG_SPIRAM) && CONFIG_SPIRAM) || (defined(CONFIG_SPIRAM_SUPPORT) && CONFIG_SPIRAM_SUPPORT)
    bool psram_ready = esp_psram_is_initialized();
    if (s_dram_only_alloc) psram_ready = false;
    size_t pool_target = I2S_RAW_POOL_DEFAULT_COUNT;
    if (!psram_ready) {
        /* On DRAM-only devices reduce the prealloc pool to a single block to
         * avoid large upfront DRAM consumption. The reader will fall back to
         * on-demand heap allocations if needed. */
        pool_target = 3U;
    }
#else
    const bool psram_ready = false;
    size_t pool_target = I2S_RAW_POOL_DRAM_COUNT;
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
                void* blk = NULL;
                if (!s_dram_only_alloc) {
                    blk = heap_caps_malloc(AUDIO_WORK_BUFFER_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                }
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
                                     AUDIO_PROCESSING_STACK_SIZE, NULL, 7, &s_audio_task_handle);
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

    /* Delete beep buffer if present */
    if (s_beep_buffer != NULL) {
        vRingbufferDelete(s_beep_buffer);
        s_beep_buffer = NULL;
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

    // If muted and no beep override/fallback is pending, just fill with zeros
    if (s_audio_config.mute && s_beep_remaining_bytes == 0 && !s_beep_fallback_active) {
        /* If the dedicated beep buffer is empty (or unavailable) then
         * there's nothing to play — return silence. If the beep buffer
         * contains data we should continue so the urgent tone can play. */
        if (s_beep_buffer == NULL || xRingbufferGetCurFreeSize(s_beep_buffer) == BEEP_BUFFER_SIZE) {
            memset(buffer, 0, size);
            *bytes_read = size;
            return ESP_OK;
        }
    }

    // Prepare to fill output buffer. First, synthesize any on-the-fly
    // fallback beep samples (these are generated when ringbuffer was full
    // at enqueue time). This path is protected by s_beep_lock.
    size_t bytes_written = 0;
    int bytes_per_sample = audio_bytes_per_sample(s_audio_config.bit_depth);
    if (bytes_per_sample <= 0) bytes_per_sample = 2;
    int sample_rate = s_audio_config.sample_rate;
    int channels = s_audio_config.channels;
    if (channels != AUDIO_CHANNEL_MONO && channels != AUDIO_CHANNEL_STEREO) channels = AUDIO_CHANNEL_STEREO;
    size_t frame_bytes = (size_t)bytes_per_sample * (size_t)channels;

    /* Drain any urgent beep data from the small beep buffer first so queued
     * tones are emitted with lower latency than the main ringbuffer. */
    if (s_beep_buffer != NULL) {
        while (bytes_written < size) {
            size_t want = size - bytes_written;
            size_t read_sz = 0;
            void* itm = xRingbufferReceiveUpTo(s_beep_buffer, &read_sz, want, 0);
            if (itm == NULL || read_sz == 0) break;
            memcpy(buffer + bytes_written, itm, read_sz);
            vRingbufferReturnItem(s_beep_buffer, itm);
            /* Decrement bypass counter for beep bytes consumed */
            if (s_beep_remaining_bytes > 0) {
                if (read_sz >= s_beep_remaining_bytes) s_beep_remaining_bytes = 0;
                else s_beep_remaining_bytes -= read_sz;
            }
            bytes_written += read_sz;
        }
    }

    if (s_beep_fallback_active) {
        bool _beep_need_restore_synth = false;
        portENTER_CRITICAL(&s_beep_lock);
        if (s_beep_fallback_active && s_beep_fallback_frames_remaining > 0) {
            size_t max_frames = size / frame_bytes;
            size_t emit_frames = s_beep_fallback_frames_remaining < max_frames ? s_beep_fallback_frames_remaining : max_frames;
            if (emit_frames > 0) {
                /* Compute how many frames have already been played from
                 * the fallback so we can apply the same fade envelope.
                 * Guard against accidental underflow if totals were not
                 * initialized properly. */
                size_t frames_played = 0;
                if (s_beep_fallback_total_frames > s_beep_fallback_frames_remaining) {
                    frames_played = s_beep_fallback_total_frames - s_beep_fallback_frames_remaining;
                }
                size_t fade_frames = (size_t)(((double)sample_rate * (double)BEEP_FADE_MS) / 1000.0);
                if (fade_frames < 1) fade_frames = 1;

                if (s_audio_config.bit_depth == AUDIO_BIT_DEPTH_16) {
                    int16_t* out = (int16_t*)buffer;
                    const double amp = 30000.0;
                    const double two_pi = 2.0 * M_PI;
                    for (size_t f = 0; f < emit_frames; ++f) {
                        size_t gidx = frames_played + f; /* global index into total frames */
                        double env = 1.0;
                        if (gidx < fade_frames) {
                            env = (double)gidx / (double)fade_frames;
                        } else if (gidx + fade_frames > s_beep_fallback_total_frames) {
                            size_t tail_idx = s_beep_fallback_total_frames > gidx ? (s_beep_fallback_total_frames - gidx) : 0;
                            if (tail_idx < fade_frames) env = (double)tail_idx / (double)fade_frames;
                        }
                        double v = sin(s_beep_fallback_phase) * amp * env;
                        int16_t sample = (int16_t)v;
                        for (int ch = 0; ch < channels; ++ch) {
                            *out++ = sample;
                        }
                        s_beep_fallback_phase += s_beep_fallback_phase_inc;
                        if (s_beep_fallback_phase >= two_pi) s_beep_fallback_phase -= two_pi;
                    }
                } else {
                    int32_t* out32 = (int32_t*)buffer;
                    const double amp32 = 30000.0 * (1 << 16);
                    const double two_pi = 2.0 * M_PI;
                    for (size_t f = 0; f < emit_frames; ++f) {
                        size_t gidx = frames_played + f;
                        double env = 1.0;
                        if (gidx < fade_frames) {
                            env = (double)gidx / (double)fade_frames;
                        } else if (gidx + fade_frames > s_beep_fallback_total_frames) {
                            size_t tail_idx = s_beep_fallback_total_frames > gidx ? (s_beep_fallback_total_frames - gidx) : 0;
                            if (tail_idx < fade_frames) env = (double)tail_idx / (double)fade_frames;
                        }
                        double v = sin(s_beep_fallback_phase) * amp32 * env;
                        int32_t sample = (int32_t)v;
                        for (int ch = 0; ch < channels; ++ch) {
                            *out32++ = sample;
                        }
                        s_beep_fallback_phase += s_beep_fallback_phase_inc;
                        if (s_beep_fallback_phase >= two_pi) s_beep_fallback_phase -= two_pi;
                    }
                }

                size_t emitted_bytes = emit_frames * frame_bytes;
                s_beep_fallback_frames_remaining -= emit_frames;
                if (s_beep_fallback_frames_remaining == 0) {
                    s_beep_fallback_active = false;
                    s_beep_fallback_total_frames = 0; /* reset totals when done */
                    /* Restore previous synth mode when the fallback has fully
                     * completed. We set a flag here while still in the
                     * critical section and perform the actual restore after
                     * leaving the critical region to avoid holding the lock
                     * while performing non-trivial operations. */
                    _beep_need_restore_synth = true;
                }
                /* Decrement the global remaining bytes used to bypass mute */
                if (s_beep_remaining_bytes > emitted_bytes) s_beep_remaining_bytes -= emitted_bytes;
                else s_beep_remaining_bytes = 0;

                /* If diagnostics are enabled for the next beep, emit a
                 * compact hex snapshot of the generated fallback samples
                 * for offline inspection. Consume the one-shot flag so
                 * it only affects a single beep. */
                if (s_dump_next_beep_diag) {
                    size_t dump = emitted_bytes < DIAG_DUMP_BYTES ? emitted_bytes : DIAG_DUMP_BYTES;
                    /* Dump the newly-generated portion starting at the
                     * current write offset. */
                    diag_dump_bytes(buffer + bytes_written, dump, "DIAG:fallback-out");
                    s_dump_next_beep_diag = false;
                }

                bytes_written += emitted_bytes;
            }
        }
        portEXIT_CRITICAL(&s_beep_lock);
        if (_beep_need_restore_synth) {
            /* Restore the synth mode that was active before we forced
             * synth for fallback playback. */
            s_force_synth = s_beep_prev_force_synth;
            ESP_LOGI(TAG, "audio_processor_beep: fallback finished, restored synth mode=%s", s_force_synth ? "ENABLED" : "DISABLED");
        }
    }

    /* If we filled the requested size entirely from the fallback generator,
     * apply volume and return immediately. */
    if (bytes_written == size) {
        if (s_volume_gain < 100) apply_volume(buffer, bytes_written, s_volume_gain);
        *bytes_read = bytes_written;
        return ESP_OK;
    }

    /* Otherwise, try to pull the remainder from the ringbuffer. */
    size_t want = size - bytes_written;
    size_t read_size = 0;
    void* item = xRingbufferReceiveUpTo(s_audio_buffer, &read_size, want, 0);
    if (item != NULL && read_size > 0) {
        memcpy(buffer + bytes_written, item, read_size);
        vRingbufferReturnItem(s_audio_buffer, item);

        /* If part of this data was scheduled as a beep (queued earlier),
         * decrement remaining counter so subsequent reads know when to
         * re-enable mute behavior. */
        if (s_beep_remaining_bytes > 0) {
            if (read_size >= s_beep_remaining_bytes) s_beep_remaining_bytes = 0;
            else s_beep_remaining_bytes -= read_size;
        }

        bytes_written += read_size;
    } else {
        /* No ringbuffer data available for the remainder; if we have already
         * generated some fallback samples return them (silence-fill the
         * rest for caller expectations). If nothing generated, report underrun. */
        if (bytes_written == 0) {
            *bytes_read = 0;
            s_audio_stats.buffer_underruns++;
            return ESP_OK;
        }
        /* Zero-fill remaining tail so output buffer is deterministic */
        memset(buffer + bytes_written, 0, want);
        bytes_written += want;
    }

    // Apply volume if not at maximum
    if (s_volume_gain < 100) {
        apply_volume(buffer, bytes_written, s_volume_gain);
    }

    *bytes_read = bytes_written;

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
        .dma_desc_num = I2S_DEFAULT_DMA_DESC_NUM,
        .dma_frame_num = I2S_DEFAULT_DMA_FRAME_NUM,
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

esp_err_t audio_processor_drain_ringbuffer(void)
{
    if (!s_is_initialized) return ESP_ERR_INVALID_STATE;
    if (s_audio_buffer == NULL) return ESP_ERR_INVALID_STATE;

    size_t rsz = 0;
    void* it = NULL;
    /* Non-blocking drain: repeatedly receive any available items and
     * return them so the ringbuffer frees its memory. Limit loop to a
     * reasonable number of iterations to avoid starving other tasks. */
    int drained = 0;
    const int max_drains = 256;
    while (drained < max_drains) {
        it = xRingbufferReceiveUpTo(s_audio_buffer, &rsz, 0, 0);
        if (it == NULL || rsz == 0) break;
        vRingbufferReturnItem(s_audio_buffer, it);
        drained++;
    }
    ESP_LOGI(TAG, "audio_processor_drain_ringbuffer: drained %d items", drained);
    return ESP_OK;
}

/**
 * @brief Emit a synchronous worker-like diagnostic snapshot.
 *
 * This function generates a short block of audio using the same synth or
 * mock generator used by the reader/worker, runs the conversion/resample
 * helpers inline, and emits a DIAG:worker-out hex dump of the resulting
 * samples. It is intentionally bounded to avoid long blocking calls.
 */
esp_err_t audio_processor_emit_sync_worker_diag(void)
{
    if (!s_is_initialized) return ESP_ERR_INVALID_STATE;

    size_t target = AUDIO_WORK_BUFFER_BYTES;
    if (target == 0) return ESP_ERR_INVALID_ARG;

    size_t generated = 0;

    /* Prefer using the same generator the worker uses: synth if enabled,
     * or the mock generator for unit tests. If neither is available,
     * synthesize the same fallback/beep tone so the snapshot is
     * representative of an actual audio tone rather than a memset test
     * pattern. */
#if defined(CONFIG_AUDIO_USE_SYNTH_SOURCE)
    generated = synth_generate_audio(s_proc_buffer, target);
#elif defined(CONFIG_BT_MOCK_TESTING)
    generated = mock_generate_i2s_audio(s_proc_buffer, target);
#else
    /* Generate a sine-wave tone (1kHz) so the diagnostic snapshot is
     * musically pleasant and representative of real audio output. Fill
     * s_proc_buffer according to bit depth and channel count. */
    int sample_rate = s_audio_config.sample_rate > 0 ? s_audio_config.sample_rate : 44100;
    const int tone_hz = 1000;
    size_t bytes_per_sample = (size_t)audio_bytes_per_sample(s_audio_config.bit_depth);
    if (bytes_per_sample == 0) bytes_per_sample = 2;
    int channels = (s_audio_config.channels == AUDIO_CHANNEL_MONO) ? 1 : 2;
    size_t frame_bytes = bytes_per_sample * (size_t)channels;
    size_t max_frames = target / frame_bytes;
    if (max_frames == 0) max_frames = 1;
    /* Use sine wave generation for nicer tone */
    const double two_pi = 2.0 * M_PI;
    double phase = 0.0;
    double phase_inc = (sample_rate > 0) ? ((two_pi * (double)tone_hz) / (double)sample_rate) : ((two_pi * (double)tone_hz) / 44100.0);
    /* Apply a short fade-in/out envelope to reduce transient clicks. Use
     * the same fade duration as the runtime beep/fallback so diagnostics
     * and live playback match. */
    size_t fade_frames = (size_t)(((double)sample_rate * (double)BEEP_FADE_MS) / 1000.0);
    if (fade_frames < 1) fade_frames = 1;

    if (s_audio_config.bit_depth == AUDIO_BIT_DEPTH_16) {
        int16_t* out = (int16_t*)s_proc_buffer;
        const double amp = 30000.0;
        for (size_t f = 0; f < max_frames; ++f) {
            double env = 1.0;
            if (f < fade_frames) {
                env = (double)f / (double)fade_frames;
            } else if (f + fade_frames > max_frames) {
                /* trailing fade */
                size_t tail_idx = max_frames - f;
                if (tail_idx < fade_frames) env = (double)tail_idx / (double)fade_frames;
            }
            double v = sin(phase) * amp * env;
            int16_t sample = (int16_t)v;
            for (int ch = 0; ch < channels; ++ch) {
                *out++ = sample;
            }
            phase += phase_inc;
            if (phase >= two_pi) phase -= two_pi;
        }
        generated = max_frames * frame_bytes;
    } else {
        int32_t* out32 = (int32_t*)s_proc_buffer;
        const double amp32 = 30000.0 * (1 << 16);
        for (size_t f = 0; f < max_frames; ++f) {
            double env = 1.0;
            if (f < fade_frames) {
                env = (double)f / (double)fade_frames;
            } else if (f + fade_frames > max_frames) {
                size_t tail_idx = max_frames - f;
                if (tail_idx < fade_frames) env = (double)tail_idx / (double)fade_frames;
            }
            double v = sin(phase) * amp32 * env;
            int32_t sample = (int32_t)v;
            for (int ch = 0; ch < channels; ++ch) {
                *out32++ = sample;
            }
            phase += phase_inc;
            if (phase >= two_pi) phase -= two_pi;
        }
        generated = max_frames * frame_bytes;
    }
#endif

    if (generated == 0) return ESP_ERR_INVALID_SIZE;

    size_t conv_size = 0;
    size_t res_size = 0;

    /* Convert/resample to the worker output format (no-op if identical) */
    if (convert_audio_format(s_proc_buffer, s_proc_buffer, generated, s_audio_config.bit_depth, s_audio_config.bit_depth, &conv_size) != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    if (conv_size == 0) return ESP_ERR_INVALID_SIZE;

    if (resample_audio(s_proc_buffer, s_proc_buffer2, conv_size, s_audio_config.sample_rate, s_audio_config.sample_rate, &res_size) != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    if (res_size == 0) return ESP_ERR_INVALID_SIZE;

    size_t dump = res_size < DIAG_DUMP_BYTES ? res_size : DIAG_DUMP_BYTES;
    diag_dump_bytes(s_proc_buffer2, dump, "DIAG:worker-out");
    return ESP_OK;
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

    /* Diagnostic: snapshot free space and heap before starting attempts */
    size_t free_before_all = xRingbufferGetCurFreeSize(s_audio_buffer);
    ESP_LOGI(TAG, "audio_processor_beep: attempting to queue %zu bytes (%u ms), free_space=%zu",
             total_bytes, (unsigned)duration_ms, free_before_all);
    log_heap_stats("beep-start");

    size_t frames_generated = 0;
    while (bytes_remaining > 0) {
        size_t chunk = bytes_remaining > max_chunk ? max_chunk : bytes_remaining;
        size_t frames = chunk / frame_bytes;

        /* Fill the proc buffer with the tone samples. Support 16-bit and
         * 32-bit containers (24-bit stored in 32-bit). Other depths fall
         * back to a 16-bit-like representation. */
    /* Apply envelope across the whole beep duration to avoid clicks.
     * Compute fade frames from BEEP_FADE_MS and apply according to the
     * global frame index (frames_generated + f). Use the already-computed
     * `total_frames` above. */
    size_t fade_frames = (size_t)(((double)sample_rate * (double)BEEP_FADE_MS) / 1000.0);
        if (fade_frames < 1) fade_frames = 1;

        if (s_audio_config.bit_depth == AUDIO_BIT_DEPTH_16) {
            int16_t* out = (int16_t*)s_proc_buffer;
            const double two_pi = 2.0 * M_PI;
            double phase = 0.0;
            double phase_inc = (sample_rate > 0) ? ((two_pi * (double)tone_hz) / (double)sample_rate) : ((two_pi * (double)tone_hz) / 44100.0);
            const double amp = 30000.0;
            for (size_t f = 0; f < frames; ++f) {
                size_t gidx = frames_generated + f; /* global frame index */
                double env = 1.0;
                if (gidx < fade_frames) {
                    env = (double)gidx / (double)fade_frames;
                } else if (gidx + fade_frames > total_frames) {
                    size_t tail_idx = total_frames > gidx ? (total_frames - gidx) : 0;
                    if (tail_idx < fade_frames) env = (double)tail_idx / (double)fade_frames;
                }
                double v = sin(phase) * amp * env;
                int16_t sample = (int16_t)v;
                for (int ch = 0; ch < channels; ++ch) {
                    *out++ = sample;
                }
                phase += phase_inc;
                if (phase >= two_pi) phase -= two_pi;
            }
        } else {
            /* 32-bit container (32- or 24-bit) */
            int32_t* out32 = (int32_t*)s_proc_buffer;
            const double two_pi = 2.0 * M_PI;
            double phase = 0.0;
            double phase_inc = (sample_rate > 0) ? ((two_pi * (double)tone_hz) / (double)sample_rate) : ((two_pi * (double)tone_hz) / 44100.0);
            const double amp32 = 30000.0 * (1 << 16);
            for (size_t f = 0; f < frames; ++f) {
                size_t gidx = frames_generated + f;
                double env = 1.0;
                if (gidx < fade_frames) {
                    env = (double)gidx / (double)fade_frames;
                } else if (gidx + fade_frames > total_frames) {
                    size_t tail_idx = total_frames > gidx ? (total_frames - gidx) : 0;
                    if (tail_idx < fade_frames) env = (double)tail_idx / (double)fade_frames;
                }
                double v = sin(phase) * amp32 * env;
                int32_t sample = (int32_t)v;
                for (int ch = 0; ch < channels; ++ch) {
                    *out32++ = sample;
                }
                phase += phase_inc;
                if (phase >= two_pi) phase -= two_pi;
            }
        }

        /* Try to push into the ring buffer. Under load the worker may not
         * have freed space yet; instead of failing immediately, perform a
         * small number of short waits to allow the worker to make room. If
         * still unsuccessful, bail and report what was queued. */
        BaseType_t sent = pdFALSE;
        const TickType_t wait_ticks = pdMS_TO_TICKS(50);
        const int max_attempts = 3;
        int attempt = 0;
            while (attempt < max_attempts) {
            size_t free_before_attempt = xRingbufferGetCurFreeSize(s_audio_buffer);
            ESP_LOGD(TAG, "audio_processor_beep: attempt %d/%d frames=%zu bytes=%zu free_before_attempt=%zu",
                     attempt + 1, max_attempts, frames, (size_t)frames * frame_bytes, free_before_attempt);

            sent = xRingbufferSend(s_audio_buffer, s_proc_buffer, (size_t)frames * frame_bytes, wait_ticks);

            size_t free_after_attempt = xRingbufferGetCurFreeSize(s_audio_buffer);
            if (sent == pdTRUE) {
                ESP_LOGD(TAG, "audio_processor_beep: send succeeded on attempt %d, free_after=%zu",
                         attempt + 1, free_after_attempt);
                /* If diagnostics were armed, synthesize the worker's
                 * post-conversion/resample output for this chunk and
                 * emit a DIAG snapshot. This mirrors the work the
                 * lower-priority worker would do but runs inline for
                 * the first chunk so captures are available even when
                 * the worker path is delayed. Keep the work bounded by
                 * operating on at most AUDIO_WORK_BUFFER_BYTES. */
                if (s_dump_next_beep_diag) {
                    size_t conv_size = 0;
                    size_t res_size = 0;
                    /* Convert proc buffer to work buffer (uses same helper) */
                    if (convert_audio_format(s_proc_buffer, s_proc_buffer, chunk, s_audio_config.bit_depth, s_audio_config.bit_depth, &conv_size) == ESP_OK && conv_size > 0) {
                        if (resample_audio(s_proc_buffer, s_proc_buffer2, conv_size, s_audio_config.sample_rate, s_audio_config.sample_rate, &res_size) == ESP_OK && res_size > 0) {
                            size_t dump = res_size < DIAG_DUMP_BYTES ? res_size : DIAG_DUMP_BYTES;
                            diag_dump_bytes(s_proc_buffer2, dump, "DIAG:worker-out");
                        }
                    }
                    s_dump_next_beep_diag = false;
                }
                break;
            }

            ESP_LOGW(TAG, "audio_processor_beep: ringbuffer send attempt %d/%d failed (free_before=%zu free_after=%zu), retrying",
                     attempt + 1, max_attempts, free_before_attempt, free_after_attempt);
            attempt++;
        }
        if (sent != pdTRUE) {
            size_t free_now = xRingbufferGetCurFreeSize(s_audio_buffer);
            ESP_LOGW(TAG, "audio_processor_beep: ringbuffer send failed after %d attempts, free_now=%zu; attempting to free space by dropping oldest items",
                     attempt, free_now);
            log_heap_stats("beep-send-failed");

            /* Attempt to free space by discarding oldest ringbuffer items.
             * This is a last-resort short-path so short beeps can be injected
             * even when the main buffer is saturated (for example when
             * connected but the remote is not actively pulling audio). */
            size_t needed = (size_t)frames * frame_bytes;
            size_t dropped = 0;
            int drop_attempts = 0;
            const int max_drop_attempts = 32; /* avoid unbounded loops */
            while (xRingbufferGetCurFreeSize(s_audio_buffer) < needed && drop_attempts < max_drop_attempts) {
                size_t rsz = 0;
                void* it = xRingbufferReceiveUpTo(s_audio_buffer, &rsz, needed, 0);
                if (it == NULL || rsz == 0) break;
                /* Return the item to free the space (we're intentionally dropping it) */
                vRingbufferReturnItem(s_audio_buffer, it);
                dropped += rsz;
                drop_attempts++;
            }

            if (dropped > 0) {
                size_t free_after_drop = xRingbufferGetCurFreeSize(s_audio_buffer);
                ESP_LOGW(TAG, "audio_processor_beep: dropped %zu bytes to free space, free_after_drop=%zu; retrying send once",
                         dropped, free_after_drop);
                    log_heap_stats("beep-after-drop");
                /* Try one immediate send (no wait) after dropping */
                sent = xRingbufferSend(s_audio_buffer, s_proc_buffer, (size_t)frames * frame_bytes, 0);
                if (sent == pdTRUE) {
                    /* success: continue with next chunk */
                    bytes_remaining -= (size_t)frames * frame_bytes;
                    continue;
                }
            }

            /* Historically this code attempted to start A2DP streaming when
             * the ringbuffer was full to "coax" a remote into pulling audio.
             * In practice this frequently provoked late allocations inside
             * the Bluetooth stack which failed (observed as repeated
             * "BT_OSI: malloc failed" messages). To avoid triggering the
             * BT stack under transient pressure we no longer attempt to
             * start A2DP from the beep path. Instead rely on the dedicated
             * beep buffer and the on-the-fly fallback generator so tones
             * remain audible without risking BT allocator failures. If a
             * future policy is desired for remote-start-on-beep it should
             * be implemented as an explicit, configurable behavior outside
             * of this hot-path.
             */
            ESP_LOGW(TAG, "audio_processor_beep: ringbuffer full and connected but not streaming — automatic bt_start_audio() disabled; using beep buffer/fallback instead");

            /* If the main buffer still can't accept this chunk, try the
             * dedicated low-latency beep buffer. Use chunked writes so a
             * modest-size beep buffer can accept large beeps piecewise.
             */
            if (s_beep_buffer != NULL) {
                size_t needed_b = (size_t)frames * frame_bytes;
                size_t enqueued = beep_buffer_send_chunked((const uint8_t*)s_proc_buffer, needed_b, frame_bytes);
                if (enqueued > 0) {
                    bytes_remaining -= enqueued;
                    ESP_LOGW(TAG, "audio_processor_beep: enqueued %zu bytes into beep buffer (low-latency)", enqueued);
                    log_heap_stats("beep-enqueued-beepbuf");
                    /* If we didn't enqueue the full chunk, we'll fall through
                     * and enable fallback for the remainder after the loop.
                     */
                    if (enqueued == needed_b) {
                        continue;
                    }
                }
            }

            ESP_LOGW(TAG, "audio_processor_beep: ringbuffer full, queued %zu/%zu bytes after %d attempts, free_now=%zu",
                     total_bytes - bytes_remaining, total_bytes, attempt, xRingbufferGetCurFreeSize(s_audio_buffer));
            break;
        }

        frames_generated += frames;
        bytes_remaining -= (size_t)frames * frame_bytes;
    }

    /* Mark remaining bytes so reads will bypass mute until beep data is
     * consumed. If the ringbuffer didn't accept the whole beep, enable
     * the fallback generator for the remainder so the tone is audible
     * even when the main buffer is saturated. */
    size_t queued = total_bytes - bytes_remaining;
    /* Increase the bypass counter by the total beep length (queued + fallback)
     * so reads will bypass mute for the whole duration; the reader will
     * decrement this counter as bytes are consumed from either source. */
    s_beep_remaining_bytes += total_bytes;

    /* If there are leftover bytes that weren't queued, enable the on-the-fly
     * fallback generator for the remaining frames. */
    if (bytes_remaining > 0) {
        size_t remaining_frames = bytes_remaining / frame_bytes;
            if (remaining_frames > 0) {
                portENTER_CRITICAL(&s_beep_lock);
                /* If this is the first time we're enabling the fallback,
                 * remember the current synth mode and force synth on so
                 * the reader generates samples locally. This prevents
                 * I2S timeouts or missing audio when no external source
                 * is present and ensures the fallback tone is audible. */
                if (!s_beep_fallback_active) {
                    s_beep_prev_force_synth = s_force_synth;
                    s_force_synth = true;
                }
                s_beep_fallback_active = true;
                s_beep_fallback_frames_remaining += remaining_frames; /* accumulate if multiple beeps */
                s_beep_fallback_total_frames += remaining_frames; /* remember total frames for envelope progress */
                /* Initialize floating-point phase accumulator for sine generator */
                s_beep_fallback_phase = 0.0;
                const double two_pi = 2.0 * M_PI;
                if (sample_rate > 0) {
                    s_beep_fallback_phase_inc = (two_pi * (double)tone_hz) / (double)sample_rate;
                } else {
                    s_beep_fallback_phase_inc = (two_pi * (double)tone_hz) / 44100.0;
                }
                portEXIT_CRITICAL(&s_beep_lock);
                ESP_LOGW(TAG, "audio_processor_beep: fallback enabled for %zu frames (%zu bytes)", remaining_frames, bytes_remaining);
                /* If diagnostics were armed for the next beep, emit an
                 * immediate snapshot of the tone we generated above so
                 * callers can observe fallback data even if the actual
                 * on-the-fly generator hasn't yet been pulled by the
                 * reader. This helps capture a representative sample
                 * for offline comparison between worker and fallback
                 * outputs. Consume the one-shot flag. */
                if (s_dump_next_beep_diag) {
                    size_t dump = AUDIO_WORK_BUFFER_BYTES < DIAG_DUMP_BYTES ? AUDIO_WORK_BUFFER_BYTES : DIAG_DUMP_BYTES;
                    /* s_proc_buffer contains the last-generated tone chunk */
                    diag_dump_bytes(s_proc_buffer, dump, "DIAG:fallback-out");
                    s_dump_next_beep_diag = false;
                }
            }
    }

    ESP_LOGI(TAG, "audio_processor_beep: queued %zu bytes (%u ms); fallback_remaining=%zu bytes", queued, (unsigned)duration_ms, bytes_remaining);
    return ESP_OK;
}

/**
 * @brief Play a WAV file by reading PCM frames, converting/resampling as needed
 * and enqueueing into the audio ringbuffer.
 */
esp_err_t audio_processor_play_wav(const char* path)
{
    /* Diagnostic: record initialization/running state to help trace
     * unexpected INVALID_STATE returns observed in unit tests. Use both
     * ESP_LOG and a plain printf so the test monitor captures the output
     * regardless of log configuration. */
    ESP_LOGD(TAG, "audio_processor_play_wav: entry (s_is_initialized=%d, s_is_running=%d, s_audio_buffer=%p)",
             (int)s_is_initialized, (int)s_is_running, (void*)s_audio_buffer);
    printf("DIAG-APLAY-STATE: init=%d run=%d buf=%p path=%s\n",
           (int)s_is_initialized, (int)s_is_running, (void*)s_audio_buffer, path ? path : "(null)");
    if (!s_is_initialized) return ESP_ERR_INVALID_STATE;
    if (!path) return ESP_ERR_INVALID_ARG;

    FILE* f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "audio_processor_play_wav: failed to open %s", path);
        return ESP_ERR_NOT_FOUND;
    }

    /* Basic WAV header parsing (RIFF/WAVE, fmt chunk, data chunk) */
    char riff[4];
    if (fread(riff, 1, 4, f) != 4) { 
        ESP_LOGW(TAG, "audio_processor_play_wav: missing RIFF header (fread failed)");
        printf("DIAG-APLAY-FAIL: missing-riff\n");
        fclose(f); return ESP_ERR_INVALID_STATE; 
    }
    if (memcmp(riff, "RIFF", 4) != 0) { 
        ESP_LOGW(TAG, "audio_processor_play_wav: RIFF header mismatch");
        printf("DIAG-APLAY-FAIL: riff-mismatch\n");
        fclose(f); return ESP_ERR_INVALID_STATE; 
    }
    /* skip file size */
    uint32_t tmp32 = 0;
    fread(&tmp32, 4, 1, f);
    char wave[4];
    if (fread(wave, 1, 4, f) != 4) { 
        ESP_LOGW(TAG, "audio_processor_play_wav: missing WAVE header (fread failed)");
        printf("DIAG-APLAY-FAIL: missing-wave\n");
        fclose(f); return ESP_ERR_INVALID_STATE; 
    }
    if (memcmp(wave, "WAVE", 4) != 0) { 
        ESP_LOGW(TAG, "audio_processor_play_wav: WAVE header mismatch");
        printf("DIAG-APLAY-FAIL: wave-mismatch\n");
        fclose(f); return ESP_ERR_INVALID_STATE; 
    }

    uint16_t audio_format = 0;
    uint16_t num_channels = 0;
    uint32_t sample_rate = 0;
    uint16_t bits_per_sample = 0;
    uint32_t data_bytes = 0;
    bool have_fmt = false;

    /* Walk chunks until we find 'fmt ' and 'data' */
    while (!feof(f)) {
        char chunk_id[4];
        uint32_t chunk_size = 0;
        if (fread(chunk_id, 1, 4, f) != 4) break;
        if (fread(&chunk_size, 4, 1, f) != 1) break;
        /* chunk_size is little-endian in WAV (host is little-endian on ESP32/Linux)
         * but keep it as-is for portability assumptions used here. */

        if (memcmp(chunk_id, "fmt ", 4) == 0) {
            /* Read minimal fmt chunk (PCM header is 16 bytes) */
            if (chunk_size < 16) { 
                ESP_LOGW(TAG, "audio_processor_play_wav: fmt chunk too small (chunk_size=%u)", (unsigned)chunk_size);
                printf("DIAG-APLAY-FAIL: fmt-chunk-too-small %u\n", (unsigned)chunk_size);
                fclose(f); return ESP_ERR_INVALID_STATE; 
            }
            uint16_t fmt16_1 = 0;
            fread(&fmt16_1, 2, 1, f); audio_format = fmt16_1;
            fread(&num_channels, 2, 1, f);
            fread(&sample_rate, 4, 1, f);
            /* skip byte rate */
            fread(&tmp32, 4, 1, f);
            /* skip block align */
            uint16_t tmp16 = 0; fread(&tmp16, 2, 1, f);
            fread(&bits_per_sample, 2, 1, f);
            /* If fmt chunk larger than 16 bytes, skip remaining */
            if (chunk_size > 16) fseek(f, (long)(chunk_size - 16), SEEK_CUR);
            have_fmt = true;
        } else if (memcmp(chunk_id, "data", 4) == 0) {
            data_bytes = chunk_size;
            /* file pointer now at start of data */
            break;
        } else {
            /* skip other chunks */
            fseek(f, (long)chunk_size, SEEK_CUR);
        }
    }

    /* Emit header diagnostics if we successfully parsed fmt/data so
     * the test runner can inspect the WAV fields and a small data peek. */
    if (have_fmt && data_bytes > 0) {
        ESP_LOGI(TAG, "audio_processor_play_wav: parsed WAV fmt=%u ch=%u sr=%u bits=%u data=%u",
                 (unsigned)audio_format, (unsigned)num_channels, (unsigned)sample_rate, (unsigned)bits_per_sample, (unsigned)data_bytes);
        printf("DIAG-APLAY-HDR: fmt=%u ch=%u sr=%u bits=%u data=%u\n",
               (unsigned)audio_format, (unsigned)num_channels, (unsigned)sample_rate, (unsigned)bits_per_sample, (unsigned)data_bytes);

        /* Peek the first few bytes of the data region to detect file/corruption issues.
         * Save/restore the file pointer so normal processing is unaffected. */
        long cur = ftell(f);
        size_t peek_n = data_bytes < 16U ? data_bytes : 16U;
        if (peek_n > 0) {
            unsigned char peek[16];
            size_t got = fread(peek, 1, peek_n, f);
            if (got > 0) {
                printf("DIAG-APLAY-PEEK: %u bytes:", (unsigned)got);
                for (size_t i = 0; i < got; ++i) {
                    printf(" %02x", (unsigned)peek[i]);
                }
                printf("\n");
            }
            /* restore file pointer to where it was (start of data) */
            if (cur >= 0) fseek(f, cur, SEEK_SET);
        }
    }

    if (!have_fmt || data_bytes == 0) { 
        ESP_LOGW(TAG, "audio_processor_play_wav: missing fmt or data chunk (have_fmt=%d data_bytes=%u)", (int)have_fmt, (unsigned)data_bytes);
        printf("DIAG-APLAY-FAIL: missing-fmt-or-data %d %u\n", (int)have_fmt, (unsigned)data_bytes);
        fclose(f); return ESP_ERR_INVALID_STATE; 
    }

    /* Only support PCM (audio_format == 1) for now */
    if (audio_format != 1) {
        ESP_LOGE(TAG, "audio_processor_play_wav: unsupported WAV format=%u", (unsigned)audio_format);
        fclose(f);
        return ESP_ERR_NOT_SUPPORTED;
    }

    /* Map bits_per_sample to audio_bit_depth_t */
    audio_bit_depth_t src_bit = AUDIO_BIT_DEPTH_16;
    if (bits_per_sample == 16) src_bit = AUDIO_BIT_DEPTH_16;
    else if (bits_per_sample == 24) src_bit = AUDIO_BIT_DEPTH_24;
    else if (bits_per_sample == 32) src_bit = AUDIO_BIT_DEPTH_32;
    else {
        ESP_LOGE(TAG, "audio_processor_play_wav: unsupported bits_per_sample=%u", (unsigned)bits_per_sample);
        fclose(f);
        return ESP_ERR_NOT_SUPPORTED;
    }

    size_t frame_bytes_src = (bits_per_sample / 8) * (size_t)num_channels;
    if (frame_bytes_src == 0) { 
        ESP_LOGW(TAG, "audio_processor_play_wav: computed zero frame_bytes_src (bits=%u channels=%u)", (unsigned)bits_per_sample, (unsigned)num_channels);
        printf("DIAG-APLAY-FAIL: zero-frame-bytes bits=%u ch=%u\n", (unsigned)bits_per_sample, (unsigned)num_channels);
        fclose(f); return ESP_ERR_INVALID_STATE; 
    }

    /* Read data in chunks, convert/resample and enqueue */
    size_t remaining = data_bytes;
    while (remaining > 0) {
        size_t to_read = AUDIO_WORK_BUFFER_BYTES;
        /* align to frame */
        to_read = (to_read / frame_bytes_src) * frame_bytes_src;
        if (to_read == 0) to_read = frame_bytes_src;
        if (to_read > remaining) to_read = remaining;

        size_t actually = fread(s_proc_buffer, 1, to_read, f);
        if (actually == 0) break; /* unexpected EOF */

        /* Convert to internal bit depth */
        size_t conv_size = 0;
        if (convert_audio_format(s_proc_buffer, s_proc_buffer, actually, src_bit, s_audio_config.bit_depth, &conv_size) != ESP_OK) {
            ESP_LOGE(TAG, "audio_processor_play_wav: convert_audio_format failed");
            printf("DIAG-APLAY-FAIL: convert-failed\n");
            fclose(f);
            return ESP_ERR_INVALID_STATE;
        }

        /* Resample if needed */
        size_t res_size = 0;
        if (resample_audio(s_proc_buffer, s_proc_buffer2, conv_size, (audio_sample_rate_t)sample_rate, s_audio_config.sample_rate, &res_size) != ESP_OK) {
            ESP_LOGE(TAG, "audio_processor_play_wav: resample_audio failed");
            printf("DIAG-APLAY-FAIL: resample-failed\n");
            fclose(f);
            return ESP_ERR_INVALID_STATE;
        }

        /* Enqueue to ringbuffer with same retry/drop logic used by beep */
        BaseType_t sent = pdFALSE;
        const int max_attempts = 3;
        for (int attempt = 0; attempt < max_attempts && sent != pdTRUE; ++attempt) {
            sent = xRingbufferSend(s_audio_buffer, s_proc_buffer2, res_size, 0);
            if (sent == pdTRUE) {
                break;
            }
            taskYIELD();
        }
        if (sent != pdTRUE) {
            /* Try to free some space by dropping oldest items (bounded) */
            size_t needed = res_size;
            int drop_attempts = 0;
            const int max_drop_attempts = 16;
            while (xRingbufferGetCurFreeSize(s_audio_buffer) < needed && drop_attempts < max_drop_attempts) {
                size_t rsz = 0;
                void* it = xRingbufferReceiveUpTo(s_audio_buffer, &rsz, needed, 0);
                if (it == NULL || rsz == 0) break;
                vRingbufferReturnItem(s_audio_buffer, it);
                drop_attempts++;
                taskYIELD();
            }
            /* Try once more without waiting */
            sent = xRingbufferSend(s_audio_buffer, s_proc_buffer2, res_size, 0);
            if (sent != pdTRUE) {
                /* Match format specifiers to argument types to satisfy -Wformat */
                ESP_LOGW(TAG, "audio_processor_play_wav: ringbuffer full, aborting playback (enqueued %u/%u bytes)", (unsigned)(data_bytes - remaining), (unsigned)data_bytes);
                fclose(f);
                return ESP_ERR_NO_MEM;
            }
        }

        remaining -= actually;
    }

    fclose(f);
    ESP_LOGI(TAG, "audio_processor_play_wav: playback enqueued for %s", path);
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

    /* Use the file-scoped failure counter so other code can observe I2S
     * health. Initialize it to zero at first-run. */
    /* NOTE: the variable is declared at file scope as `s_i2s_consecutive_failures`. */

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
                s_i2s_consecutive_failures = 0;
        } else {
            /* Perform smaller, frame-aligned reads to avoid long blocking
             * inside the I2S driver. Large single reads can block the
             * reader task and increase chance of timeouts; cap each read to
             * I2S_MAX_READ_BYTES and loop until we've attempted the
             * ideal_read amount or hit an error. */
            size_t remaining = read_request;
            size_t total_read = 0;
            int local_failures = 0;
            int64_t t_start = esp_timer_get_time();

            while (remaining > 0) {
                size_t this_read = remaining;
                if (this_read > I2S_MAX_READ_BYTES) this_read = I2S_MAX_READ_BYTES;
                /* Align to frame_bytes */
                if (frame_bytes > 0) {
                    this_read = (this_read / frame_bytes) * frame_bytes;
                    if (this_read == 0) this_read = frame_bytes;
                }

                size_t part_read = 0;
                esp_err_t part_ret = i2s_channel_read(s_i2s_rx_handle,
                                                      (uint8_t*)s_i2s_buffer + total_read,
                                                      this_read,
                                                      &part_read,
                                                      0);

                if (part_ret == ESP_OK && part_read > 0) {
                    total_read += part_read;
                    remaining = (remaining > part_read) ? (remaining - part_read) : 0;
                    have_frame = true;
                    s_i2s_consecutive_failures = 0;
                } else {
                    /* Treat both errors and zero reads as a failure for the
                     * consecutive failure counter. Bail out of the chunked
                     * loop to avoid spinning on a non-responsive I2S device. */
                    local_failures++;
                    s_i2s_consecutive_failures++;
                    if (part_ret != ESP_OK) {
                        last_i2s_ret = part_ret;
                    } else {
                        last_i2s_ret = ESP_ERR_INVALID_SIZE;
                    }
                    break;
                }

                /* If we've filled our work buffer, stop to let the worker run */
                if (total_read >= AUDIO_WORK_BUFFER_BYTES) {
                    break;
                }
            }

            bytes_read = total_read;
            if (!have_frame && local_failures > 0) {
                /* nothing read and at least one failure */
            }

            if (total_read > I2S_MAX_READ_BYTES) {
                int64_t t_end = esp_timer_get_time();
                ESP_LOGD(TAG, "i2s_reader_task: multi-chunk read total=%zu took=%lld us", total_read, (long long)(t_end - t_start));
            }

        const int FAILURE_THRESHOLD = 20;
            if (s_i2s_consecutive_failures >= FAILURE_THRESHOLD) {
                ESP_LOGW(TAG, "I2S read failing repeatedly (%d); enabling runtime synth mode", s_i2s_consecutive_failures);
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
                    ESP_LOGW(TAG, "i2s_reader_task: backpressure while enqueueing i2s block len=%zu", blk.len);
                    log_heap_stats("i2s-backpressure");
                }
            } else {
                s_audio_stats.buffer_overruns++;
                backpressure = true;
            }
        } else {
            s_audio_stats.buffer_overruns++;
            backpressure = true;
            ESP_LOGW(TAG, "i2s_reader_task: backpressure (no free queue) for len=%zu", bytes_read);
            log_heap_stats("i2s-backpressure-no-free-queue");
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

        /* If diagnostics were armed for the next beep and this block was
         * filled by the synth path, emit a small snapshot of the worker's
         * post-resample output so we can compare against the fallback
         * generator output. */
        if (blk.synth_fill && s_dump_next_beep_diag && res_size > 0) {
            size_t dump = res_size < DIAG_DUMP_BYTES ? res_size : DIAG_DUMP_BYTES;
            diag_dump_bytes(s_proc_buffer2, dump, "DIAG:worker-out");
            s_dump_next_beep_diag = false;
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

            BaseType_t sent = xRingbufferSend(s_audio_buffer, s_proc_buffer2, res_size, pdMS_TO_TICKS(5));
                if (sent != pdTRUE) {
                    s_audio_stats.buffer_overruns++;
                    ESP_LOGW(TAG, "audio_worker_task: xRingbufferSend failed for res_size=%zu", res_size);
                    log_heap_stats("worker-send-failed");
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
 * @brief Diagnostic byte-dump helper
 * Prints up to `len` bytes from data in compact hex groups to the log.
 */
static void diag_dump_bytes(const void* data, size_t len, const char* tag)
{
    if (data == NULL || len == 0 || tag == NULL) return;
    const uint8_t* b = (const uint8_t*)data;
    /* Print in 16-byte rows to keep logs readable */
    size_t off = 0;
    while (off < len) {
        char line[128];
        size_t row = (len - off) < 16 ? (len - off) : 16;
        int pos = snprintf(line, sizeof(line), "%s: ", tag);
        for (size_t i = 0; i < row && pos < (int)sizeof(line) - 3; ++i) {
            pos += snprintf(line + pos, sizeof(line) - pos, "%02X ", b[off + i]);
        }
        ESP_LOGI(TAG, "%s", line);
        off += row;
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
 * @brief Arm a one-shot diagnostic dump for the next beep invocation.
 * Call this before issuing `BEEP` to capture fallback and worker snapshots.
 */
void audio_processor_enable_next_beep_diag(void)
{
    s_dump_next_beep_diag = true;
    ESP_LOGI(TAG, "audio_processor: next-beep diagnostic enabled");
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
