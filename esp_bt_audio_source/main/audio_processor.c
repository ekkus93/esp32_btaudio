#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/ringbuf.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "driver/i2s_std.h"  // Use the current I2S driver
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_err.h"
#include "esp_debug_helpers.h"
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
#define AUDIO_PROC_LOG_ONCE()                                                           \
    do {                                                                                \
        static bool _logged = false;                                                    \
        if (!_logged) {                                                                 \
            ESP_LOGI(TAG, "audio_processor (main) entered %s", __func__);              \
            _logged = true;                                                             \
        }                                                                               \
    } while (0)

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
/* Reduce worst-case resample ratio on DRAM-only boards to save a modest
 * amount of temporary work buffer space. 12->8 still covers common
 * upsampling (8k->64k) while lowering memory pressure. */
#define AUDIO_RESAMPLE_MAX_RATIO     8    // Reduced from 12 to 8 to save RAM
#endif
#define AUDIO_WORK_BUFFER_BYTES      (AUDIO_BLOCK_SIZE * 8 * AUDIO_RESAMPLE_MAX_RATIO)

/* Dedicated small buffer for urgent beep audio so short tones can be
 * delivered even when the main pipeline is congested. Keep modest to
 * limit DRAM pressure. Reduce to 8 KiB by default to avoid DRAM
 * allocation failures in the Bluetooth stack (see runtime logs). */
#define BEEP_BUFFER_SIZE (4 * 1024)
/* Fade duration (ms) applied to the start and end of queued/fallback beeps
 * Option A: small crossfade to reduce clicks. Reasonable default: 8 ms. */
#define BEEP_FADE_MS 8

/* Metadata ringbuffer capacity scaled relative to audio buffer with a
 * modest floor so tag enqueue operations never outpace the audio data
 * writers even on DRAM-only systems with reduced audio buffers. */
#define AUDIO_SOURCE_BUFFER_SIZE ((AUDIO_BUFFER_SIZE / 16U) > 512U ? (AUDIO_BUFFER_SIZE / 16U) : 512U)

// Audio processing task handle
static TaskHandle_t s_audio_task_handle = NULL; /* I2S reader task */
static TaskHandle_t s_audio_worker_handle = NULL; /* Worker that performs convert/resample */
typedef struct {
    void* ptr;
    size_t len;
    size_t capacity;
    bool synth_fill;
    bool pooled_ptr;
} i2s_block_t;

typedef enum {
    AUDIO_SOURCE_TAG_INVALID = 0,
    AUDIO_SOURCE_TAG_WAV     = 1,
    AUDIO_SOURCE_TAG_CAPTURE = 2,
    AUDIO_SOURCE_TAG_SYNTH   = 3,
    AUDIO_SOURCE_TAG_BEEP    = 4,
} audio_source_tag_t;

static QueueHandle_t s_i2s_queue = NULL; /* Queue of i2s_block_t */
static QueueHandle_t s_i2s_free_queue = NULL; /* Queue of free raw block pointers */
static void **s_i2s_pool = NULL; /* Array of pointers for freeing at deinit */
#define I2S_RAW_POOL_DEFAULT_COUNT 8U
/* On DRAM-only devices reduce the small prealloc pool to avoid large
 * upfront DRAM consumption. Increase only if PSRAM is available. */
#define I2S_RAW_POOL_DRAM_COUNT    1U
/* Tunable I2S parameters to trade latency vs RAM. Lower per-read sizes
 * reduce blocking time in the reader; more descriptors give the DMA more
 * headroom without needing very large per-descriptor frames. Adjust if
 * you have PSRAM or different timing requirements. */
#define I2S_DEFAULT_DMA_DESC_NUM 6U
#define I2S_DEFAULT_DMA_FRAME_NUM 32U
#define I2S_MAX_READ_BYTES (4 * 1024)
#define SYNTH_MIN_HEADROOM_BYTES  (AUDIO_WORK_BUFFER_BYTES)
#define SYNTH_THROTTLE_DELAY_MS   2

// Ring buffer for audio data
static RingbufHandle_t s_audio_buffer = NULL;
static RingbufHandle_t s_audio_source_buffer = NULL;
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
static volatile bool s_trace_next_read_call = false;
static size_t s_runtime_work_bytes = 0U;

static size_t audio_get_runtime_work_bytes(void)
{
    size_t bytes = s_runtime_work_bytes;
    if (bytes == 0U) {
        bytes = (size_t)AUDIO_WORK_BUFFER_BYTES;
    }
    return bytes;
}

static void log_read_summary(const char *phase, size_t requested, size_t produced);
static inline int audio_bytes_per_sample(audio_bit_depth_t bit_depth);
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

#define WAV_RINGBUFFER_WAIT_MS (50U)
#define WAV_RINGBUFFER_MAX_DROPS (16)
/* Limit WAV ringbuffer writes to smaller slices to keep critical sections short. */
#define WAV_RINGBUFFER_SUBCHUNK_BYTES (256U)

// Diagnostics throttling state for high-frequency logging inside the
// audio processing task. We emit the first log immediately and then
// rate-limit updates to avoid starving the idle task and tripping the watchdog.
static TickType_t s_diag_next_log_tick = 0;
static size_t s_diag_last_conv_size = SIZE_MAX;
static size_t s_diag_last_frame_bytes = SIZE_MAX;
static int s_diag_last_src_rate = -1;
static int s_diag_last_dst_rate = -1;

typedef enum {
    WORKER_DIAG_SOURCE_WORKER = 0,
    WORKER_DIAG_SOURCE_WAV
} worker_diag_source_t;

typedef struct {
    uint32_t dequeued_blocks;
    uint32_t synth_blocks;
    size_t bytes_sent;
    size_t worker_bytes_sent;
    size_t wav_bytes_sent;
    uint32_t wav_chunks;
    uint32_t ringbuffer_failures;
    size_t last_enqueued_bytes;
    BaseType_t last_send_result;
    TickType_t last_report_tick;
    worker_diag_source_t last_source;
} worker_diag_state_t;

static worker_diag_state_t s_worker_diag = {0};
static const TickType_t WORKER_DIAG_INTERVAL_TICKS = pdMS_TO_TICKS(1000);

static TickType_t s_wav_retry_log_next_tick = 0;
static const TickType_t WAV_RETRY_LOG_INTERVAL_TICKS = pdMS_TO_TICKS(200);

static inline void wav_stream_log_backpressure(size_t chunk, size_t free_sz, size_t frame_sz,
                                               size_t max_item, size_t remaining)
{
    TickType_t now = xTaskGetTickCount();
    if (now >= s_wav_retry_log_next_tick) {
        ESP_LOGW(TAG,
                 "WAV enqueue backpressure: chunk=%zu free=%zu frame=%zu max=%zu remaining=%zu",
                 chunk, free_sz, frame_sz, max_item, remaining);
        s_wav_retry_log_next_tick = now + WAV_RETRY_LOG_INTERVAL_TICKS;
    }
}

static void log_read_summary(const char *phase, size_t requested, size_t produced);
static esp_err_t convert_audio_format(void* src, void* dst, size_t src_size,
                                      audio_bit_depth_t src_bit_depth, audio_bit_depth_t dst_bit_depth,
                                      size_t* dst_size);
static esp_err_t resample_audio(void* src, void* dst, size_t src_size,
                                audio_sample_rate_t src_rate, audio_sample_rate_t dst_rate,
                                size_t* dst_size);
static void worker_diag_report(worker_diag_source_t source, size_t enqueued_bytes, BaseType_t send_result);
static bool audio_source_tag_push(audio_source_tag_t tag);
static bool audio_source_tag_take(audio_source_tag_t *tag, TickType_t wait_ticks);
static void audio_source_tag_drop_one(void);
static void audio_source_tag_reset_buffer(void);

static inline void audio_proc_mock_yield(void)
{
#ifdef CONFIG_BT_MOCK_TESTING
    vTaskDelay(1);
#endif
}

/* Diagnostic logging helper for tag lifecycle traces used during
 * investigation and unit testing. These are silent by default; to
 * enable the verbose tag diagnostics for a host test run define
 * CONFIG_AUDIO_TAG_DIAGNOSTICS in the test target. This avoids
 * producing large volumes of test output during normal runs while
 * allowing focused troubleshooting when explicitly requested. */
#if defined(CONFIG_BT_MOCK_TESTING) && defined(CONFIG_AUDIO_TAG_DIAGNOSTICS)
#define AUDIO_TAG_DIAG(...) printf(__VA_ARGS__)
#else
#define AUDIO_TAG_DIAG(...) do {} while (0)
#endif

static bool audio_source_tag_push(audio_source_tag_t tag)
{
    if (s_audio_source_buffer == NULL) {
        return false;
    }

    const uint8_t value = (uint8_t)tag;
    const TickType_t wait_ticks = pdMS_TO_TICKS(2);
    /* Test-only: capture occupancy before/after to make push atomic in logs */
#ifdef CONFIG_BT_MOCK_TESTING
    size_t free_before = xRingbufferGetCurFreeSize(s_audio_source_buffer);
    size_t cap_before = (size_t)AUDIO_SOURCE_BUFFER_SIZE;
    AUDIO_TAG_DIAG("DIAG-TAG-PUSH-BEFORE: tag=%u used=%zu\n", (unsigned)value, (cap_before > free_before ? cap_before - free_before : 0));
#endif
    BaseType_t sent = xRingbufferSend(s_audio_source_buffer, &value, sizeof(value), wait_ticks);
    if (sent != pdTRUE) {
        ESP_LOGE(TAG, "audio_source_tag_push: failed tag=%u", (unsigned)value);
#ifdef CONFIG_BT_MOCK_TESTING
    size_t free_fail = xRingbufferGetCurFreeSize(s_audio_source_buffer);
    size_t cap_fail = (size_t)AUDIO_SOURCE_BUFFER_SIZE;
    AUDIO_TAG_DIAG("DIAG-TAG-PUSH-FAILED: tag=%u used=%zu\n", (unsigned)value, (cap_fail > free_fail ? cap_fail - free_fail : 0));
#endif
        return false;
    }
#ifdef CONFIG_BT_MOCK_TESTING
    /* Test-only diagnostic: show occupancy after push */
    size_t free_now = xRingbufferGetCurFreeSize(s_audio_source_buffer);
    size_t cap = (size_t)AUDIO_SOURCE_BUFFER_SIZE;
    AUDIO_TAG_DIAG("DIAG-TAG-PUSH-AFTER: tag=%u used=%zu\n", (unsigned)value, (cap > free_now ? cap - free_now : 0));
#endif
    return true;
}

static bool audio_source_tag_take(audio_source_tag_t *tag, TickType_t wait_ticks)
{
    if (s_audio_source_buffer == NULL) {
        return false;
    }

    /* Test-only: show occupancy before/after take so logs reflect atomic change */
#ifdef CONFIG_BT_MOCK_TESTING
    size_t free_before = xRingbufferGetCurFreeSize(s_audio_source_buffer);
    size_t cap_before = (size_t)AUDIO_SOURCE_BUFFER_SIZE;
    AUDIO_TAG_DIAG("DIAG-TAG-TAKE-BEFORE: used=%zu\n", (cap_before > free_before ? cap_before - free_before : 0));
#endif
#ifdef CONFIG_BT_MOCK_TESTING
    size_t free_before_recv = xRingbufferGetCurFreeSize(s_audio_source_buffer);
#endif
    size_t size = 0;
    void *item = xRingbufferReceiveUpTo(s_audio_source_buffer, &size, wait_ticks, sizeof(uint8_t));
    if (item == NULL || size == 0) {
#ifdef CONFIG_BT_MOCK_TESTING
    size_t free_after_recv_empty = xRingbufferGetCurFreeSize(s_audio_source_buffer);
    AUDIO_TAG_DIAG("DIAG-TAG-TAKE-EMPTY: item=NULL size=%zu free_before=%zu free_after=%zu\n", size, free_before_recv, free_after_recv_empty);
#endif
        return false;
    }

    uint8_t value = *((const uint8_t *)item);
#ifdef CONFIG_BT_MOCK_TESTING
    size_t free_after_recv = xRingbufferGetCurFreeSize(s_audio_source_buffer);
    AUDIO_TAG_DIAG("DIAG-TAG-TAKE-RECV: ptr=%p size=%zu free_before=%zu free_after_recv=%zu\n", item, size, free_before_recv, free_after_recv);
#endif
    vRingbufferReturnItem(s_audio_source_buffer, item);
    if (tag != NULL) {
        *tag = (audio_source_tag_t)value;
    }
#ifdef CONFIG_BT_MOCK_TESTING
    size_t free_after_return = xRingbufferGetCurFreeSize(s_audio_source_buffer);
    size_t cap2 = (size_t)AUDIO_SOURCE_BUFFER_SIZE;
    AUDIO_TAG_DIAG("DIAG-TAG-TAKE-RETURN: got=%u used=%zu free_after_return=%zu\n", (unsigned)value, (cap2 > free_after_return ? cap2 - free_after_return : 0), free_after_return);
#endif
    
    return true;
}

static void audio_source_tag_drop_one(void)
{
    if (s_audio_source_buffer == NULL) {
        return;
    }

    /* Test-only: occupancy before drop */
#ifdef CONFIG_BT_MOCK_TESTING
    size_t free_before = xRingbufferGetCurFreeSize(s_audio_source_buffer);
    size_t cap_before = (size_t)AUDIO_SOURCE_BUFFER_SIZE;
    AUDIO_TAG_DIAG("DIAG-TAG-DROP-BEFORE: used=%zu free_before=%zu\n", (cap_before > free_before ? cap_before - free_before : 0), free_before);
#endif
    size_t size = 0;
    void *item = xRingbufferReceiveUpTo(s_audio_source_buffer, &size, 0, sizeof(uint8_t));
    if (item != NULL && size > 0) {
#ifdef CONFIG_BT_MOCK_TESTING
    size_t free_after_recv = xRingbufferGetCurFreeSize(s_audio_source_buffer);
    AUDIO_TAG_DIAG("DIAG-TAG-DROP-RECV: ptr=%p size=%zu free_after_recv=%zu\n", item, size, free_after_recv);
#endif
        vRingbufferReturnItem(s_audio_source_buffer, item);
#ifdef CONFIG_BT_MOCK_TESTING
    size_t free_now3 = xRingbufferGetCurFreeSize(s_audio_source_buffer);
    size_t cap3 = (size_t)AUDIO_SOURCE_BUFFER_SIZE;
    AUDIO_TAG_DIAG("DIAG-TAG-DROP-AFTER: dropped used=%zu free_after_return=%zu\n", (cap3 > free_now3 ? cap3 - free_now3 : 0), free_now3);
#endif
    }
}

static void audio_source_tag_reset_buffer(void)
{
    if (s_audio_source_buffer == NULL) {
        return;
    }

    size_t size = 0;
    void *item = NULL;
    const size_t max_take = sizeof(uint8_t);

    while ((item = xRingbufferReceiveUpTo(s_audio_source_buffer, &size, 0, max_take)) != NULL && size > 0U) {
        vRingbufferReturnItem(s_audio_source_buffer, item);
    }
#ifdef CONFIG_BT_MOCK_TESTING
    size_t free_now4 = xRingbufferGetCurFreeSize(s_audio_source_buffer);
    size_t cap4 = (size_t)AUDIO_SOURCE_BUFFER_SIZE;
    AUDIO_TAG_DIAG("DIAG-TAG-RESET: used=%zu\n", (cap4 > free_now4 ? cap4 - free_now4 : 0));
#endif
}

static bool wait_for_ringbuffer_space(size_t required_bytes, uint32_t timeout_ms, size_t *free_before_out)
{
    if (free_before_out != NULL) {
        *free_before_out = 0U;
    }

    if (required_bytes == 0U) {
        if (s_audio_buffer != NULL && free_before_out != NULL) {
            *free_before_out = xRingbufferGetCurFreeSize(s_audio_buffer);
        }
        return (s_audio_buffer != NULL);
    }

    if (s_audio_buffer == NULL) {
        return false;
    }

    size_t free_now = xRingbufferGetCurFreeSize(s_audio_buffer);
    if (free_before_out != NULL) {
        *free_before_out = free_now;
    }
    if (free_now >= required_bytes) {
        return true;
    }

    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    if (timeout_ms > 0U && timeout_ticks == 0) {
        timeout_ticks = 1;
    }
    bool use_deadline = (timeout_ms != 0U);
    TickType_t start_tick = xTaskGetTickCount();
    TickType_t poll_delay = pdMS_TO_TICKS(1);
    if (poll_delay == 0) {
        poll_delay = 1;
    }

    while (free_now < required_bytes) {
        vTaskDelay(poll_delay);
        free_now = xRingbufferGetCurFreeSize(s_audio_buffer);
        if (free_now >= required_bytes) {
            return true;
        }
        if (use_deadline) {
            TickType_t now = xTaskGetTickCount();
            if ((now - start_tick) >= timeout_ticks) {
                return false;
            }
        }
    }

    return true;
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
    AUDIO_PROC_LOG_ONCE();
    s_force_synth = enable ? true : false;
    ESP_LOGI(TAG, "audio_processor: synth mode %s", s_force_synth ? "ENABLED" : "DISABLED");
}

bool audio_processor_is_synth_mode_enabled(void)
{
    AUDIO_PROC_LOG_ONCE();
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
/* Residual buffers hold partial ringbuffer chunks so subsequent reads can
 * resume without dropping samples when the caller requests smaller blocks
 * than the chunk size produced by the worker or WAV enqueue paths. */
static uint8_t s_audio_rb_residual[AUDIO_WORK_BUFFER_BYTES];
static size_t s_audio_rb_residual_len = 0;
static size_t s_audio_rb_residual_pos = 0;
static audio_source_tag_t s_audio_rb_residual_tag = AUDIO_SOURCE_TAG_INVALID;
static bool s_audio_rb_residual_tag_valid = false;
static uint8_t s_beep_rb_residual[BEEP_BUFFER_SIZE];
static size_t s_beep_rb_residual_len = 0;
static size_t s_beep_rb_residual_pos = 0;
static volatile bool s_wav_playback_active = false;
static size_t s_wav_pending_bytes = 0;
static bool s_wav_prev_force_synth = false;
static bool s_wav_prev_valid = false;
static portMUX_TYPE s_wav_lock = portMUX_INITIALIZER_UNLOCKED;

typedef struct {
    bool active;
    bool resume_pipeline;
    FILE *file;
    audio_bit_depth_t src_bit_depth;
    audio_sample_rate_t src_sample_rate;
    size_t frame_bytes_src;
    size_t frame_bytes_dst;
    size_t remaining_bytes;
} wav_stream_state_t;

static SemaphoreHandle_t s_wav_mutex = NULL;
static wav_stream_state_t s_wav_stream = {0};
static uint8_t s_wav_send_residual[AUDIO_WORK_BUFFER_BYTES];
static size_t s_wav_send_residual_len = 0;
static size_t s_wav_send_residual_pos = 0;
static audio_source_tag_t s_wav_residual_tag = AUDIO_SOURCE_TAG_INVALID;
static bool s_wav_residual_tag_valid = false;

static size_t audio_ringbuffer_min_capacity(size_t frame_bytes)
{
    /* Ensure we always have space for several resample bursts plus headroom
     * for the ringbuffer's bookkeeping. Use AUDIO_WORK_BUFFER_BYTES as the
     * nominal burst size and fall back to a frame-aligned block when the
     * build-time constant is smaller than the current configuration. */
    size_t block_bytes = frame_bytes * (size_t)AUDIO_BLOCK_SIZE;
    if (block_bytes == 0U) {
        block_bytes = 1024U;
    }

    size_t burst_bytes = AUDIO_WORK_BUFFER_BYTES;
    if (burst_bytes < block_bytes) {
        burst_bytes = block_bytes;
    }

    /* Target at least three bursts so 6 KiB resample chunks do not saturate
     * the buffer immediately while WAV playback is enqueueing data. */
    size_t floor = burst_bytes;
    if (burst_bytes <= (SIZE_MAX / 3U)) {
        floor = burst_bytes * 3U;
    } else {
        floor = SIZE_MAX & ~((size_t)3U);
    }

    /* Add an extra burst worth of headroom to accommodate the ringbuffer's
     * internal bookkeeping and small drifts in producer/consumer pacing. */
    if (block_bytes <= (SIZE_MAX - burst_bytes)) {
        size_t padded = burst_bytes + block_bytes;
        if (padded > floor) {
            floor = padded;
        }
    }

    const size_t static_floor = (AUDIO_WORK_BUFFER_BYTES > (4U * 1024U)) ? AUDIO_WORK_BUFFER_BYTES : (4U * 1024U);
    if (floor < static_floor) {
        floor = static_floor;
    }

    floor = (floor + 3U) & ~((size_t)3U);
    return floor;
}

static size_t residual_copy(uint8_t *dest, size_t dest_len,
                            uint8_t *buffer, size_t *pos, size_t *len)
{
    if (dest == NULL || buffer == NULL || pos == NULL || len == NULL || dest_len == 0) {
        return 0;
    }

    size_t available = (*len > *pos) ? (*len - *pos) : 0;
    size_t to_copy = (available < dest_len) ? available : dest_len;
    if (to_copy == 0) {
        return 0;
    }

    memcpy(dest, buffer + *pos, to_copy);
    *pos += to_copy;
    if (*pos >= *len) {
        *pos = 0;
        *len = 0;
    }
    return to_copy;
}

static size_t residual_store(const uint8_t *src, size_t len,
                             uint8_t *buffer, size_t capacity,
                             size_t *pos, size_t *stored_len)
{
    if (src == NULL || buffer == NULL || pos == NULL || stored_len == NULL) {
        return 0;
    }

    if (len == 0 || capacity == 0) {
        *stored_len = 0;
        *pos = 0;
        return 0;
    }

    size_t copy_len = (len > capacity) ? capacity : len;
    memcpy(buffer, src, copy_len);
    *stored_len = copy_len;
    *pos = 0;
    return copy_len;
}

static bool wav_playback_is_active(void)
{
    return s_wav_playback_active;
}

static void wav_playback_begin(void)
{
    bool prev = false;
    portENTER_CRITICAL(&s_wav_lock);
    prev = s_force_synth;
    s_wav_prev_force_synth = s_force_synth;
    s_wav_prev_valid = true;
    s_wav_pending_bytes = 0;
    s_wav_playback_active = true;
    s_force_synth = false;
    portEXIT_CRITICAL(&s_wav_lock);
    ESP_LOGI(TAG, "audio_processor: WAV playback begin (prev synth=%s)", prev ? "ENABLED" : "DISABLED");
}

static void wav_playback_add_pending(size_t bytes)
{
    if (bytes == 0) {
        return;
    }

    portENTER_CRITICAL(&s_wav_lock);
    if (s_wav_playback_active) {
        if (SIZE_MAX - s_wav_pending_bytes < bytes) {
            s_wav_pending_bytes = SIZE_MAX;
        } else {
            s_wav_pending_bytes += bytes;
        }
    }
    portEXIT_CRITICAL(&s_wav_lock);
}

static bool wav_playback_consume(size_t bytes)
{
    if (bytes == 0) {
        return false;
    }

    bool drained = false;
    portENTER_CRITICAL(&s_wav_lock);
    if (s_wav_playback_active) {
        if (bytes >= s_wav_pending_bytes) {
            s_wav_pending_bytes = 0;
            drained = true;
        } else {
            s_wav_pending_bytes -= bytes;
        }
    }
    portEXIT_CRITICAL(&s_wav_lock);

    return drained;
}

static void wav_playback_abort(void)
{
    bool restored = false;
    bool synth_mode = false;
    portENTER_CRITICAL(&s_wav_lock);
    if (s_wav_playback_active || s_wav_prev_valid) {
        s_wav_pending_bytes = 0;
        s_wav_playback_active = false;
        if (s_wav_prev_valid) {
            s_force_synth = s_wav_prev_force_synth;
            synth_mode = s_force_synth;
            s_wav_prev_valid = false;
            restored = true;
        }
    }
    portEXIT_CRITICAL(&s_wav_lock);

    if (restored) {
        ESP_LOGI(TAG, "audio_processor: WAV playback aborted (restored synth=%s)", synth_mode ? "ENABLED" : "DISABLED");
    }
}

static void wav_playback_complete_if_idle(void)
{
    bool restored = false;
    bool synth_mode = false;
    portENTER_CRITICAL(&s_wav_lock);
    if (s_wav_playback_active && s_wav_pending_bytes == 0) {
        s_wav_playback_active = false;
        if (s_wav_prev_valid) {
            s_force_synth = s_wav_prev_force_synth;
            synth_mode = s_force_synth;
            s_wav_prev_valid = false;
            restored = true;
        }
    }
    portEXIT_CRITICAL(&s_wav_lock);

    if (restored) {
        ESP_LOGI(TAG, "audio_processor: WAV playback completed (restored synth=%s)", synth_mode ? "ENABLED" : "DISABLED");
    }
}

static void wav_stream_mutex_init(void)
{
    if (s_wav_mutex != NULL) {
        return;
    }

    s_wav_mutex = xSemaphoreCreateMutex();
    if (s_wav_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to allocate WAV stream mutex");
    }
}

static inline void wav_stream_reset_residual_locked(void)
{
    s_wav_send_residual_len = 0;
    s_wav_send_residual_pos = 0;
    s_wav_residual_tag = AUDIO_SOURCE_TAG_INVALID;
    s_wav_residual_tag_valid = false;
}

static size_t wav_stream_frame_bytes_dst(void)
{
    if (s_wav_stream.frame_bytes_dst != 0U) {
        return s_wav_stream.frame_bytes_dst;
    }

    size_t bytes_per_sample = (size_t)audio_bytes_per_sample(s_audio_config.bit_depth);
    if (bytes_per_sample == 0U) {
        bytes_per_sample = 2U;
    }
    size_t channels = (s_audio_config.channels == AUDIO_CHANNEL_MONO) ? 1U : 2U;
    if (channels == 0U) {
        channels = 2U;
    }
    return bytes_per_sample * channels;
}

static bool wav_stream_flush_residual_locked(void)
{
    if (s_audio_buffer == NULL) {
        return false;
    }

    if (s_wav_send_residual_len == 0 || s_wav_send_residual_pos >= s_wav_send_residual_len) {
        wav_stream_reset_residual_locked();
        return false;
    }

    size_t frame_bytes = wav_stream_frame_bytes_dst();
    size_t max_item = xRingbufferGetMaxItemSize(s_audio_buffer);
    bool pending = false;

    while (s_wav_send_residual_pos < s_wav_send_residual_len) {
        size_t remaining = s_wav_send_residual_len - s_wav_send_residual_pos;
        size_t send_size = remaining;
        if (max_item > 0U && send_size > max_item) {
            send_size = max_item;
        }
        if (frame_bytes > 0U && send_size >= frame_bytes) {
            size_t aligned = (send_size / frame_bytes) * frame_bytes;
            if (aligned == 0U) {
                aligned = frame_bytes;
            }
            if (aligned > send_size) {
                aligned = send_size;
            }
            send_size = aligned;
        }
        if (send_size == 0U) {
            pending = true;
            break;
        }

        audio_source_tag_t tag = s_wav_residual_tag_valid ? s_wav_residual_tag : AUDIO_SOURCE_TAG_WAV;
        if (!audio_source_tag_push(tag)) {
            pending = true;
            break;
        }

        BaseType_t sent = xRingbufferSend(s_audio_buffer,
                                          s_wav_send_residual + s_wav_send_residual_pos,
                                          send_size,
                                          0);
        if (sent != pdTRUE) {
            audio_source_tag_drop_one();
            pending = true;
            break;
        }

        wav_playback_add_pending(send_size);
        worker_diag_report(WORKER_DIAG_SOURCE_WAV, send_size, sent);
        printf("DIAG-APLAY-STREAM: residual-send=%ld size=%zu\n", (long)sent, send_size);
        s_wav_send_residual_pos += send_size;
    }

    if (!pending || s_wav_send_residual_pos >= s_wav_send_residual_len) {
        wav_stream_reset_residual_locked();
        pending = false;
    }

    return pending;
}

/* Ensure prototype is visible before first use in this translation unit.
 * Some callers appear earlier in the file; declare the signature so the
 * compiler doesn't assume an implicit int-returning declaration. */
static size_t wav_stream_try_enqueue_unlocked(const uint8_t *data, size_t len, audio_source_tag_t source_tag);

static bool wav_stream_queue_data_locked(const uint8_t *data, size_t len)
{
    if (data == NULL || len == 0U || s_audio_buffer == NULL) {
        return false;
    }

    size_t sent = wav_stream_try_enqueue_unlocked(data, len, AUDIO_SOURCE_TAG_WAV);
    if (sent < len) {
        size_t residual = len - sent;
        if (residual > sizeof(s_wav_send_residual)) {
            residual = sizeof(s_wav_send_residual);
        }
        memcpy(s_wav_send_residual, data + sent, residual);
        s_wav_send_residual_len = residual;
        s_wav_send_residual_pos = 0U;
        s_wav_residual_tag = AUDIO_SOURCE_TAG_WAV;
        s_wav_residual_tag_valid = true;
        printf("DIAG-APLAY-STREAM: backlog=%zu\n", residual);
        return true;
    }

    return false;
}

/* Attempt to enqueue `len` bytes from `data` into the audio ringbuffer without
 * holding the WAV mutex. Returns the number of bytes successfully enqueued.
 * This function does not modify WAV playback shared state (residual buffers)
 * and is safe to call without `s_wav_mutex` held. It will perform paced
 * retries with a small delay when the ringbuffer is temporarily full. */
static size_t wav_stream_try_enqueue_unlocked(const uint8_t *data, size_t len, audio_source_tag_t source_tag)
{
    if (data == NULL || len == 0U || s_audio_buffer == NULL) {
        return 0U;
    }

    size_t frame_bytes = wav_stream_frame_bytes_dst();
    size_t max_item = xRingbufferGetMaxItemSize(s_audio_buffer);
    size_t offset = 0U;

    /* Target producer chunk size to match typical consumer fetch size.
     * Keep this conservative and align to frame size so the consumer always
     * receives full frames. This reduces bursty enqueues that fill the
     * ringbuffer and cause overruns / backpressure. */
    const size_t PRODUCER_CHUNK_TARGET = 512U;
    size_t producer_chunk = PRODUCER_CHUNK_TARGET;
    if (frame_bytes > 0U) {
        producer_chunk = (producer_chunk / frame_bytes) * frame_bytes;
        if (producer_chunk == 0U) producer_chunk = frame_bytes;
    }

    while (offset < len) {
    size_t remaining = len - offset;
        size_t free_size = xRingbufferGetCurFreeSize(s_audio_buffer);

        if (free_size == 0U) {
            /* backpressure: yield a bit and return what we've queued so far */
            vTaskDelay(pdMS_TO_TICKS(1));
            break;
        }

        if (max_item > 0U) {
            if (remaining > max_item) {
                remaining = max_item;
            }
            if (free_size > max_item) {
                free_size = max_item;
            }
        }

        size_t chunk_size = remaining;
        if (chunk_size > free_size) {
            chunk_size = free_size;
        }

        /* Limit chunk size to a conservative producer_chunk to avoid
         * enqueuing very large items (e.g. 4 KiB) that the consumer
         * reads in small pieces. This keeps the ringbuffer levels
         * stable and reduces overruns. */
        if (producer_chunk > 0U && chunk_size > producer_chunk) {
            chunk_size = producer_chunk;
        }

        if (frame_bytes > 0U) {
            if (free_size < frame_bytes) {
                /* Not enough room for a full frame; yield and stop */
                vTaskDelay(pdMS_TO_TICKS(1));
                break;
            }
            chunk_size = (chunk_size / frame_bytes) * frame_bytes;
            if (chunk_size == 0U) {
                vTaskDelay(pdMS_TO_TICKS(1));
                break;
            }
        } else if (chunk_size == 0U) {
            vTaskDelay(pdMS_TO_TICKS(1));
            break;
        }

        /* Try sending with a short wait to avoid long spinlock holds inside
         * the ringbuffer implementation. Also capture lightweight timing
         * info to help diagnose stalls that previously triggered the
         * interrupt WDT. Keep logs at debug level to avoid noisy output. */
        /* Try sending with a short wait; if it fails, do a bounded retry
         * with brief yields to avoid busy spins and to give the consumer a
         * chance to free space. This prevents task watchdog triggers from
         * tight enqueue loops. */
        if (!audio_source_tag_push(source_tag)) {
            break;
        }

        int retry = 0;
        const int max_retries = 3;
        BaseType_t sent = pdFALSE;
        while (retry <= max_retries) {
            int64_t t_before = esp_timer_get_time();
            sent = xRingbufferSend(s_audio_buffer, data + offset, chunk_size, pdMS_TO_TICKS(1));
            int64_t t_after = esp_timer_get_time();
            if (t_after - t_before > 5000) {
                ESP_LOGW(TAG, "ringbuf send stalled: took %lld us chunk=%zu free=%zu",
                         (long long)(t_after - t_before), chunk_size, free_size);
            } else {
                ESP_LOGD(TAG, "ringbuf send: took %lld us chunk=%zu",
                         (long long)(t_after - t_before), chunk_size);
            }
            if (sent == pdTRUE) break;
            /* not sent: small backoff and try a couple more times */
            vTaskDelay(pdMS_TO_TICKS(1));
            retry++;
        }
        if (sent != pdTRUE) {
            audio_source_tag_drop_one();
            /* Could not send promptly; yield and stop so caller will save
             * residuals and avoid long blocking. */
            vTaskDelay(pdMS_TO_TICKS(1));
            break;
        }

        if (source_tag == AUDIO_SOURCE_TAG_WAV) {
            wav_playback_add_pending(chunk_size);
        }
        worker_diag_source_t diag_src = (source_tag == AUDIO_SOURCE_TAG_WAV) ? WORKER_DIAG_SOURCE_WAV : WORKER_DIAG_SOURCE_WORKER;
        worker_diag_report(diag_src, chunk_size, sent);
        offset += chunk_size;
    }

    return offset;
}

static esp_err_t wav_stream_fill_locked(void)
{
    if (!s_wav_stream.active || s_wav_stream.file == NULL) {
        return ESP_OK;
    }

    if (s_audio_buffer == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (wav_stream_flush_residual_locked()) {
        return ESP_OK;
    }

    const int max_iterations = 4;
    for (int iter = 0; iter < max_iterations && s_wav_stream.remaining_bytes > 0U; ++iter) {
        size_t frame_src = (s_wav_stream.frame_bytes_src != 0U) ? s_wav_stream.frame_bytes_src : 1U;
        size_t work_bytes = audio_get_runtime_work_bytes();
        if (work_bytes < frame_src) {
            work_bytes = frame_src;
        }
        size_t to_read = work_bytes;
        to_read = (to_read / frame_src) * frame_src;
        if (to_read == 0U) {
            to_read = frame_src;
        }
        if (to_read > s_wav_stream.remaining_bytes) {
            to_read = s_wav_stream.remaining_bytes;
        }
        if (to_read == 0U) {
            break;
        }

        size_t raw_read = fread(s_proc_buffer, 1, to_read, s_wav_stream.file);
        if (raw_read == 0U) {
            s_wav_stream.remaining_bytes = 0U;
            break;
        }

        if (raw_read > s_wav_stream.remaining_bytes) {
            s_wav_stream.remaining_bytes = 0U;
        } else {
            s_wav_stream.remaining_bytes -= raw_read;
        }

        size_t conv_size = 0U;
        esp_err_t conv_ret = convert_audio_format(s_proc_buffer,
                                                  s_proc_buffer,
                                                  raw_read,
                                                  s_wav_stream.src_bit_depth,
                                                  s_audio_config.bit_depth,
                                                  &conv_size);
        if (conv_ret != ESP_OK) {
            return conv_ret;
        }

        size_t res_size = 0U;
        esp_err_t res_ret = resample_audio(s_proc_buffer,
                                           s_proc_buffer2,
                                           conv_size,
                                           s_wav_stream.src_sample_rate,
                                           s_audio_config.sample_rate,
                                           &res_size);
        if (res_ret != ESP_OK) {
            return res_ret;
        }

        if (res_size == 0U) {
            continue;
        }

        /* Release wav mutex while doing potentially long ringbuffer sends.
         * We perform the actual sends in an unlocked helper so we don't hold
         * `s_wav_mutex` (the WAV state mutex) while entering ringbuffer
         * critical sections. Any remaining bytes are stored back into the
         * residual buffer under the mutex. */
    size_t sent_bytes = 0;
    xSemaphoreGive(s_wav_mutex);
    sent_bytes = wav_stream_try_enqueue_unlocked(s_proc_buffer2, res_size, AUDIO_SOURCE_TAG_WAV);
        /* Re-acquire WAV mutex to update shared state */
        if (xSemaphoreTake(s_wav_mutex, portMAX_DELAY) != pdTRUE) {
            /* Should not happen; treat as error */
            return ESP_FAIL;
        }

        if (sent_bytes < res_size) {
            size_t residual = res_size - sent_bytes;
            if (residual > sizeof(s_wav_send_residual)) {
                residual = sizeof(s_wav_send_residual);
            }
            memcpy(s_wav_send_residual, s_proc_buffer2 + sent_bytes, residual);
            s_wav_send_residual_len = residual;
            s_wav_send_residual_pos = 0U;
            s_wav_residual_tag = AUDIO_SOURCE_TAG_WAV;
            s_wav_residual_tag_valid = true;
            /* indicate we could not send all data */
            break;
        }

        if (wav_stream_flush_residual_locked()) {
            break;
        }
    }

    return ESP_OK;
}

static bool wav_stream_clear_locked(bool close_file)
{
    bool resume_needed = s_wav_stream.resume_pipeline;

    if (close_file && s_wav_stream.file != NULL) {
        fclose(s_wav_stream.file);
    }

    s_wav_stream.active = false;
    s_wav_stream.resume_pipeline = false;
    s_wav_stream.file = NULL;
    s_wav_stream.src_bit_depth = s_audio_config.bit_depth;
    s_wav_stream.src_sample_rate = s_audio_config.sample_rate;
    s_wav_stream.frame_bytes_src = 0U;
    s_wav_stream.frame_bytes_dst = 0U;
    s_wav_stream.remaining_bytes = 0U;
    wav_stream_reset_residual_locked();

    return resume_needed;
}

static void wav_stream_try_refill(void)
{
    if (s_wav_mutex == NULL) {
        return;
    }

    bool resume_needed = false;
    esp_err_t fill_ret = ESP_OK;

    if (xSemaphoreTake(s_wav_mutex, portMAX_DELAY) == pdTRUE) {
        if (s_wav_stream.active) {
            fill_ret = wav_stream_fill_locked();
            if (fill_ret != ESP_OK) {
                ESP_LOGE(TAG, "wav_stream_fill_locked failed (%d %s)", (int)fill_ret, esp_err_to_name(fill_ret));
                s_wav_stream.resume_pipeline = true;
                resume_needed = wav_stream_clear_locked(true);
            } else if (s_wav_stream.remaining_bytes == 0U && s_wav_pending_bytes == 0U && s_wav_send_residual_len == 0U) {
                resume_needed = wav_stream_clear_locked(true);
            }
        }
        xSemaphoreGive(s_wav_mutex);
    }

    if (fill_ret != ESP_OK) {
        wav_playback_abort();
    }

    if (resume_needed) {
        wav_playback_complete_if_idle();
        if (s_is_initialized) {
            esp_err_t restart_ret = audio_processor_start();
            if (restart_ret != ESP_OK) {
                ESP_LOGE(TAG, "wav_stream_try_refill: failed to resume pipeline (%d %s)", (int)restart_ret, esp_err_to_name(restart_ret));
            }
        }
    }
}

static void wav_stream_abort(bool allow_resume)
{
    if (s_wav_mutex == NULL) {
        return;
    }

    bool resume_needed = false;

    if (xSemaphoreTake(s_wav_mutex, portMAX_DELAY) == pdTRUE) {
        if (s_wav_stream.active || s_wav_stream.file != NULL) {
            if (!allow_resume) {
                s_wav_stream.resume_pipeline = false;
            }
            resume_needed = wav_stream_clear_locked(true);
        }
        xSemaphoreGive(s_wav_mutex);
    }

    /* If abort was requested without allowing resume and we cleared the
     * WAV stream state, also drop any pending metadata tags so the tag
     * buffer doesn't reference audio items that were discarded. */
    if (!allow_resume && resume_needed) {
        audio_source_tag_reset_buffer();
    }

    if (allow_resume && resume_needed) {
        wav_playback_complete_if_idle();
        if (s_is_initialized) {
            esp_err_t restart_ret = audio_processor_start();
            if (restart_ret != ESP_OK) {
                ESP_LOGE(TAG, "wav_stream_abort: failed to resume pipeline (%d %s)", (int)restart_ret, esp_err_to_name(restart_ret));
            }
        }
    }
}

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
    size_t min_capacity = audio_ringbuffer_min_capacity(frame_bytes);

    size_t capacity = AUDIO_BUFFER_SIZE;
    if (capacity < min_capacity) {
        capacity = min_capacity;
    }

    if (capacity == 0) {
        capacity = min_capacity;
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

#ifdef CONFIG_BT_MOCK_TESTING
/* Simple mock I2S audio generator used for host-unit tests. Produces a
 * deterministic byte pattern so consumers can validate reads. Returns the
 * number of bytes written into `buffer`. */
static size_t mock_generate_i2s_audio(uint8_t* buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0) return 0;
    /* Fill with a simple 8-bit saw pattern to make the data non-zero and
     * deterministic for assertions in tests. */
    for (size_t i = 0; i < buffer_size; ++i) {
        buffer[i] = (uint8_t)((i + s_mock_i2s_state.frame_counter) & 0xFF);
    }
    /* Advance the frame counter by a coarse amount (not exact frames).
     * Tests don't depend on precise sample counting, just reproducibility. */
    s_mock_i2s_state.frame_counter += (uint32_t)(buffer_size / 2);
    return buffer_size;
}
#else
static size_t mock_generate_i2s_audio(uint8_t* buffer, size_t buffer_size);
#endif
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
/* Forward declare wav_stream_try_enqueue_unlocked so callers earlier in
 * this translation unit see the correct signature and the compiler does
 * not assume an implicit int-returning declaration. */
static size_t wav_stream_try_enqueue_unlocked(const uint8_t *data, size_t len, audio_source_tag_t source_tag);

/* Diagnostic helper forward-declaration (defined later) */
static void diag_dump_bytes(const void* data, size_t len, const char* tag);
static void drain_beep_buffer(void);

/* Runtime control: allow forcing DRAM-only allocations for debugging/static avoidance */
/* Temporarily default to true for Option 2: boot with DRAM-only allocations
 * (this avoids PSRAM placement for audio buffers so we can A/B the beep).
 * Revert this change after debugging or make persistent via Kconfig/NVS as needed. */
#if (defined(CONFIG_SPIRAM) && CONFIG_SPIRAM) || (defined(CONFIG_SPIRAM_SUPPORT) && CONFIG_SPIRAM_SUPPORT)
static bool s_dram_only_alloc = false;
#else
static bool s_dram_only_alloc = true;
#endif

void audio_processor_set_dram_only(bool enable)
{
    AUDIO_PROC_LOG_ONCE();
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

    /* Choose a small chunk size friendly to the beep buffer and DMA. */
    size_t chunk_bytes = 4 * 1024;
    if (chunk_bytes > (size_t)BEEP_BUFFER_SIZE) chunk_bytes = (size_t)BEEP_BUFFER_SIZE;
    chunk_bytes = (chunk_bytes / frame_bytes) * frame_bytes;
    if (chunk_bytes == 0) chunk_bytes = frame_bytes;

    size_t sent_total = 0;
    const TickType_t chunk_wait = pdMS_TO_TICKS(10);

    while (sent_total < total_bytes) {
        size_t remaining = total_bytes - sent_total;
        size_t this_chunk = remaining > chunk_bytes ? chunk_bytes : remaining;

        /* Push a matching metadata tag for this beep chunk so the tag
         * buffer remains aligned with audio items regardless of which
         * ringbuffer holds them. If the send fails we will drop the tag
         * to keep counts balanced. */
        (void)audio_source_tag_push(AUDIO_SOURCE_TAG_BEEP);
        BaseType_t ok = xRingbufferSend(s_beep_buffer, data + sent_total, this_chunk, chunk_wait);
        if (ok == pdTRUE) {
            sent_total += this_chunk;
            continue;
        }

        /* Try freeing a little space by dropping oldest items from the beep buffer. */
        int drop_attempts = 0;
        const int max_drop_attempts = 4;
        while (xRingbufferGetCurFreeSize(s_beep_buffer) < this_chunk && drop_attempts < max_drop_attempts) {
            size_t rsz = 0;
            void* it = xRingbufferReceiveUpTo(s_beep_buffer, &rsz, 0, this_chunk);
            if (it == NULL || rsz == 0) break;
            /* Return the item to free the space (we're intentionally dropping it) */
            /* Drop the matching metadata tag for the item we removed. */
            audio_source_tag_drop_one();
            vRingbufferReturnItem(s_beep_buffer, it);
            drop_attempts++;
        }

        /* After dropping a few items try one immediate send without waiting. */
        ok = xRingbufferSend(s_beep_buffer, data + sent_total, this_chunk, 0);
        if (ok == pdTRUE) {
            sent_total += this_chunk;
            continue;
        }

        /* Could not enqueue this chunk; give up and return what we managed. */
        /* Failed to enqueue: remove the tag we pushed for this chunk so
         * the tag buffer remains balanced. */
        audio_source_tag_drop_one();
        break;
    }

    return sent_total;
}


/**
 * @brief Initialize the audio processor
 */
esp_err_t audio_processor_init(const audio_config_t* config)
{
    AUDIO_PROC_LOG_ONCE();
    if (s_is_initialized) {
        ESP_LOGW(TAG, "Audio processor already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (config == NULL) {
        ESP_LOGE(TAG, "Null config provided");
        return ESP_ERR_INVALID_ARG;
    }

    wav_stream_mutex_init();

    /* Ensure diagnostics requested for current investigation are emitted. */
    esp_log_level_set(TAG, ESP_LOG_INFO);

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
    /* If PSRAM is not available at runtime, prefer a conservative initial
     * allocation target to avoid exhausting DRAM and starving other
     * subsystems (for example the Bluetooth stack). The allocation loop
     * below will still try larger sizes if necessary, but starting lower
     * prevents happily succeeding with a very large DRAM allocation that
     * later causes malloc failures elsewhere. */
    if (!runtime_psram_ready) {
        /* Lower the initial conservative allocation on DRAM-only boards to
         * reduce startup pressure and increase the chance the Bluetooth
         * stack retains sufficient free/contiguous heap for A2DP startup. */
        const size_t dram_initial_cap = 16 * 1024; /* 16 KiB conservative start (reduced from 32K) */
        if (try_capacity > dram_initial_cap) {
            ESP_LOGW(TAG, "Runtime: PSRAM absent — capping initial audio buffer target to %zu bytes to reduce DRAM pressure", dram_initial_cap);
            try_capacity = dram_initial_cap;
        }
    }
    /* Keep the initial target at the full computed capacity; the retry loop
     * below will progressively reduce the size if allocations fail. This
     * avoids preemptively constraining DRAM-only boards to 32 KiB and gives
     * more headroom for larger resampled audio blocks. */
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

    size_t desired_min = audio_ringbuffer_min_capacity(frame_bytes);
    if (desired_min < block_requirement) {
        desired_min = block_requirement;
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
                                /* Use BYTEBUF so the consumer's xRingbufferReceiveUpTo usage
                                 * remains valid. The consumer currently assumes byte-buffer
                                 * semantics (it expects ReceiveUpTo behavior). If we later
                                 * switch to ALLOWSPLIT we must also update the consumer to
                                 * use split-aware receive APIs or ensure the producer chunks
                                 * items no larger than the consumer's requests. */
                                            s_audio_buffer = xRingbufferCreateWithCaps(try_capacity, RINGBUF_TYPE_BYTEBUF,
                                                                                                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (s_audio_buffer != NULL) {
                size_t max_item = xRingbufferGetMaxItemSize(s_audio_buffer);
                ESP_LOGI(TAG, "Audio buffer created in PSRAM (%zu bytes capacity, max item %zu)",
                         try_capacity,
                         max_item);
                break;
            }
            ESP_LOGW(TAG, "xRingbufferCreateWithCaps(PSRAM) failed for %zu, falling back to default allocator", try_capacity);
        }

    s_audio_buffer = xRingbufferCreate(try_capacity, RINGBUF_TYPE_BYTEBUF);
        if (s_audio_buffer != NULL) {
            size_t max_item = xRingbufferGetMaxItemSize(s_audio_buffer);
            ESP_LOGI(TAG, "Audio buffer created (%zu bytes capacity, max item %zu)",
                     try_capacity,
                     max_item);
            break;
        }

        ESP_LOGW(TAG, "xRingbufferCreate(%zu) failed, trying smaller size", try_capacity);
        if (try_capacity == min_capacity) {
            break;
        }

        try_capacity = try_capacity / 2U;
        try_capacity = (try_capacity + 3U) & ~((size_t)3U);
    }

    /* Create metadata tag buffer to track the source for each audio chunk.
     * Prefer PSRAM when available to keep DRAM pressure low. */
    s_audio_source_buffer = NULL;
    size_t tag_capacity = (size_t)AUDIO_SOURCE_BUFFER_SIZE;
    bool psram_ready_tags = false;
#if (defined(CONFIG_SPIRAM) && CONFIG_SPIRAM) || (defined(CONFIG_SPIRAM_SUPPORT) && CONFIG_SPIRAM_SUPPORT)
    psram_ready_tags = esp_psram_is_initialized();
#endif
    if (s_dram_only_alloc) {
        psram_ready_tags = false;
    }

    if (psram_ready_tags) {
        s_audio_source_buffer = xRingbufferCreateWithCaps(tag_capacity, RINGBUF_TYPE_BYTEBUF,
                                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_audio_source_buffer != NULL) {
            ESP_LOGI(TAG, "Metadata source buffer created in PSRAM (%zu bytes)", tag_capacity);
        } else {
            ESP_LOGW(TAG, "audio_processor_init: failed to create metadata buffer in PSRAM, falling back to DRAM");
        }
    }

    if (s_audio_source_buffer == NULL) {
        s_audio_source_buffer = xRingbufferCreate(tag_capacity, RINGBUF_TYPE_BYTEBUF);
        if (s_audio_source_buffer != NULL) {
            ESP_LOGI(TAG, "Metadata source buffer created (%zu bytes)", tag_capacity);
        } else {
            ESP_LOGE(TAG, "audio_processor_init: failed to create metadata buffer (%zu bytes)", tag_capacity);
            vRingbufferDelete(s_audio_buffer);
            s_audio_buffer = NULL;
            return ESP_ERR_NO_MEM;
        }
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
        if (s_audio_source_buffer != NULL) {
            vRingbufferDelete(s_audio_source_buffer);
            s_audio_source_buffer = NULL;
        }
        if (s_beep_buffer != NULL) {
            vRingbufferDelete(s_beep_buffer);
            s_beep_buffer = NULL;
        }
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
    s_runtime_work_bytes = 0U;
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
            s_runtime_work_bytes = try_work_bytes;
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
        if (s_audio_source_buffer != NULL) {
            vRingbufferDelete(s_audio_source_buffer);
            s_audio_source_buffer = NULL;
        }
        if (s_beep_buffer != NULL) {
            vRingbufferDelete(s_beep_buffer);
            s_beep_buffer = NULL;
        }
        vRingbufferDelete(s_audio_buffer);
        s_audio_buffer = NULL;
        return ESP_ERR_NO_MEM;
    }

    // Configure I2S
    esp_err_t ret = configure_i2s(config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure I2S: %d", ret);
        if (s_audio_source_buffer != NULL) {
            vRingbufferDelete(s_audio_source_buffer);
            s_audio_source_buffer = NULL;
        }
        if (s_beep_buffer != NULL) {
            vRingbufferDelete(s_beep_buffer);
            s_beep_buffer = NULL;
        }
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
        if (s_audio_source_buffer != NULL) {
            vRingbufferDelete(s_audio_source_buffer);
            s_audio_source_buffer = NULL;
        }
        if (s_beep_buffer != NULL) {
            vRingbufferDelete(s_beep_buffer);
            s_beep_buffer = NULL;
        }
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
        if (s_audio_source_buffer != NULL) {
            vRingbufferDelete(s_audio_source_buffer);
            s_audio_source_buffer = NULL;
        }
        if (s_beep_buffer != NULL) {
            vRingbufferDelete(s_beep_buffer);
            s_beep_buffer = NULL;
        }
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
        if (s_audio_source_buffer != NULL) {
            vRingbufferDelete(s_audio_source_buffer);
            s_audio_source_buffer = NULL;
        }
        if (s_beep_buffer != NULL) {
            vRingbufferDelete(s_beep_buffer);
            s_beep_buffer = NULL;
        }
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

    if (s_runtime_work_bytes == 0U) {
        s_runtime_work_bytes = (size_t)AUDIO_WORK_BUFFER_BYTES;
    }

    ESP_LOGI(TAG, "Audio processor initialized: %dHz, %d-bit, %d channels", 
             config->sample_rate, config->bit_depth, config->channels);

    return ESP_OK;
}

/**
 * @brief Deinitialize the audio processor
 */
esp_err_t audio_processor_deinit(void)
{
    AUDIO_PROC_LOG_ONCE();
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

    wav_stream_abort(false);

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
    s_runtime_work_bytes = 0U;

    if (s_audio_source_buffer != NULL) {
        audio_source_tag_reset_buffer();
        vRingbufferDelete(s_audio_source_buffer);
        s_audio_source_buffer = NULL;
    }

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
    AUDIO_PROC_LOG_ONCE();
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
    AUDIO_PROC_LOG_ONCE();
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

    wav_playback_abort();
    wav_stream_abort(false);

    return ESP_OK;
}

/**
 * @brief Set the output sample rate
 */
esp_err_t audio_processor_set_sample_rate(audio_sample_rate_t sample_rate)
{
    AUDIO_PROC_LOG_ONCE();
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
    AUDIO_PROC_LOG_ONCE();
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
    AUDIO_PROC_LOG_ONCE();
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
    AUDIO_PROC_LOG_ONCE();
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
    AUDIO_PROC_LOG_ONCE();
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
    AUDIO_PROC_LOG_ONCE();
    if (!s_is_initialized) {
        ESP_LOGE(TAG, "Audio processor not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (buffer == NULL || bytes_read == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }

    if (s_trace_next_read_call) {
        s_trace_next_read_call = false;
        const char *task_name = pcTaskGetName(NULL);
        if (task_name == NULL) {
            task_name = "<unknown>";
        }
        ESP_LOGI(TAG, "audio_processor_read trace: task=%s request_size=%zu", task_name, size);
        printf("TRACE: audio_processor_read task=%s request_size=%zu\n", task_name, size);
        esp_backtrace_print(10);
    }

    // If muted and no beep override/fallback is pending, just fill with zeros
    if (s_audio_config.mute && s_beep_remaining_bytes == 0 && !s_beep_fallback_active) {
        /* If the dedicated beep buffer is empty (or unavailable) then
         * there's nothing to play — return silence. If the beep buffer
         * contains data we should continue so the urgent tone can play. */
        if (s_beep_buffer == NULL || xRingbufferGetCurFreeSize(s_beep_buffer) == BEEP_BUFFER_SIZE) {
            memset(buffer, 0, size);
            *bytes_read = size;
            wav_stream_try_refill();
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
    bool wav_override_beep = wav_playback_is_active();
    if (wav_override_beep && s_beep_remaining_bytes > 0) {
        ESP_LOGD(TAG, "audio_processor_read: WAV playback suppressing %zu pending beep bytes", s_beep_remaining_bytes);
    }

    /* Drain any previously saved beep residual data so queued tones retain
     * ordering even when callers request smaller buffers than the enqueued
     * chunk size. */
    if (!wav_override_beep && bytes_written < size && s_beep_rb_residual_len > s_beep_rb_residual_pos) {
        size_t copied = residual_copy(buffer + bytes_written, size - bytes_written,
                                      s_beep_rb_residual, &s_beep_rb_residual_pos,
                                      &s_beep_rb_residual_len);
        if (copied > 0) {
            bytes_written += copied;
            if (s_beep_remaining_bytes > copied) s_beep_remaining_bytes -= copied;
            else s_beep_remaining_bytes = 0;
        }
    }

    /* Drain any urgent beep data from the small beep buffer so queued tones
     * are emitted with lower latency than the main ringbuffer. Store any
     * unconsumed tail in the residual buffer for the next read. */
    if (!wav_override_beep && s_beep_buffer != NULL && bytes_written < size) {
        while (bytes_written < size) {
            size_t read_sz = 0;
            size_t free_before = xRingbufferGetCurFreeSize(s_beep_buffer);
            void* itm = xRingbufferReceive(s_beep_buffer, &read_sz, 0);
            if (itm == NULL || read_sz == 0) {
                ESP_LOGI(TAG, "audio_processor_read: beep dequeue empty free_before=%zu", free_before);
                printf("DIAG-READ-BEEP-DEQ: empty free_before=%zu\n", free_before);
            } else {
                ESP_LOGI(TAG, "audio_processor_read: beep dequeue len=%zu free_before=%zu", read_sz, free_before);
                printf("DIAG-READ-BEEP-DEQ: len=%zu free_before=%zu\n", read_sz, free_before);
            }
            if (itm == NULL || read_sz == 0) {
                break;
            }

            size_t to_copy = read_sz;
            size_t remaining = size - bytes_written;
            if (to_copy > remaining) {
                to_copy = remaining;
            }

            if (to_copy > 0) {
                    printf("DIAG-READ-START: size=%zu init=%d run=%d rb=%p\n",
                           size,
                           (int)s_is_initialized,
                           (int)s_is_running,
                           (void*)s_audio_buffer);
                memcpy(buffer + bytes_written, itm, to_copy);
                bytes_written += to_copy;
                if (s_beep_remaining_bytes > 0) {
                    if (to_copy >= s_beep_remaining_bytes) s_beep_remaining_bytes = 0;
                    else s_beep_remaining_bytes -= to_copy;
                }
            }
                            printf("DIAG-READ-EXIT: mute-silence bytes=%zu ret=ESP_OK\n", size);

            size_t leftover = (read_sz > to_copy) ? (read_sz - to_copy) : 0;
            if (leftover > 0) {
            printf("DIAG-READ-BEEP-DEQ: empty free_before=%zu\n", free_before);
                size_t stored = residual_store((const uint8_t*)itm + to_copy, leftover,
                                               s_beep_rb_residual, sizeof(s_beep_rb_residual),
                                               &s_beep_rb_residual_pos, &s_beep_rb_residual_len);
                if (stored < leftover) {
                    size_t dropped = leftover - stored;
                    ESP_LOGW(TAG, "audio_processor_read: beep residual truncated %zu -> %zu", leftover, stored);
                    s_audio_stats.buffer_overruns++;
                    if (s_beep_remaining_bytes > dropped) {
                        s_beep_remaining_bytes -= dropped;
                    } else {
                        s_beep_remaining_bytes = 0;
                    }
                }
            } else {
                s_beep_rb_residual_len = 0;
                s_beep_rb_residual_pos = 0;
            }

            size_t free_after = xRingbufferGetCurFreeSize(s_beep_buffer);
            ESP_LOGI(TAG, "audio_processor_read: beep return len=%zu free_after=%zu", read_sz, free_after);
            printf("DIAG-READ-BEEP-RET: len=%zu free_after=%zu\n", read_sz, free_after);
            printf("DIAG-READ-BEEP-RET: len=%zu free_after=%zu\n", read_sz, free_after);
            /* Consume any matching metadata tag for this beep item so the
             * tag ringbuffer stays aligned with audio items that came
             * from the beep buffer. This is a non-blocking take; if no
             * tag is present the call simply returns false. */
            audio_source_tag_take(NULL, 0);
            vRingbufferReturnItem(s_beep_buffer, itm);

            if (to_copy == 0) {
                break;
            }
        }
    }

    if (!wav_override_beep && s_beep_fallback_active) {
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
            printf("DIAG-READ-EXIT: fallback bytes=%zu ret=ESP_OK\n", bytes_written);
            wav_stream_try_refill();
            return ESP_OK;
        }
    }

    /* If we filled the requested size entirely from the fallback generator,
     * apply volume and return immediately. */
    if (bytes_written == size) {
        if (s_volume_gain < 100) apply_volume(buffer, bytes_written, s_volume_gain);
        *bytes_read = bytes_written;
        log_read_summary("fallback", size, bytes_written);
        printf("DIAG-READ-EXIT: fallback-only bytes=%zu ret=ESP_OK\n", bytes_written);
        wav_stream_try_refill();
        return ESP_OK;
    }

    /* Otherwise, try to pull the remainder from the ringbuffer using the
     * same residual-handling strategy. */
    while (bytes_written < size) {
        size_t copied = residual_copy(buffer + bytes_written, size - bytes_written,
                                      s_audio_rb_residual, &s_audio_rb_residual_pos,
                                      &s_audio_rb_residual_len);
        if (copied > 0) {
            bytes_written += copied;
            wav_playback_consume(copied);
            continue;
        }

        size_t read_size = 0;
        size_t free_before = xRingbufferGetCurFreeSize(s_audio_buffer);
        size_t max_fetch = size - bytes_written;
        if (max_fetch == 0) {
            break;
        }
        TickType_t wait_ticks = 0;
        bool wav_active = wav_playback_is_active();
        if (wav_active) {
            wait_ticks = pdMS_TO_TICKS(50);
        }
        size_t wav_remaining_snapshot = 0;
        bool wav_remaining_valid = false;
        if (wav_active && s_wav_mutex != NULL && xSemaphoreTake(s_wav_mutex, 0) == pdTRUE) {
            wav_remaining_snapshot = s_wav_stream.remaining_bytes;
            wav_remaining_valid = true;
            xSemaphoreGive(s_wav_mutex);
        }
        size_t rb_max_item = xRingbufferGetMaxItemSize(s_audio_buffer);
     ESP_LOGI(TAG,
           "DIAG-READ-AUDIO-REQ: max_fetch=%lu wait_ticks=%lu wav_active=%d wav_pending=%lu wav_remaining=%lu valid=%d rb_free=%lu rb_max_item=%lu",
           (unsigned long)max_fetch,
           (unsigned long)wait_ticks,
           wav_active ? 1 : 0,
           (unsigned long)s_wav_pending_bytes,
           (unsigned long)wav_remaining_snapshot,
           wav_remaining_valid ? 1 : 0,
           (unsigned long)free_before,
           (unsigned long)rb_max_item);
     printf("DIAG-READ-AUDIO-REQ: max_fetch=%lu wait_ticks=%lu wav_active=%d wav_pending=%lu wav_remaining=%lu wav_remaining_valid=%d rb_free=%lu rb_max_item=%lu\n",
         (unsigned long)max_fetch,
         (unsigned long)wait_ticks,
         wav_active ? 1 : 0,
         (unsigned long)s_wav_pending_bytes,
         (unsigned long)wav_remaining_snapshot,
         wav_remaining_valid ? 1 : 0,
         (unsigned long)free_before,
         (unsigned long)rb_max_item);
        void* item = xRingbufferReceiveUpTo(s_audio_buffer, &read_size, wait_ticks, max_fetch);
     ESP_LOGI(TAG,
           "DIAG-READ-AUDIO-ITEM: ptr=%p size=%lu wait_ticks=%lu max_fetch=%lu free_before=%lu wav_pending=%lu",
           item,
           (unsigned long)read_size,
           (unsigned long)wait_ticks,
           (unsigned long)max_fetch,
           (unsigned long)free_before,
           (unsigned long)s_wav_pending_bytes);
     printf("DIAG-READ-AUDIO-ITEM: ptr=%p size=%lu wait_ticks=%lu max_fetch=%lu free_before=%lu wav_pending=%lu\n",
         item,
         (unsigned long)read_size,
         (unsigned long)wait_ticks,
         (unsigned long)max_fetch,
         (unsigned long)free_before,
         (unsigned long)s_wav_pending_bytes);
        if (item == NULL || read_size == 0) {
            ESP_LOGI(TAG, "audio_processor_read: audio dequeue empty free_before=%zu", free_before);
            printf("DIAG-READ-AUDIO-DEQ: empty free_before=%lu\n", (unsigned long)free_before);
        } else {
            ESP_LOGI(TAG, "audio_processor_read: audio dequeue len=%zu free_before=%zu", read_size, free_before);
            printf("DIAG-READ-AUDIO-DEQ: len=%lu free_before=%lu\n", (unsigned long)read_size, (unsigned long)free_before);
        }
        if (item == NULL || read_size == 0) {
            break;
        }

        size_t to_copy = read_size;
        size_t remaining = size - bytes_written;
        if (to_copy > remaining) {
            to_copy = remaining;
        }

        if (to_copy > 0) {
            memcpy(buffer + bytes_written, item, to_copy);
            bytes_written += to_copy;
            if (s_beep_remaining_bytes > 0) {
                if (to_copy >= s_beep_remaining_bytes) s_beep_remaining_bytes = 0;
                else s_beep_remaining_bytes -= to_copy;
            }
            wav_playback_consume(to_copy);
        }

        size_t leftover = (read_size > to_copy) ? (read_size - to_copy) : 0;
        if (leftover > 0) {
            size_t stored = residual_store((const uint8_t*)item + to_copy, leftover,
                                           s_audio_rb_residual, sizeof(s_audio_rb_residual),
                                           &s_audio_rb_residual_pos, &s_audio_rb_residual_len);
            if (stored < leftover) {
                ESP_LOGW(TAG, "audio_processor_read: residual truncated %zu -> %zu", leftover, stored);
                s_audio_stats.buffer_overruns++;
            }
        } else {
            s_audio_rb_residual_len = 0;
            s_audio_rb_residual_pos = 0;
        }

    size_t free_after = xRingbufferGetCurFreeSize(s_audio_buffer);
    ESP_LOGI(TAG, "audio_processor_read: audio return len=%zu free_after=%zu", read_size, free_after);
    printf("DIAG-READ-AUDIO-RET: len=%zu free_after=%zu\n", read_size, free_after);
    /* Consume the matching metadata tag for this audio item so the
     * tag ringbuffer stays aligned with the audio ringbuffer. We
     * drop the tag value (caller doesn't need it) and perform a
     * non-blocking take. */
    audio_source_tag_take(NULL, 0);
    vRingbufferReturnItem(s_audio_buffer, item);
    }

    if (bytes_written == 0) {
        *bytes_read = 0;
        s_audio_stats.buffer_underruns++;
        log_read_summary("empty", size, bytes_written);
        printf("DIAG-READ-EXIT: empty bytes=%zu ret=ESP_OK\n", bytes_written);
        wav_stream_try_refill();
        return ESP_OK;
    }

    if (bytes_written < size) {
        /* Zero-fill remaining tail so output buffer is deterministic */
        size_t tail = size - bytes_written;
        memset(buffer + bytes_written, 0, tail);
        bytes_written += tail;
    }

    // Apply volume if not at maximum
    if (s_volume_gain < 100) {
        apply_volume(buffer, bytes_written, s_volume_gain);
    }

    *bytes_read = bytes_written;

    log_read_summary("complete", size, bytes_written);

    // Update statistics
    s_audio_stats.current_buffer_level = xRingbufferGetCurFreeSize(s_audio_buffer);
    if (s_audio_stats.current_buffer_level > s_audio_stats.peak_buffer_level) {
        s_audio_stats.peak_buffer_level = s_audio_stats.current_buffer_level;
    }

    wav_stream_try_refill();
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
    AUDIO_PROC_LOG_ONCE();
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
    AUDIO_PROC_LOG_ONCE();
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
    AUDIO_PROC_LOG_ONCE();
    if (!s_is_initialized) return ESP_ERR_INVALID_STATE;
    if (s_audio_buffer == NULL) return ESP_ERR_INVALID_STATE;

    size_t rsz = 0;
    void* it = NULL;
    /* Non-blocking drain: repeatedly receive any available items and
     * return them so the ringbuffer frees its memory. Limit loop to a
     * reasonable number of iterations to avoid starving other tasks. */
    int drained = 0;
    const int max_drains = 256;
    size_t max_chunk = xRingbufferGetMaxItemSize(s_audio_buffer);
    if (max_chunk == 0) {
        /* Fallback: ensure we still retrieve data even if the API reports
         * zero (shouldn't happen for byte buffers, but be defensive). */
        max_chunk = audio_get_runtime_work_bytes();
        if (max_chunk == 0) {
            max_chunk = 1024; /* final fallback */
        }
    }
    while (drained < max_drains) {
        it = xRingbufferReceiveUpTo(s_audio_buffer, &rsz, 0, max_chunk);
        if (it == NULL || rsz == 0) break;
        vRingbufferReturnItem(s_audio_buffer, it);
        drained++;
    }
    /* Drain any pending metadata tags that correspond to the items we just
     * removed from the audio ringbuffer so consumers don't observe stale
     * tag state. */
    audio_source_tag_reset_buffer();
    wav_playback_abort();
    ESP_LOGI(TAG, "audio_processor_drain_ringbuffer: drained %d items", drained);
    return ESP_OK;
}

static void drain_beep_buffer(void)
{
    if (s_beep_buffer == NULL) {
        return;
    }

    size_t rsz = 0;
    void* item = NULL;
    int drained = 0;

    while ((item = xRingbufferReceiveUpTo(s_beep_buffer, &rsz, 0, SIZE_MAX)) != NULL && rsz > 0) {
        vRingbufferReturnItem(s_beep_buffer, item);
        drained++;
    }

    if (drained > 0) {
        ESP_LOGD(TAG, "drain_beep_buffer: released %d items", drained);
    }
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
    AUDIO_PROC_LOG_ONCE();
    if (!s_is_initialized) return ESP_ERR_INVALID_STATE;

    size_t target = audio_get_runtime_work_bytes();
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
    AUDIO_PROC_LOG_ONCE();
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
        const int max_attempts = 3;
        int attempt = 0;
        /* Track bytes enqueued across attempts so callers below can inspect
         * the final result even when the send was performed inside the
         * retry loop. Declared outside the loop to avoid scope issues. */
        size_t enqueued = 0;
        while (attempt < max_attempts) {
            size_t free_before_attempt = xRingbufferGetCurFreeSize(s_audio_buffer);
            ESP_LOGD(TAG, "audio_processor_beep: attempt %d/%d frames=%zu bytes=%zu free_before_attempt=%zu",
                     attempt + 1, max_attempts, frames, (size_t)frames * frame_bytes, free_before_attempt);

            /* Use chunked enqueue helper to avoid very large atomic sends */
            enqueued = wav_stream_try_enqueue_unlocked(s_proc_buffer, (size_t)frames * frame_bytes, AUDIO_SOURCE_TAG_BEEP);

            size_t free_after_attempt = xRingbufferGetCurFreeSize(s_audio_buffer);
            if (enqueued == (size_t)frames * frame_bytes) {
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
        if (enqueued != (size_t)frames * frame_bytes) {
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
                void* it = xRingbufferReceiveUpTo(s_audio_buffer, &rsz, 0, needed);
                if (it == NULL || rsz == 0) break;
                /* Return the item to free the space (we're intentionally dropping it) */
                audio_source_tag_drop_one();
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
                /* Try one immediate chunked send (no wait) after dropping */
                size_t enq2 = wav_stream_try_enqueue_unlocked(s_proc_buffer, (size_t)frames * frame_bytes, AUDIO_SOURCE_TAG_BEEP);
                if (enq2 == (size_t)frames * frame_bytes) {
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
        if (wav_playback_is_active()) {
            ESP_LOGW(TAG, "audio_processor_beep: suppressing fallback (%zu bytes) because WAV playback is active", bytes_remaining);
            if (s_beep_remaining_bytes >= bytes_remaining) {
                s_beep_remaining_bytes -= bytes_remaining;
            } else {
                s_beep_remaining_bytes = 0;
            }
            bytes_remaining = 0;
        }

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
    AUDIO_PROC_LOG_ONCE();
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

    s_trace_next_read_call = true;
    ESP_LOGI(TAG, "audio_processor_play_wav: armed one-shot trace for next audio_processor_read call");

    esp_err_t status = ESP_OK;
    FILE* f = fopen(path, "rb");
    bool resume_pipeline = false;

    if (!f) {
        ESP_LOGE(TAG, "audio_processor_play_wav: failed to open %s", path);
        return ESP_ERR_NOT_FOUND;
    }

    if (s_is_running) {
        status = audio_processor_stop();
        if (status != ESP_OK) {
            ESP_LOGE(TAG, "audio_processor_play_wav: stop failed (%d %s)", (int)status, esp_err_to_name(status));
            printf("DIAG-APLAY-FAIL: stop-failed %d\n", (int)status);
            goto cleanup;
        }
        resume_pipeline = true;
    }

    if (s_audio_buffer != NULL) {
        (void)audio_processor_drain_ringbuffer();
    }
    drain_beep_buffer();
    portENTER_CRITICAL(&s_beep_lock);
    s_beep_remaining_bytes = 0;
    s_beep_fallback_active = false;
    s_beep_fallback_frames_remaining = 0;
    s_beep_fallback_total_frames = 0;
    portEXIT_CRITICAL(&s_beep_lock);

    wav_playback_begin();

    /* Basic WAV header parsing (RIFF/WAVE, fmt chunk, data chunk) */
    uint32_t tmp32 = 0;
    char riff[4];
    if (fread(riff, 1, 4, f) != 4) {
        ESP_LOGW(TAG, "audio_processor_play_wav: missing RIFF header (fread failed)");
        printf("DIAG-APLAY-FAIL: missing-riff\n");
        status = ESP_ERR_INVALID_STATE;
        goto cleanup;
    }
    if (memcmp(riff, "RIFF", 4) != 0) {
        ESP_LOGW(TAG, "audio_processor_play_wav: RIFF header mismatch");
        printf("DIAG-APLAY-FAIL: riff-mismatch\n");
        status = ESP_ERR_INVALID_STATE;
        goto cleanup;
    }
    /* skip file size */
    fread(&tmp32, 4, 1, f);
    char wave[4];
    if (fread(wave, 1, 4, f) != 4) {
        ESP_LOGW(TAG, "audio_processor_play_wav: missing WAVE header (fread failed)");
        printf("DIAG-APLAY-FAIL: missing-wave\n");
        status = ESP_ERR_INVALID_STATE;
        goto cleanup;
    }
    if (memcmp(wave, "WAVE", 4) != 0) {
        ESP_LOGW(TAG, "audio_processor_play_wav: WAVE header mismatch");
        printf("DIAG-APLAY-FAIL: wave-mismatch\n");
        status = ESP_ERR_INVALID_STATE;
        goto cleanup;
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

        if (memcmp(chunk_id, "fmt ", 4) == 0) {
            if (chunk_size < 16) {
                ESP_LOGW(TAG, "audio_processor_play_wav: fmt chunk too small (chunk_size=%u)", (unsigned)chunk_size);
                printf("DIAG-APLAY-FAIL: fmt-chunk-too-small %u\n", (unsigned)chunk_size);
                status = ESP_ERR_INVALID_STATE;
                goto cleanup;
            }
            uint16_t fmt16_1 = 0;
            fread(&fmt16_1, 2, 1, f); audio_format = fmt16_1;
            fread(&num_channels, 2, 1, f);
            fread(&sample_rate, 4, 1, f);
            fread(&tmp32, 4, 1, f); /* skip byte rate */
            uint16_t tmp16 = 0; fread(&tmp16, 2, 1, f); /* skip block align */
            fread(&bits_per_sample, 2, 1, f);
            if (chunk_size > 16) fseek(f, (long)(chunk_size - 16), SEEK_CUR);
            have_fmt = true;
        } else if (memcmp(chunk_id, "data", 4) == 0) {
            data_bytes = chunk_size;
            break;
        } else {
            fseek(f, (long)chunk_size, SEEK_CUR);
        }
    }

    if (have_fmt && data_bytes > 0) {
        ESP_LOGI(TAG, "audio_processor_play_wav: parsed WAV fmt=%u ch=%u sr=%u bits=%u data=%u",
                 (unsigned)audio_format, (unsigned)num_channels, (unsigned)sample_rate, (unsigned)bits_per_sample, (unsigned)data_bytes);
        printf("DIAG-APLAY-HDR: fmt=%u ch=%u sr=%u bits=%u data=%u\n",
               (unsigned)audio_format, (unsigned)num_channels, (unsigned)sample_rate, (unsigned)bits_per_sample, (unsigned)data_bytes);

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
            if (cur >= 0) fseek(f, cur, SEEK_SET);
        }
    }

    if (!have_fmt || data_bytes == 0) {
        ESP_LOGW(TAG, "audio_processor_play_wav: missing fmt or data chunk (have_fmt=%d data_bytes=%u)", (int)have_fmt, (unsigned)data_bytes);
        printf("DIAG-APLAY-FAIL: missing-fmt-or-data %d %u\n", (int)have_fmt, (unsigned)data_bytes);
        status = ESP_ERR_INVALID_STATE;
        goto cleanup;
    }

    if (audio_format != 1) {
        ESP_LOGE(TAG, "audio_processor_play_wav: unsupported WAV format=%u", (unsigned)audio_format);
        status = ESP_ERR_NOT_SUPPORTED;
        goto cleanup;
    }

    audio_bit_depth_t src_bit = AUDIO_BIT_DEPTH_16;
    if (bits_per_sample == 16) src_bit = AUDIO_BIT_DEPTH_16;
    else if (bits_per_sample == 24) src_bit = AUDIO_BIT_DEPTH_24;
    else if (bits_per_sample == 32) src_bit = AUDIO_BIT_DEPTH_32;
    else {
        ESP_LOGE(TAG, "audio_processor_play_wav: unsupported bits_per_sample=%u", (unsigned)bits_per_sample);
        status = ESP_ERR_NOT_SUPPORTED;
        goto cleanup;
    }

    size_t frame_bytes_src = (bits_per_sample / 8) * (size_t)num_channels;
    if (frame_bytes_src == 0) {
        ESP_LOGW(TAG, "audio_processor_play_wav: computed zero frame_bytes_src (bits=%u channels=%u)", (unsigned)bits_per_sample, (unsigned)num_channels);
        printf("DIAG-APLAY-FAIL: zero-frame-bytes bits=%u ch=%u\n", (unsigned)bits_per_sample, (unsigned)num_channels);
        status = ESP_ERR_INVALID_STATE;
        goto cleanup;
    }

    size_t frame_bytes_dst = (size_t)audio_bytes_per_sample(s_audio_config.bit_depth);
    if (frame_bytes_dst == 0U) {
        frame_bytes_dst = 2U;
    }
    int dst_channels = (s_audio_config.channels == AUDIO_CHANNEL_MONO) ? 1 : 2;
    if (dst_channels <= 0) {
        dst_channels = 2;
    }
    frame_bytes_dst *= (size_t)dst_channels;
    if (frame_bytes_dst == 0U) {
        frame_bytes_dst = frame_bytes_src;
        if (frame_bytes_dst == 0U) {
            frame_bytes_dst = 2U;
        }
    }

    if (s_wav_mutex == NULL) {
        wav_stream_mutex_init();
    }

    if (s_wav_mutex == NULL) {
        ESP_LOGE(TAG, "audio_processor_play_wav: streaming mutex unavailable");
        status = ESP_ERR_NO_MEM;
        goto cleanup;
    }

    if (xSemaphoreTake(s_wav_mutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "audio_processor_play_wav: failed to take streaming mutex");
        status = ESP_ERR_TIMEOUT;
        goto cleanup;
    }

    bool resume_prev = wav_stream_clear_locked(true);
    if (resume_prev) {
        /* Previous playback left the pipeline stopped; resume now so callers
         * do not observe a stuck stream. We'll restart below once we release
         * the mutex if needed. */
        xSemaphoreGive(s_wav_mutex);
        if (s_is_initialized) {
            esp_err_t restart_prev = audio_processor_start();
            if (restart_prev != ESP_OK) {
                ESP_LOGW(TAG, "audio_processor_play_wav: failed to resume previous pipeline (%d %s)", (int)restart_prev, esp_err_to_name(restart_prev));
            }
        }
        if (xSemaphoreTake(s_wav_mutex, portMAX_DELAY) != pdTRUE) {
            status = ESP_ERR_TIMEOUT;
            goto cleanup;
        }
    }

    wav_stream_reset_residual_locked();
    s_wav_stream.active = true;
    s_wav_stream.resume_pipeline = resume_pipeline;
    s_wav_stream.file = f;
    s_wav_stream.src_bit_depth = src_bit;
    s_wav_stream.src_sample_rate = (audio_sample_rate_t)sample_rate;
    s_wav_stream.frame_bytes_src = frame_bytes_src;
    s_wav_stream.frame_bytes_dst = frame_bytes_dst;
    s_wav_stream.remaining_bytes = data_bytes;

    esp_err_t prime_ret = wav_stream_fill_locked();
    if (prime_ret != ESP_OK) {
        ESP_LOGE(TAG, "audio_processor_play_wav: failed priming stream (%d %s)", (int)prime_ret, esp_err_to_name(prime_ret));
        s_wav_stream.resume_pipeline = true;
        bool resume_now = wav_stream_clear_locked(true);
        xSemaphoreGive(s_wav_mutex);
        status = prime_ret;
        if (resume_now && s_is_initialized) {
            esp_err_t restart_now = audio_processor_start();
            if (restart_now != ESP_OK) {
                ESP_LOGW(TAG, "audio_processor_play_wav: pipeline resume failed after prime error (%d %s)", (int)restart_now, esp_err_to_name(restart_now));
            }
        }
        goto cleanup;
    }

    bool prime_empty = (s_wav_stream.remaining_bytes == 0U && s_wav_pending_bytes == 0U && s_wav_send_residual_len == 0U);

    xSemaphoreGive(s_wav_mutex);

    /* File ownership transferred to streaming context */
    f = NULL;
    resume_pipeline = false; /* streaming context will resume when drained */

    if (prime_empty) {
        bool resume_now = false;
        if (xSemaphoreTake(s_wav_mutex, portMAX_DELAY) == pdTRUE) {
            resume_now = wav_stream_clear_locked(true);
            xSemaphoreGive(s_wav_mutex);
        }
        wav_playback_complete_if_idle();
        if (resume_now && s_is_initialized) {
            esp_err_t restart_ret = audio_processor_start();
            if (restart_ret != ESP_OK) {
                ESP_LOGW(TAG, "audio_processor_play_wav: resume failed after empty WAV (%d %s)", (int)restart_ret, esp_err_to_name(restart_ret));
            }
        }
    }

    ESP_LOGI(TAG, "audio_processor_play_wav: playback streaming for %s", path);
    status = ESP_OK;

cleanup:
    if (f != NULL) {
        fclose(f);
        f = NULL;
    }

    if (status != ESP_OK) {
        wav_stream_abort(false);
        wav_playback_abort();
    }

    if (resume_pipeline) {
        esp_err_t restart_ret = audio_processor_start();
        if (restart_ret != ESP_OK) {
            ESP_LOGE(TAG, "audio_processor_play_wav: failed to restart pipeline (%d %s)", (int)restart_ret, esp_err_to_name(restart_ret));
            if (status == ESP_OK) {
                status = restart_ret;
            }
        }
    }

    return status;
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
                if (!wav_playback_is_active()) {
                    ESP_LOGW(TAG, "I2S read failing repeatedly (%d); enabling runtime synth mode", s_i2s_consecutive_failures);
                    s_force_synth = true;
                } else {
                    ESP_LOGW(TAG, "I2S read failing (%d) but WAV playback active; keeping synth disabled", s_i2s_consecutive_failures);
                }
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
                const bool synth_fill = s_force_synth;
                if (synth_fill && s_audio_buffer != NULL) {
                    size_t rb_free = xRingbufferGetCurFreeSize(s_audio_buffer);
                    if (rb_free < (size_t)SYNTH_MIN_HEADROOM_BYTES) {
                        (void)xQueueSend(s_i2s_free_queue, &pooled_ptr, 0);
                        /*s
                        printf("DIAG-READER-SYNTH-HOLD: rb_free=%zu threshold=%zu\n",
                               rb_free,
                               (size_t)SYNTH_MIN_HEADROOM_BYTES);
                        */
                        TickType_t hold_ticks = pdMS_TO_TICKS(SYNTH_THROTTLE_DELAY_MS);
                        if (hold_ticks == 0) {
                            hold_ticks = 1;
                        }
                        vTaskDelay(hold_ticks);
                        continue;
                    }
                }

                i2s_block_t blk = {
                    .ptr = pooled_ptr,
                    .len = synth_fill ? 0 : bytes_read,
                    .capacity = bytes_read,
                    .synth_fill = synth_fill,
                    .pooled_ptr = true,
                };
                /*
                printf("DIAG-READER-ALLOC: ptr=%p len=%zu synth=%d free_q=%lu\n",
                       pooled_ptr,
                       blk.len,
                       blk.synth_fill ? 1 : 0,
                       (unsigned long)uxQueueMessagesWaiting(s_i2s_free_queue));
                */
                if (!blk.synth_fill && blk.len > 0) {
                    memcpy(blk.ptr, s_i2s_buffer, blk.len);
                }
                if (xQueueSend(s_i2s_queue, &blk, 0) != pdTRUE) {
                    (void)xQueueSend(s_i2s_free_queue, &pooled_ptr, 0);
                    s_audio_stats.buffer_overruns++;
                    backpressure = true;
                    ESP_LOGW(TAG, "i2s_reader_task: backpressure while enqueueing i2s block len=%zu", blk.len);
                    /*
                    printf("DIAG-READER-SEND-FAIL: ptr=%p len=%zu synth=%d q_wait=%lu\n",
                           blk.ptr,
                           blk.len,
                           blk.synth_fill ? 1 : 0,
                           (unsigned long)uxQueueMessagesWaiting(s_i2s_queue));
                    */    
                    log_heap_stats("i2s-backpressure");
                } else {
                    /*
                    printf("DIAG-READER-SEND: ptr=%p len=%zu synth=%d q_wait=%lu\n",
                           blk.ptr,
                           blk.len,
                           blk.synth_fill ? 1 : 0,
                           (unsigned long)uxQueueMessagesWaiting(s_i2s_queue));
                    */
                }
            } else {
                s_audio_stats.buffer_overruns++;
                backpressure = true;
                /*
                printf("DIAG-READER-NO-BUF: synth=%d free_q=%lu\n",
                       s_force_synth ? 1 : 0,
                       (unsigned long)uxQueueMessagesWaiting(s_i2s_free_queue));
                */
            }
        } else {
            s_audio_stats.buffer_overruns++;
            backpressure = true;
            ESP_LOGW(TAG, "i2s_reader_task: backpressure (no free queue) for len=%zu", bytes_read);
            /*
            printf("DIAG-READER-BP: len=%zu synth=%d q_wait=%lu free_q=%lu\n",
                   bytes_read,
                   s_force_synth ? 1 : 0,
                   (unsigned long)(s_i2s_queue != NULL ? uxQueueMessagesWaiting(s_i2s_queue) : 0UL),
                   (unsigned long)(s_i2s_free_queue != NULL ? uxQueueMessagesWaiting(s_i2s_free_queue) : 0UL));
            */
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

static inline void log_worker_block_state(const char *phase, const i2s_block_t *blk)
{
    if (esp_log_level_get(TAG) < ESP_LOG_DEBUG) {
        return;
    }
    const char *tag = (phase != NULL) ? phase : "unknown";
    ESP_LOGD(TAG, "audio_worker_task[%s]: ptr=%p len=%zu cap=%zu synth=%d pooled=%d",
             tag,
             (blk != NULL) ? blk->ptr : NULL,
             (blk != NULL) ? blk->len : 0U,
             (blk != NULL) ? blk->capacity : 0U,
             (blk != NULL) ? (int)blk->synth_fill : 0,
             (blk != NULL) ? (int)blk->pooled_ptr : 0);
}

static void audio_worker_return_block(const char *phase, i2s_block_t *blk)
{
    const char *tag = (phase != NULL) ? phase : "unknown";
    if (blk == NULL || blk->ptr == NULL) {
        ESP_LOGD(TAG, "audio_worker_task[%s]: return skipped (ptr=NULL)", tag);
        return;
    }

    void *raw_ptr = blk->ptr;
    const bool pooled = blk->pooled_ptr && s_i2s_free_queue != NULL;

    if (pooled) {
        ESP_LOGV(TAG, "audio_worker_task[%s]: returning pooled ptr=%p", tag, raw_ptr);
        if (xQueueSend(s_i2s_free_queue, &raw_ptr, pdMS_TO_TICKS(10)) != pdTRUE) {
            ESP_LOGW(TAG, "audio_worker_task[%s]: free queue full, freeing pooled ptr=%p", tag, raw_ptr);
            heap_caps_free(raw_ptr);
        } else {
            printf("DIAG-WORKER-RETPOOL: ptr=%p phase=%s free_q=%lu\n",
                   raw_ptr,
                   tag,
                   (unsigned long)uxQueueMessagesWaiting(s_i2s_free_queue));
        }
    } else {
        ESP_LOGV(TAG, "audio_worker_task[%s]: freeing non-pooled ptr=%p", tag, raw_ptr);
        heap_caps_free(raw_ptr);
        printf("DIAG-WORKER-FREE: ptr=%p phase=%s\n", raw_ptr, tag);
    }

    blk->ptr = NULL;
    blk->len = 0;
    blk->capacity = 0;
    blk->synth_fill = false;
    blk->pooled_ptr = false;
}

static void audio_worker_discard_block(const char *phase, i2s_block_t *blk)
{
    const char *tag = (phase != NULL) ? phase : "unknown";
    if (blk == NULL || blk->ptr == NULL) {
        ESP_LOGD(TAG, "audio_worker_task[%s]: discard skipped (ptr=NULL)", tag);
        return;
    }

    if (blk->pooled_ptr) {
        ESP_LOGV(TAG, "audio_worker_task[%s]: discard pooled ptr=%p -> recycle", tag, blk->ptr);
        audio_worker_return_block(phase, blk);
        return;
    }

    ESP_LOGV(TAG, "audio_worker_task[%s]: discarding non-pooled ptr=%p", tag, blk->ptr);
    heap_caps_free(blk->ptr);
    blk->ptr = NULL;
    blk->len = 0;
    blk->capacity = 0;
    blk->synth_fill = false;
    blk->pooled_ptr = false;
}

static void log_read_summary(const char *phase, size_t requested, size_t produced)
{
    if (esp_log_level_get(TAG) < ESP_LOG_INFO) {
        return;
    }

    const char *tag = (phase != NULL) ? phase : "done";
    size_t audio_residual = 0;
    if (s_audio_rb_residual_len > s_audio_rb_residual_pos) {
        audio_residual = s_audio_rb_residual_len - s_audio_rb_residual_pos;
    }
    size_t beep_residual = 0;
    if (s_beep_rb_residual_len > s_beep_rb_residual_pos) {
        beep_residual = s_beep_rb_residual_len - s_beep_rb_residual_pos;
    }
    size_t rb_free = 0;
    if (s_audio_buffer != NULL) {
        rb_free = xRingbufferGetCurFreeSize(s_audio_buffer);
    }

    ESP_LOGI(TAG,
             "audio_processor_read[%s]: req=%zu produced=%zu mute=%d beep_remaining=%zu audio_residual=%zu beep_residual=%zu rb_free=%zu underruns=%u overruns=%u",
             tag,
             requested,
             produced,
             (int)s_audio_config.mute,
             s_beep_remaining_bytes,
             audio_residual,
             beep_residual,
             rb_free,
             (unsigned)s_audio_stats.buffer_underruns,
             (unsigned)s_audio_stats.buffer_overruns);
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

        s_worker_diag.dequeued_blocks++;

        printf("DIAG-WORKER-DEQ: ptr=%p len=%zu synth=%d q_wait=%lu free_q=%lu\n",
               blk.ptr,
               blk.len,
               blk.synth_fill ? 1 : 0,
               (unsigned long)uxQueueMessagesWaiting(s_i2s_queue),
               (unsigned long)(s_i2s_free_queue != NULL ? uxQueueMessagesWaiting(s_i2s_free_queue) : 0UL));

        log_worker_block_state("dequeue", &blk);

        if (blk.ptr == NULL) {
            log_worker_block_state("dequeue-null", &blk);
            continue;
        }

        if (blk.synth_fill) {
            if (wav_playback_is_active()) {
                s_worker_diag.synth_blocks++;
                audio_worker_return_block("wav-priority", &blk);
                TickType_t pause_ticks = pdMS_TO_TICKS(1);
                if (pause_ticks == 0) {
                    pause_ticks = 1;
                }
                vTaskDelay(pause_ticks);
                blk.len = 0;
                blk.capacity = 0;
                blk.synth_fill = false;
                continue;
            }
            s_worker_diag.synth_blocks++;
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
                audio_worker_return_block("synth-zero", &blk);
                blk.len = 0;
                blk.capacity = 0;
                blk.synth_fill = false;
                continue;
            }
        }

        if (blk.len == 0) {
            audio_worker_return_block("zero-len", &blk);
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
            audio_worker_discard_block("convert-fail", &blk);
            continue;
        }

        /* Resample */
        size_t res_size = 0;
        esp_err_t rret = resample_audio(s_proc_buffer, s_proc_buffer2, conv_size,
                                        s_audio_config.sample_rate, s_audio_config.sample_rate,
                                        &res_size);
        if (rret != ESP_OK) {
            s_audio_stats.conversion_errors++;
            audio_worker_discard_block("resample-fail", &blk);
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
            printf("DIAG-WORKER-ENQ: attempt len=%zu free_before=%zu synth=%d\n", res_size, free_size, blk.synth_fill ? 1 : 0);
            if (free_size < res_size) {
                s_audio_stats.buffer_overruns++;
                if (free_size < (res_size / 2)) {
                    /* Skip adding if buffer very full */
                    printf("DIAG-WORKER-ENQ: drop len=%zu free_before=%zu reason=overrun\n", res_size, free_size);
                    audio_worker_discard_block("overrun-drop", &blk);
                    TickType_t pause_ticks = pdMS_TO_TICKS(1);
                    if (pause_ticks == 0) {
                        pause_ticks = 1;
                    }
                    vTaskDelay(pause_ticks);
                    continue;
                }
            }

            /* Use the chunked enqueue helper to avoid large atomic sends
             * that can saturate the ringbuffer. The helper returns the
             * number of bytes actually enqueued. */
            audio_source_tag_t tag = blk.synth_fill ? AUDIO_SOURCE_TAG_SYNTH : AUDIO_SOURCE_TAG_CAPTURE;
            size_t sent_bytes = wav_stream_try_enqueue_unlocked(s_proc_buffer2, res_size, tag);
            if (sent_bytes < res_size) {
                s_audio_stats.buffer_overruns++;
                ESP_LOGW(TAG, "audio_worker_task: enqueue incomplete for res_size=%zu sent=%zu", res_size, sent_bytes);
                printf("DIAG-WORKER-ENQ: fail len=%zu sent=%zu free_before=%zu\n", res_size, sent_bytes, free_size);
                log_heap_stats("worker-send-failed");
                s_worker_diag.ringbuffer_failures++;
                worker_diag_report(WORKER_DIAG_SOURCE_WORKER, res_size, (BaseType_t)pdFALSE);
                TickType_t pause_ticks = pdMS_TO_TICKS(1);
                if (pause_ticks == 0) {
                    pause_ticks = 1;
                }
                vTaskDelay(pause_ticks);
            } else {
                size_t free_after = xRingbufferGetCurFreeSize(s_audio_buffer);
                const char *src = blk.synth_fill ? "synth" : "capture";
                ESP_LOGI(TAG,
                         "audio_worker_task: enqueued %zu bytes (%s) free_before=%zu free_after=%zu dequeued=%u rb_fail=%u",
                         res_size,
                         src,
                         free_size,
                         free_after,
                         (unsigned)s_worker_diag.dequeued_blocks,
                         (unsigned)s_worker_diag.ringbuffer_failures);
                printf("DIAG-WORKER-RET: len=%zu free_before=%zu free_after=%zu\n", res_size, free_size, free_after);
                worker_diag_report(WORKER_DIAG_SOURCE_WORKER, res_size, (BaseType_t)pdTRUE);
            }
        }

        /* Return the raw block to the free pool if present, otherwise free it */
        if (blk.ptr != NULL) {
            audio_worker_return_block("return-final", &blk);
            blk.len = 0;
            blk.capacity = 0;
            blk.synth_fill = false;
        }
    }
}

static void worker_diag_report(worker_diag_source_t source, size_t enqueued_bytes, BaseType_t send_result)
{
    if (esp_log_level_get(TAG) < ESP_LOG_INFO) {
        return;
    }

    if (enqueued_bytes > 0) {
        if (source == WORKER_DIAG_SOURCE_WORKER) {
            s_worker_diag.worker_bytes_sent += enqueued_bytes;
        } else if (source == WORKER_DIAG_SOURCE_WAV) {
            s_worker_diag.wav_bytes_sent += enqueued_bytes;
            s_worker_diag.wav_chunks++;
        }
        s_worker_diag.bytes_sent += enqueued_bytes;
    }

    s_worker_diag.last_enqueued_bytes = enqueued_bytes;
    s_worker_diag.last_send_result = send_result;
    s_worker_diag.last_source = source;

    const TickType_t now = xTaskGetTickCount();
    if (s_worker_diag.last_report_tick == 0 ||
        (now - s_worker_diag.last_report_tick) >= WORKER_DIAG_INTERVAL_TICKS) {
        size_t rb_free = 0U;
        size_t rb_max_item = 0U;
        if (s_audio_buffer != NULL) {
            rb_free = xRingbufferGetCurFreeSize(s_audio_buffer);
            rb_max_item = xRingbufferGetMaxItemSize(s_audio_buffer);
        }

        const char *src_str = (s_worker_diag.last_source == WORKER_DIAG_SOURCE_WAV) ? "wav" : "worker";
        ESP_LOGI(TAG,
                 "worker diag: src=%s dequeued=%" PRIu32 " synth=%" PRIu32 " worker_bytes=%zu wav_chunks=%" PRIu32 " wav_bytes=%zu total_bytes=%zu rb_free=%zu rb_max_item=%zu send_failures=%" PRIu32 " last_enqueued=%zu last_send=%ld",
                 src_str,
                 s_worker_diag.dequeued_blocks,
                 s_worker_diag.synth_blocks,
                 s_worker_diag.worker_bytes_sent,
                 s_worker_diag.wav_chunks,
                 s_worker_diag.wav_bytes_sent,
                 s_worker_diag.bytes_sent,
                 rb_free,
                 rb_max_item,
                 s_worker_diag.ringbuffer_failures,
                 s_worker_diag.last_enqueued_bytes,
                 (long)s_worker_diag.last_send_result);

        s_worker_diag.dequeued_blocks = 0U;
        s_worker_diag.synth_blocks = 0U;
        s_worker_diag.bytes_sent = 0U;
        s_worker_diag.worker_bytes_sent = 0U;
        s_worker_diag.wav_bytes_sent = 0U;
        s_worker_diag.wav_chunks = 0U;
        s_worker_diag.ringbuffer_failures = 0U;
        s_worker_diag.last_report_tick = now;
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
    size_t work_bytes = audio_get_runtime_work_bytes();
    if (work_bytes == 0U) {
        work_bytes = (size_t)AUDIO_WORK_BUFFER_BYTES;
    }

    if (src_bit_depth == dst_bit_depth) {
        // Same format, just copy
        size_t copy_size = src_size;
        if (copy_size > work_bytes) {
            ESP_LOGW(TAG, "convert_audio_format: copy truncated from %zu to %zu bytes", copy_size, work_bytes);
            copy_size = work_bytes;
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
    if (calculated > work_bytes) {
        ESP_LOGW(TAG, "convert_audio_format: dst size %zu exceeds buffer %zu, truncating", calculated, work_bytes);
        calculated = work_bytes;
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

    size_t work_bytes = audio_get_runtime_work_bytes();
    if (work_bytes == 0U) {
        work_bytes = (size_t)AUDIO_WORK_BUFFER_BYTES;
    }

    // Sanity clamp: never write more than our work buffers
    if (src_size > work_bytes) {
        ESP_LOGW(TAG, "resample_audio: src_size (%zu) exceeds work buffer (%zu), truncating", src_size, work_bytes);
        src_size = work_bytes;
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

    if (src_size > work_bytes) {
        ESP_LOGW(TAG, "Resample input truncated from %zu to %zu bytes", src_size, work_bytes);
        src_size = work_bytes;
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
        if (src_size > work_bytes) {
            ESP_LOGW(TAG, "resample_audio: pass-through truncated %zu -> %zu", src_size, work_bytes);
            src_size = work_bytes;
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
        if (src_size > work_bytes) {
            ESP_LOGW(TAG, "resample_audio: rate-equal copy truncated %zu -> %zu", src_size, work_bytes);
            src_size = work_bytes;
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

    size_t max_dst_frames = work_bytes / frame_bytes;
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

    if (dst_bytes > work_bytes) {
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
    AUDIO_PROC_LOG_ONCE();
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
    AUDIO_PROC_LOG_ONCE();
    return s_beep_remaining_bytes > 0;
}

/**
 * @brief Arm a one-shot diagnostic dump for the next beep invocation.
 * Call this before issuing `BEEP` to capture fallback and worker snapshots.
 */
void audio_processor_enable_next_beep_diag(void)
{
    AUDIO_PROC_LOG_ONCE();
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
    AUDIO_PROC_LOG_ONCE();
    return s_is_running;
}

size_t audio_processor_get_work_buffer_bytes(void)
{
    AUDIO_PROC_LOG_ONCE();
    return s_runtime_work_bytes;
}

#ifdef CONFIG_BT_MOCK_TESTING
void audio_processor_test_wav_reset_state(void)
{
    AUDIO_PROC_LOG_ONCE();
    portENTER_CRITICAL(&s_wav_lock);
    s_wav_playback_active = false;
    s_wav_pending_bytes = 0;
    s_wav_prev_valid = false;
    s_wav_prev_force_synth = false;
    portEXIT_CRITICAL(&s_wav_lock);
}

void audio_processor_test_wav_begin(void)
{
    AUDIO_PROC_LOG_ONCE();
    wav_playback_begin();
}

void audio_processor_test_wav_add_pending(size_t bytes)
{
    AUDIO_PROC_LOG_ONCE();
    wav_playback_add_pending(bytes);
}

bool audio_processor_test_wav_consume(size_t bytes)
{
    AUDIO_PROC_LOG_ONCE();
    return wav_playback_consume(bytes);
}

void audio_processor_test_wav_abort(void)
{
    AUDIO_PROC_LOG_ONCE();
    wav_playback_abort();
}

void audio_processor_test_wav_complete_if_idle(void)
{
    AUDIO_PROC_LOG_ONCE();
    wav_playback_complete_if_idle();
}

bool audio_processor_test_wav_is_active(void)
{
    AUDIO_PROC_LOG_ONCE();
    return wav_playback_is_active();
}

size_t audio_processor_test_wav_pending_bytes(void)
{
    AUDIO_PROC_LOG_ONCE();
    size_t pending = 0;
    portENTER_CRITICAL(&s_wav_lock);
    pending = s_wav_pending_bytes;
    portEXIT_CRITICAL(&s_wav_lock);
    return pending;
}

/**
 * @brief Inject audio data directly into the ring buffer (for testing only)
 */
esp_err_t audio_processor_test_inject_audio_data(const uint8_t* data, size_t size)
{
    AUDIO_PROC_LOG_ONCE();
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

#ifdef CONFIG_BT_MOCK_TESTING
/* Return number of bytes currently used in the metadata/tag ringbuffer.
 * Each tag is stored as a single byte; callers can use this to validate
 * that producer-side tag pushes and consumer-side tag takes remain
 * balanced during unit tests. Returns 0 if the metadata buffer is not
 * initialized. */
size_t audio_processor_test_get_tag_used(void)
{
    if (s_audio_source_buffer == NULL) return 0;
    /* Capacity of the tag buffer is AUDIO_SOURCE_BUFFER_SIZE. xRingbuffer
     * provides the current free size; compute used = capacity - free. */
    size_t free_sz = xRingbufferGetCurFreeSize(s_audio_source_buffer);
    size_t capacity = (size_t)AUDIO_SOURCE_BUFFER_SIZE;
    if (free_sz > capacity) return 0; /* defensive */
    return capacity - free_sz;
}
#endif
