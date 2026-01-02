#ifdef _POSIX_READER_WRITER_LOCKS
// Clear toolchain define so newlib can set the standard value without a redefinition warning.
#undef _POSIX_READER_WRITER_LOCKS
#endif

#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdbool.h>
#include <limits.h>
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "driver/i2s_std.h"  /* Use the current I2S driver */
#include "driver/gpio.h"

#include "audio_processor.h"
#include "mem_util.h"
#include "synth_manager.h"
#include "audio_queue.h"
#include "beep_manager.h"
#include "i2s_manager.h"
#include "play_manager.h"
#include "audio_util.h"
#include "nvs_storage.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#if (defined(CONFIG_SPIRAM) && CONFIG_SPIRAM) || (defined(CONFIG_SPIRAM_SUPPORT) && CONFIG_SPIRAM_SUPPORT)
#include "esp_psram.h"
#endif

/* Weak A2DP connection probe so keepalive can be suppressed when BT is down. */
bool __attribute__((weak)) bt_manager_is_a2dp_connected(void)
{
    return true;
}

static bool audio_processor_is_a2dp_connected(void)
{
    return bt_manager_is_a2dp_connected();
}

static const char *TAG = "AUDIO_PROC";
static volatile bool s_audio_diag_enabled = false; /* gate noisy diagnostics */
#define AUDIO_DIAG_ENABLED() (s_audio_diag_enabled)
#define AUDIO_DIAG_PRINTF(...)    \
    do {                        \
        if (AUDIO_DIAG_ENABLED()) { printf(__VA_ARGS__); } \
    } while (0)
#define AUDIO_PROC_LOG_ONCE()                                             \
    do {                                                                  \
        static bool _logged = false;                                      \
        if (!_logged) {                                                   \
            ESP_LOGI(TAG, "audio_processor (main) entered %s", __func__);\
            _logged = true;                                               \
        }                                                                 \
    } while (0)

#define AUDIO_PROCESSING_STACK_SIZE  4096
#define AUDIO_BLOCK_SIZE              128
#ifdef CONFIG_BT_MOCK_TESTING
#define AUDIO_RESAMPLE_MAX_RATIO      6    /* cover worst-case 8 kHz -> 48 kHz upsampling in tests */
#else
#define AUDIO_RESAMPLE_MAX_RATIO      8    /* reduced from 12 to save RAM on DRAM-only builds */
#endif
#define AUDIO_WORK_BUFFER_BYTES ((size_t)AUDIO_BLOCK_SIZE * 8U * (size_t)AUDIO_RESAMPLE_MAX_RATIO)
#define BEEP_FADE_MS 50
#define I2S_RAW_POOL_DEFAULT_COUNT 8U
#define I2S_RAW_POOL_DRAM_COUNT    1U
#define I2S_DEFAULT_DMA_DESC_NUM 6U
#define I2S_DEFAULT_DMA_FRAME_NUM 32U
#define I2S_MAX_READ_BYTES ((size_t)4U * 1024U)
#define SYNTH_MIN_HEADROOM_BYTES  (AUDIO_WORK_BUFFER_BYTES)
#define SYNTH_THROTTLE_DELAY_MS   2
#define I2S_PROBE_MAX_ENTRIES 32U
#define I2S_FAILURE_THRESHOLD 20
#define I2S_FAILURE_LOG_THROTTLE 200
#define DIAG_DUMP_BYTES 64U

typedef struct {
    int64_t t_before_us;
    int64_t t_after_us;
    uint32_t dur_us;
    size_t requested;
    size_t got;
    int err;
} i2s_probe_entry_t;

static size_t audio_get_runtime_work_bytes(void);
static void drain_beep_buffer(void);
static void wav_refill_from_manager(void);
static int audio_bytes_per_sample(audio_bit_depth_t bit_depth);
static void audio_processor_flush_priority_queues(const char* reason);
static bool audio_processor_is_i2s_capture_active(void);
static void apply_volume(void* buffer, size_t size, uint8_t volume);
#if defined(CONFIG_BT_MOCK_TESTING) || defined(UNIT_TEST)
static void wav_playback_add_pending(size_t bytes);
#endif
static bool wav_playback_consume(size_t bytes);
static void wav_playback_complete_if_idle(void);
static bool s_is_initialized = false;
static bool s_is_running = false;
static bool s_force_synth = false;
static bool s_keepalive_armed = false;
static uint8_t s_volume_gain = 100;
static audio_config_t s_audio_config = {0};
static audio_stats_t s_audio_stats = {0};
static uint32_t s_tag_miss_count = 0;
static int64_t s_tag_recover_mute_until = 0;
static size_t s_runtime_work_bytes = 0;
static uint8_t s_audio_rb_residual[AUDIO_WORK_BUFFER_BYTES] = {0};
static size_t s_audio_rb_residual_len = 0;
static size_t s_audio_rb_residual_pos = 0;
static uint8_t *s_work_block = NULL;
static uint8_t *s_capture_buffer = NULL;
static uint8_t *s_proc_buffer = NULL;
static uint8_t *s_proc_buffer2 = NULL;
static TickType_t s_diag_next_log_tick = 0;
static size_t s_diag_last_conv_size = SIZE_MAX;
static size_t s_diag_last_frame_bytes = SIZE_MAX;
static int s_diag_last_src_rate = -1;
static int s_diag_last_dst_rate = -1;
static bool s_beep_prefill_active = false;
static size_t s_beep_prefill_accum_bytes = 0;
static size_t s_beep_prefill_goal_bytes = 0;
static bool s_beep_restore_synth = false;
static bool s_trace_next_read_call = false;
static bool s_last_source_was_synth = false;
static unsigned s_i2s_read_ops = 0;
static unsigned s_i2s_total_read_bytes = 0;
static unsigned s_i2s_timeout_count = 0;
static unsigned s_probe_captured = 0;
static unsigned s_probe_target = 0;
#ifdef CONFIG_BT_MOCK_TESTING
static int s_i2s_consecutive_failures = 0;
static int s_last_i2s_failure_log = 0;
#endif
static i2s_probe_entry_t s_probe_buf[I2S_PROBE_MAX_ENTRIES] = {0};

#ifdef UNIT_TEST
static uint32_t s_last_beep_duration_ms = 0;
static double s_last_beep_freq_hz = 0.0;
#endif

static size_t audio_get_runtime_work_bytes(void)
{
    size_t bytes = s_runtime_work_bytes;
    if (bytes == 0U) {
        bytes = (size_t)AUDIO_WORK_BUFFER_BYTES;
    }
    return bytes;
}

static portMUX_TYPE s_wav_lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool s_wav_playback_active = false;
static volatile size_t s_wav_pending_bytes = 0;
static bool s_wav_prev_valid = false;
static bool s_wav_prev_force_synth = false;
static bool s_wav_resume_pipeline = false;
#if defined(CONFIG_BT_MOCK_TESTING) || defined(UNIT_TEST)
static size_t s_test_queue_block_override = 0;
#endif

static portMUX_TYPE s_beep_lock = portMUX_INITIALIZER_UNLOCKED;
static size_t s_beep_remaining_bytes = 0;
static bool s_dump_next_beep_diag = false;

static void audio_processor_beep_done_cb(void *ctx)
{
    (void)ctx;
    portENTER_CRITICAL(&s_beep_lock);
    s_beep_remaining_bytes = 0;
    portEXIT_CRITICAL(&s_beep_lock);
}

static size_t audio_queue_free_bytes(void)
{
    size_t used = audio_descriptor_used();
    if (used >= AUDIO_CHUNK_POOL_BLOCKS) {
        return 0;
    }
    size_t free_blocks = (size_t)AUDIO_CHUNK_POOL_BLOCKS - used;
    return free_blocks * (size_t)AUDIO_CHUNK_BLOCK_BYTES;
}

static void audio_source_tag_reset_buffer(void)
{
    /* Metadata buffer removed in the audio_queue path; nothing to reset. */
}

static void audio_processor_flush_priority_queues(const char* reason)
{
    /* Drop any queued priority audio (beep/keepalive) so new sources start cleanly. */
    (void)reason;
    audio_chunk_t chunk = {0};
    while (audio_chunk_dequeue(&chunk, 0)) {
        audio_chunk_release_block(chunk.data);
    }
    s_audio_rb_residual_len = 0;
    s_audio_rb_residual_pos = 0;
}

static esp_err_t audio_processor_acquire_chunk_internal(audio_chunk_t *out_chunk, TickType_t wait_ticks)
{
    if (out_chunk == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!audio_chunk_dequeue(out_chunk, wait_ticks)) {
        return ESP_ERR_TIMEOUT;
    }

    size_t to_copy = out_chunk->len;
    bool is_beep = (out_chunk->tag == AUDIO_SOURCE_TAG_BEEP);
    if (is_beep) {
        if (s_beep_remaining_bytes > to_copy) {
            s_beep_remaining_bytes -= to_copy;
        } else {
            s_beep_remaining_bytes = 0;
        }
    } else {
        if (s_audio_config.mute) {
            safe_memset(out_chunk->data, out_chunk->len, 0, to_copy);
        } else if (s_volume_gain < 100 && to_copy > 0) {
            apply_volume(out_chunk->data, to_copy, s_volume_gain);
        }
        if (out_chunk->tag == AUDIO_SOURCE_TAG_WAV) {
            bool drained = wav_playback_consume(to_copy);
            if (drained) {
                wav_playback_complete_if_idle();
            }
        }
    }

    int bytes_per_sample = audio_bytes_per_sample(s_audio_config.bit_depth);
    if (bytes_per_sample <= 0) {
        bytes_per_sample = 2;
    }
    size_t frame_bytes = (size_t)bytes_per_sample * (size_t)((s_audio_config.channels == AUDIO_CHANNEL_MONO) ? 1U : 2U);
    if (frame_bytes > 0) {
        s_audio_stats.samples_processed += (uint32_t)(to_copy / frame_bytes);
    }

    return ESP_OK;
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

    safe_memcpy(dest, dest_len, buffer + *pos, to_copy);
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
    *stored_len = safe_memcpy(buffer, capacity, src, copy_len);
    *pos = 0;
    return copy_len;
}

static bool wav_playback_is_active(void)
{
    return s_wav_playback_active;
}

bool audio_processor_is_wav_active(void)
{
    AUDIO_PROC_LOG_ONCE();
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
    s_wav_resume_pipeline = false;
    portEXIT_CRITICAL(&s_wav_lock);
    ESP_LOGI(TAG, "audio_processor: WAV playback begin (prev synth=%s)", prev ? "ENABLED" : "DISABLED");
}

#if defined(CONFIG_BT_MOCK_TESTING) || defined(UNIT_TEST)
static void wav_playback_add_pending(size_t bytes)
{
    if (bytes == 0) {
        return;
    }

    s_wav_resume_pipeline = false;

    size_t pending = 0;

    if (play_manager_is_active() || play_manager_pending_bytes() > 0) {
        (void)play_manager_consume(bytes);
        pending = play_manager_pending_bytes();
    } else {
        portENTER_CRITICAL(&s_wav_lock);
        if (s_wav_playback_active) {
            if (bytes >= s_wav_pending_bytes) {
                s_wav_pending_bytes = 0;
            } else {
                s_wav_pending_bytes -= bytes;
            }
            pending = s_wav_pending_bytes;
        }
        portEXIT_CRITICAL(&s_wav_lock);
        return;
    }

    portENTER_CRITICAL(&s_wav_lock);
    s_wav_pending_bytes = pending;
    s_wav_playback_active = (pending > 0) || play_manager_is_active();
    portEXIT_CRITICAL(&s_wav_lock);
}
#endif

static bool wav_playback_consume(size_t bytes)
{
    bool drained = false;
    size_t pending = 0;

    if (play_manager_is_active() || play_manager_pending_bytes() > 0) {
        drained = play_manager_consume(bytes);
        pending = play_manager_pending_bytes();
    } else {
        portENTER_CRITICAL(&s_wav_lock);
        if (s_wav_playback_active) {
            if (bytes >= s_wav_pending_bytes) {
                s_wav_pending_bytes = 0;
                drained = true;
            } else {
                s_wav_pending_bytes -= bytes;
            }
            pending = s_wav_pending_bytes;
            s_wav_playback_active = (pending > 0);
        }
        portEXIT_CRITICAL(&s_wav_lock);
        return drained && (pending == 0);
    }

    portENTER_CRITICAL(&s_wav_lock);
    s_wav_pending_bytes = pending;
    s_wav_playback_active = (pending > 0) || play_manager_is_active();
    portEXIT_CRITICAL(&s_wav_lock);
    return drained && (pending == 0);
}

static void wav_playback_abort(const char *caller)
{
    bool restored = false;
    bool synth_mode = false;
#if CONFIG_BT_MOCK_TESTING
    const char *abort_caller = (caller != NULL) ? caller : "<unknown>";
    bool was_active = false;
    bool had_prev = false;
    size_t pending_before = 0;
    size_t beep_remaining_before = 0;
#endif
    play_manager_abort(false);
    portENTER_CRITICAL(&s_wav_lock);
    if (s_wav_playback_active || s_wav_prev_valid) {
#if CONFIG_BT_MOCK_TESTING
        was_active = s_wav_playback_active;
        had_prev = s_wav_prev_valid;
        pending_before = s_wav_pending_bytes;
#endif
        s_wav_pending_bytes = 0;
        s_wav_playback_active = false;
        s_wav_resume_pipeline = false;
        if (s_wav_prev_valid) {
            s_force_synth = false;
            synth_mode = s_force_synth;
            s_wav_prev_valid = false;
            s_wav_prev_force_synth = false;
            restored = true;
        }
    }
    portEXIT_CRITICAL(&s_wav_lock);

    /* Ensure any transient beep state is cleared when WAV is aborted so
     * we don't leave a tone active. The call to drain_beep_buffer() is
     * safe here and idempotent. */
    drain_beep_buffer();
    portENTER_CRITICAL(&s_beep_lock);
#if CONFIG_BT_MOCK_TESTING
    beep_remaining_before = s_beep_remaining_bytes;
#endif
    s_beep_remaining_bytes = 0;
    portEXIT_CRITICAL(&s_beep_lock);

#if CONFIG_BT_MOCK_TESTING
    ESP_LOGI(TAG, "WAV-ABORT: caller=%s was_active=%d prev_valid=%d pending_before=%lu beep_remaining_before=%lu",
             abort_caller,
             was_active ? 1 : 0,
             had_prev ? 1 : 0,
             (unsigned long)pending_before,
             (unsigned long)beep_remaining_before);
#endif
    if (restored) {
        ESP_LOGI(TAG, "audio_processor: WAV playback aborted (restored synth=%s)", synth_mode ? "ENABLED" : "DISABLED");
    }
    ESP_LOGI(TAG, "audio_processor: wav_playback_abort called (caller=%s) -> s_force_synth=%s s_beep_remaining=%zu",
             (caller != NULL) ? caller : "<unknown>",
             s_force_synth ? "ENABLED" : "DISABLED",
             s_beep_remaining_bytes);
}

static void wav_playback_complete_if_idle(void)
{
    bool restored = false;
    bool synth_mode = false;
    size_t pending = play_manager_pending_bytes();
    bool pm_active = play_manager_is_active();
    bool restart_needed = false;

    portENTER_CRITICAL(&s_wav_lock);
    s_wav_pending_bytes = pending;
    if (s_wav_playback_active && !pm_active && pending == 0) {
        s_wav_playback_active = false;
        restart_needed = s_wav_resume_pipeline;
        s_wav_resume_pipeline = false;
        if (s_wav_prev_valid) {
            s_force_synth = false;
            synth_mode = s_force_synth;
            s_wav_prev_valid = false;
            s_wav_prev_force_synth = false;
            restored = true;
        }
    }
    portEXIT_CRITICAL(&s_wav_lock);

    /* Clear any transient beep state that may have been left around.
     * This prevents short tones from continuing after the WAV file has
     * finished playing. */
    drain_beep_buffer();
    portENTER_CRITICAL(&s_beep_lock);
    s_beep_remaining_bytes = 0;
    portEXIT_CRITICAL(&s_beep_lock);

    if (restored) {
        ESP_LOGI(TAG, "audio_processor: WAV playback completed (restored synth=%s)", synth_mode ? "ENABLED" : "DISABLED");
    }
    ESP_LOGI(TAG, "audio_processor: wav_playback_complete_if_idle -> s_force_synth=%s s_beep_remaining=%zu",
             s_force_synth ? "ENABLED" : "DISABLED",
             s_beep_remaining_bytes);

    if (restart_needed && s_is_initialized) {
        esp_err_t restart_ret = audio_processor_start();
        if (restart_ret != ESP_OK) {
            ESP_LOGE(TAG, "wav_playback_complete_if_idle: failed to resume pipeline (%d %s)", (int)restart_ret, esp_err_to_name(restart_ret));
        }
    }
}

static void wav_refill_from_manager(void)
{
    if (!play_manager_is_active()) {
        return;
    }

    esp_err_t fill_ret = play_manager_fill();
    size_t pending = play_manager_pending_bytes();

    portENTER_CRITICAL(&s_wav_lock);
    s_wav_pending_bytes = pending;
    portEXIT_CRITICAL(&s_wav_lock);

    if (fill_ret != ESP_OK) {
        ESP_LOGE(TAG, "wav_refill_from_manager: fill failed (%d %s)", (int)fill_ret, esp_err_to_name(fill_ret));
        play_manager_abort(false);
        wav_playback_abort(__func__);
        if (s_wav_resume_pipeline && s_is_initialized) {
            esp_err_t restart_ret = audio_processor_start();
            if (restart_ret != ESP_OK) {
                ESP_LOGE(TAG, "wav_refill_from_manager: failed to resume pipeline (%d %s)", (int)restart_ret, esp_err_to_name(restart_ret));
            }
            s_wav_resume_pipeline = false;
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

// Forward declarations of internal functions
static esp_err_t configure_i2s(const audio_config_t* config);
static void log_read_summary(const char *phase, size_t requested, size_t produced);

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

    /* Reset keepalive arming on each init so PLAY failures cannot inherit
     * armed state from earlier tests or sessions. */
    s_keepalive_armed = false;

    synth_manager_reset_state();

    // Copy configuration
    safe_memcpy(&s_audio_config, sizeof(s_audio_config), config, sizeof(audio_config_t));

    /* Initialize descriptor queue and block pool (128 x 1 KiB blocks in DRAM). */
    if (!audio_chunk_pool_init()) {
        ESP_LOGE(TAG, "audio_processor_init: failed to init audio chunk pool");
        return ESP_ERR_NO_MEM;
    }

    bool runtime_psram_ready = false;
#if (defined(CONFIG_SPIRAM) && CONFIG_SPIRAM) || (defined(CONFIG_SPIRAM_SUPPORT) && CONFIG_SPIRAM_SUPPORT)
    runtime_psram_ready = esp_psram_is_initialized();
#endif
    if (s_dram_only_alloc) {
        runtime_psram_ready = false;
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
            /* Carve the single block into three equal sub-buffers: capture, proc, proc2 */
            s_capture_buffer = s_work_block;
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
        return ESP_ERR_NO_MEM;
    }

    /* Initialize statistics */
    safe_memset(&s_audio_stats, sizeof(s_audio_stats), 0, sizeof(audio_stats_t));

    /* Initialize NVS storage helper (best effort) */
    nvs_storage_init();

    /* Initialize play_manager with processing buffers */
    play_manager_buffers_t pm_bufs = {
        .proc_buf = s_proc_buffer,
        .proc_buf2 = s_proc_buffer2,
        .work_bytes = s_runtime_work_bytes,
    };
    esp_err_t pm_ret = play_manager_init(&s_audio_config, &pm_bufs);
    if (pm_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init play_manager: %d", (int)pm_ret);
        return pm_ret;
    }

    /* Initialize i2s_manager with capture + processing buffers */
    i2s_manager_buffers_t i2s_bufs = {
        .raw_buf = s_capture_buffer,
        .raw_buf_bytes = s_runtime_work_bytes,
        .proc_buf = s_proc_buffer,
        .proc_buf2 = s_proc_buffer2,
        .work_bytes = s_runtime_work_bytes,
    };
    esp_err_t i2s_ret = i2s_manager_init(&s_audio_config, &i2s_bufs);
    if (i2s_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init i2s_manager: %d", (int)i2s_ret);
        play_manager_deinit();
        return i2s_ret;
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

    /* Tear down managers and shared pools. */
    wav_playback_abort(__func__);
    play_manager_deinit();
    i2s_manager_deinit();
    audio_chunk_pool_deinit();

    /* Free heap-allocated work buffers (we allocated a single combined
     * block and carved it into three sub-buffers). Free only the block. */
    if (s_work_block) { heap_caps_free(s_work_block); s_work_block = NULL; }
    s_capture_buffer = NULL;
    s_proc_buffer = NULL;
    s_proc_buffer2 = NULL;
    s_runtime_work_bytes = 0U;

    /* Reset transient beep state so subsequent initializations start
     * clean even if a previous session ended while a beep was active. */
    portENTER_CRITICAL(&s_beep_lock);
    s_beep_remaining_bytes = 0;
    s_beep_prefill_active = false;
    s_beep_prefill_accum_bytes = 0;
    s_beep_prefill_goal_bytes = 0;
    portEXIT_CRITICAL(&s_beep_lock);

    s_keepalive_armed = false;

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

    /* Start with keepalive disarmed; successful playback will arm it. */
    s_keepalive_armed = false;

    /* If A2DP is disconnected at start, keep the synth alive to prevent
     * I2S from hammering an absent source. When A2DP is connected we leave
     * synth untouched so the caller’s setting is preserved. */
    if (!audio_processor_is_a2dp_connected()) {
        s_force_synth = true;
    }

    esp_err_t ret = i2s_manager_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start i2s_manager: %d", (int)ret);
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
    AUDIO_PROC_LOG_ONCE();
    if (!s_is_initialized) {
        ESP_LOGE(TAG, "Audio processor not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_is_running) {
        ESP_LOGW(TAG, "Audio processor already stopped");
        return ESP_OK;
    }

    esp_err_t ret = i2s_manager_stop();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop i2s_manager: %d", (int)ret);
        return ret;
    }

    s_is_running = false;
    s_keepalive_armed = false;
    s_force_synth = false;
    s_wav_resume_pipeline = false;
    ESP_LOGI(TAG, "Audio processor stopped");

    wav_playback_abort(__func__);

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

    // Reconfigure managers with updated config
    play_manager_deinit();
    play_manager_buffers_t pm_bufs = {
        .proc_buf = s_proc_buffer,
        .proc_buf2 = s_proc_buffer2,
        .work_bytes = s_runtime_work_bytes,
    };
    esp_err_t ret = play_manager_init(&s_audio_config, &pm_bufs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reconfigure play_manager: %d", ret);
        return ret;
    }

    i2s_manager_deinit();
    i2s_manager_buffers_t i2s_bufs = {
        .raw_buf = s_capture_buffer,
        .raw_buf_bytes = s_runtime_work_bytes,
        .proc_buf = s_proc_buffer,
        .proc_buf2 = s_proc_buffer2,
        .work_bytes = s_runtime_work_bytes,
    };
    ret = i2s_manager_init(&s_audio_config, &i2s_bufs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reconfigure I2S via i2s_manager: %d", ret);
        play_manager_deinit();
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

    safe_memcpy(config, sizeof(*config), &s_audio_config, sizeof(audio_config_t));
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

    safe_memcpy(stats, sizeof(*stats), &s_audio_stats, sizeof(audio_stats_t));
    return ESP_OK;
}

esp_err_t audio_processor_acquire_chunk(audio_chunk_t *out_chunk, TickType_t wait_ticks)
{
    return audio_processor_acquire_chunk_internal(out_chunk, wait_ticks);
}

esp_err_t audio_processor_release_chunk(const audio_chunk_t *chunk)
{
    if (chunk == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (chunk->data != NULL) {
        audio_chunk_release_block(chunk->data);
    }
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

    /* When A2DP is disconnected, avoid emitting keepalive/capture audio.
     * Preserve WAV data so playback can resume after reconnect. */
    if (!audio_processor_is_a2dp_connected()) {
        bool wav_active = play_manager_is_active() || play_manager_pending_bytes() > 0 || s_wav_playback_active;
        s_force_synth = false;           /* Suppress keepalive until real playback succeeds again. */
        s_keepalive_armed = false;
        if (!wav_active) {
            (void)audio_processor_drain_audio_queue();
            s_beep_remaining_bytes = 0;
            s_audio_rb_residual_len = 0;
            s_audio_rb_residual_pos = 0;
        }
        *bytes_read = 0;
        return ESP_ERR_INVALID_STATE;
    }

    /* If no sources are active, treat the read as idle. */
    if (!play_manager_is_active() && s_beep_remaining_bytes == 0 && !s_force_synth && !s_wav_playback_active) {
        (void)audio_processor_drain_audio_queue();
        *bytes_read = 0;
        return ESP_OK;
    }

    if (s_trace_next_read_call) {
        s_trace_next_read_call = false;
        const char *task_name = pcTaskGetName(NULL);
        if (task_name == NULL) {
            task_name = "<unknown>";
        }
        ESP_LOGI(TAG, "audio_processor_read trace: task=%s request_size=%zu", task_name, size);
        printf("TRACE: audio_processor_read task=%s request_size=%zu\n", task_name, size);
    #if CONFIG_BT_MOCK_TESTING
        /* esp_backtrace_print expects a crash frame; calling it from a task can fault.
         * For diagnostics, rely on the log line above unless a valid panic frame is available. */
    #endif
    }

    size_t bytes_written = 0;

    /* Deliver any leftover bytes from the previous dequeue first. */
    bytes_written += residual_copy(buffer, size, s_audio_rb_residual, &s_audio_rb_residual_pos, &s_audio_rb_residual_len);

    while (bytes_written < size) {
        audio_chunk_t chunk = {0};
        esp_err_t acq = audio_processor_acquire_chunk_internal(&chunk, 0);
        if (acq != ESP_OK) {
            break;
        }

        size_t write_offset = bytes_written;
        size_t to_copy = chunk.len;
        if (to_copy > (size - bytes_written)) {
            size_t leftover = to_copy - (size - bytes_written);
            to_copy = size - bytes_written;
            (void)residual_store(chunk.data + to_copy,
                                 leftover,
                                 s_audio_rb_residual,
                                 sizeof(s_audio_rb_residual),
                                 &s_audio_rb_residual_pos,
                                 &s_audio_rb_residual_len);
        } else {
            s_audio_rb_residual_len = 0;
            s_audio_rb_residual_pos = 0;
        }

        safe_memcpy(buffer + write_offset, size - write_offset, chunk.data, to_copy);
        bytes_written += to_copy;

        audio_processor_release_chunk(&chunk);
    }

    *bytes_read = bytes_written;
    if (bytes_written == 0 && s_audio_config.mute) {
        safe_memset(buffer, size, 0, size);
    }
    if (bytes_written < size) {
        s_audio_stats.buffer_underruns++;
    }

    log_read_summary("read", size, bytes_written);
    wav_refill_from_manager();

    return ESP_OK;
}

/*******************************
 * Internal functions
 *******************************/

/**
 * @brief Configure capture path via i2s_manager using shared buffers.
 */
static esp_err_t configure_i2s(const audio_config_t* config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    i2s_manager_deinit();

    i2s_manager_buffers_t bufs = {
        .raw_buf = s_capture_buffer,
        .raw_buf_bytes = s_runtime_work_bytes,
        .proc_buf = s_proc_buffer,
        .proc_buf2 = s_proc_buffer2,
        .work_bytes = s_runtime_work_bytes,
    };

    return i2s_manager_init(config, &bufs);
}

static esp_err_t audio_processor_reinit_i2s(const char *ctx)
{
#ifdef CONFIG_BT_MOCK_TESTING
    (void)ctx;
    return configure_i2s(&s_audio_config);
#else
    if (s_is_running) {
        ESP_LOGW(TAG, "audio_processor_reinit_i2s: called while running (ctx=%s)", ctx ? ctx : "?");
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "audio_processor_reinit_i2s: reconfiguring I2S (ctx=%s)", ctx ? ctx : "?");
    esp_err_t ret = configure_i2s(&s_audio_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "audio_processor_reinit_i2s: configure failed (ctx=%s) ret=%d", ctx ? ctx : "?", ret);
    }
    return ret;
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

esp_err_t audio_processor_drain_audio_queue(void)
{
    AUDIO_PROC_LOG_ONCE();
    if (!s_is_initialized) return ESP_ERR_INVALID_STATE;

    audio_chunk_clear();
    audio_source_tag_reset_buffer();
    beep_manager_stop();

    portENTER_CRITICAL(&s_beep_lock);
    s_beep_remaining_bytes = 0;
    s_beep_prefill_accum_bytes = 0;
    s_beep_restore_synth = false;
    portEXIT_CRITICAL(&s_beep_lock);

    wav_playback_abort(__func__);
    ESP_LOGI(TAG, "audio_processor_drain_audio_queue: cleared audio_queue and beep state");
    return ESP_OK;
}

static void drain_beep_buffer(void)
{
    beep_manager_stop();
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
     * synthesize the same beep tone so the snapshot is representative of
     * an actual audio tone rather than a memset test pattern. */
#if defined(CONFIG_AUDIO_USE_SYNTH_SOURCE)
    generated = synth_manager_generate_audio(s_proc_buffer, target, &s_audio_config, &s_force_synth, &s_beep_lock);
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
     * the same fade duration as the runtime beep so diagnostics and live
     * playback match. */
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
    audio_convert_args_t conv_args = {
        .src = s_proc_buffer,
        .dst = s_proc_buffer,
        .src_size = generated,
        .src_bit_depth = s_audio_config.bit_depth,
        .dst_bit_depth = s_audio_config.bit_depth,
        .dst_size = &conv_size,
        .work_bytes = audio_get_runtime_work_bytes(),
    };
    if (convert_audio_format(&conv_args) != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    if (conv_size == 0) return ESP_ERR_INVALID_SIZE;

    audio_resample_args_t res_args = {
        .src = s_proc_buffer,
        .dst = s_proc_buffer2,
        .src_size = conv_size,
        .src_rate = s_audio_config.sample_rate,
        .dst_rate = s_audio_config.sample_rate,
        .bit_depth = s_audio_config.bit_depth,
        .channels = s_audio_config.channels,
        .dst_size = &res_size,
        .work_bytes = audio_get_runtime_work_bytes(),
    };
    if (resample_audio(&res_args) != ESP_OK) {
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
 * This generates a sine-wave beep and pushes the PCM data into the audio
 * ring buffer so it will be played even when the normal I2S source is
 * silent/muted. The function increments s_beep_remaining_bytes so reads can
 * track and bypass mute for the duration of the beep.
 */
esp_err_t audio_processor_beep_tone(uint32_t duration_ms, double freq_hz)
{
    AUDIO_PROC_LOG_ONCE();
    if (!s_is_initialized) {
        ESP_LOGW(TAG, "audio_processor_beep: not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (wav_playback_is_active()) {
        ESP_LOGW(TAG, "audio_processor_beep: busy (WAV active)");
        return ESP_ERR_INVALID_STATE;
    }

    /* Clamp duration to a sane window. */
    if (duration_ms == 0) {
        duration_ms = 50;
    } else if (duration_ms > 20000U) {
        duration_ms = 20000U;
    }

    /* Estimate bytes so mute bypass stays active while the beep is queued/playing. */
    const uint32_t sample_rate = (uint32_t)s_audio_config.sample_rate;
    const uint32_t channels = (s_audio_config.channels == AUDIO_CHANNEL_MONO) ? 1U : 2U;
    size_t frame_bytes = (size_t)audio_bytes_per_sample(s_audio_config.bit_depth) * (size_t)channels;
    if (frame_bytes == 0 || sample_rate == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    uint64_t total_frames = ((uint64_t)duration_ms * (uint64_t)sample_rate) / 1000ULL;
    size_t est_bytes = 0;
    if (total_frames > 0) {
        uint64_t bytes64 = total_frames * (uint64_t)frame_bytes;
        est_bytes = (bytes64 > SIZE_MAX) ? SIZE_MAX : (size_t)bytes64;
    }

    beep_request_t req = {
        .duration_ms = duration_ms,
        .freq_hz = (freq_hz > 0.0) ? freq_hz : 1000.0,
        .amplitude = 0, /* let beep_manager apply its default */
    };

    beep_manager_set_done_callback(audio_processor_beep_done_cb, NULL);
    esp_err_t ret = beep_manager_play(&req, &s_audio_config);
    if (ret != ESP_OK) {
        portENTER_CRITICAL(&s_beep_lock);
        s_beep_remaining_bytes = 0;
        portEXIT_CRITICAL(&s_beep_lock);
        return ret;
    }

    if (est_bytes > 0) {
        portENTER_CRITICAL(&s_beep_lock);
        s_beep_remaining_bytes = est_bytes;
        portEXIT_CRITICAL(&s_beep_lock);
    }

#ifdef UNIT_TEST
    s_last_beep_duration_ms = duration_ms;
    s_last_beep_freq_hz = freq_hz;
#endif

    ESP_LOGI(TAG, "audio_processor_beep: queued duration_ms=%u freq=%.2f est_bytes=%zu", (unsigned)duration_ms, freq_hz, est_bytes);
    return ESP_OK;
}

esp_err_t audio_processor_beep(uint32_t duration_ms)
{
    /* Preserve legacy behavior: default to a 1 kHz tone when no frequency is provided. */
    return audio_processor_beep_tone(duration_ms, 1000.0);
}

#ifdef UNIT_TEST
void audio_processor_get_last_beep_request(uint32_t* duration_ms, double* freq_hz)
{
    if (duration_ms) {
        *duration_ms = s_last_beep_duration_ms;
    }
    if (freq_hz) {
        *freq_hz = s_last_beep_freq_hz;
    }
}
#endif

/**
 * @brief Play a WAV file by reading PCM frames, converting/resampling as needed
 * and enqueueing into the shared audio_queue.
 */
esp_err_t audio_processor_play_wav(const char* path)
{
    AUDIO_PROC_LOG_ONCE();
    /* Diagnostic: record initialization/running state to help trace
     * unexpected INVALID_STATE returns observed in unit tests. Use both
     * ESP_LOG and a plain printf so the test monitor captures the output
     * regardless of log configuration. */
        size_t queue_free = audio_queue_free_bytes();
        ESP_LOGD(TAG, "audio_processor_play_wav: entry (s_is_initialized=%d, s_is_running=%d, queue_free=%zu)",
              (int)s_is_initialized, (int)s_is_running, queue_free);
        printf("DIAG-APLAY-STATE: init=%d run=%d queue_free=%zu path=%s\n",
            (int)s_is_initialized, (int)s_is_running, queue_free, path ? path : "(null)");
    if (!s_is_initialized) return ESP_ERR_INVALID_STATE;
    if (!path) return ESP_ERR_INVALID_ARG;

    /* Drop any synthetic keepalive so WAV output is audible immediately. */
    if (s_force_synth) {
        audio_processor_flush_priority_queues("play_wav");
        s_last_source_was_synth = false;
    }

    /* Reject PLAY while A2DP is disconnected to avoid queueing audio that
     * cannot be consumed. */
    if (!audio_processor_is_a2dp_connected()) {
        ESP_LOGW(TAG, "audio_processor_play_wav: A2DP not connected; rejecting PLAY");
        return ESP_ERR_INVALID_STATE;
    }

    /* Reject WAV playback while a beep is active or queued to keep the
     * paths isolated and avoid audible contention. */
    bool beep_active = (beep_manager_get_state() == BEEP_STATE_PLAYING);
    portENTER_CRITICAL(&s_beep_lock);
    if (!beep_active && s_beep_remaining_bytes > 0) {
        beep_active = true;
    }
    portEXIT_CRITICAL(&s_beep_lock);
    if (beep_active) {
        ESP_LOGW(TAG, "audio_processor_play_wav: busy (beep active)");
        return ESP_ERR_INVALID_STATE;
    }

    s_trace_next_read_call = true;
    ESP_LOGI(TAG, "audio_processor_play_wav: armed one-shot trace for next audio_processor_read call");

    esp_err_t status = ESP_OK;
    bool resume_pipeline = false;

    if (s_is_running) {
        status = audio_processor_stop();
        if (status != ESP_OK) {
            ESP_LOGE(TAG, "audio_processor_play_wav: stop failed (%d %s)", (int)status, esp_err_to_name(status));
            printf("DIAG-APLAY-FAIL: stop-failed %d\n", (int)status);
            goto cleanup;
        }
        resume_pipeline = true;
    }

    esp_err_t reinit_ret = audio_processor_reinit_i2s("play_wav");
    if (reinit_ret != ESP_OK) {
        status = reinit_ret;
        goto cleanup;
    }

    (void)audio_processor_drain_audio_queue();
    drain_beep_buffer();
    portENTER_CRITICAL(&s_beep_lock);
    s_beep_remaining_bytes = 0;
    portEXIT_CRITICAL(&s_beep_lock);

    wav_playback_begin();
    s_wav_resume_pipeline = resume_pipeline;

    status = play_manager_play_wav(path);
    if (status != ESP_OK) {
        wav_playback_abort(__func__);
        goto cleanup;
    }

    portENTER_CRITICAL(&s_wav_lock);
    s_wav_pending_bytes = play_manager_pending_bytes();
    portEXIT_CRITICAL(&s_wav_lock);

    if (!play_manager_is_active() && s_wav_pending_bytes == 0) {
        wav_playback_complete_if_idle();
    }

    ESP_LOGI(TAG, "audio_processor_play_wav: playback streaming for %s", path);
    status = ESP_OK;

cleanup:
    if (status != ESP_OK && resume_pipeline) {
        esp_err_t restart_ret = audio_processor_start();
        if (restart_ret != ESP_OK) {
            ESP_LOGE(TAG, "audio_processor_play_wav: failed to restart pipeline (%d %s)", (int)restart_ret, esp_err_to_name(restart_ret));
            if (status == ESP_OK) {
                status = restart_ret;
            }
        }
        s_wav_resume_pipeline = false;
    }

    if (status == ESP_OK) {
        /* Only arm the synth keepalive once real playback has succeeded. */
        s_keepalive_armed = true;
        /* Disable synth once real playback is active. */
        s_force_synth = false;
    }

    return status;
}

static void log_read_summary(const char *phase, size_t requested, size_t produced)
{
    if (esp_log_level_get(TAG) < ESP_LOG_INFO) {
        return;
    }

    if (AUDIO_DIAG_ENABLED()) {
        ESP_LOGI(TAG, "I2S-OP-STATS: ops=%u bytes=%u timeouts=%u",
                 (unsigned)s_i2s_read_ops,
                 (unsigned)s_i2s_total_read_bytes,
                 (unsigned)s_i2s_timeout_count);
    }

    const char *tag = (phase != NULL) ? phase : "done";
    size_t audio_residual = 0;
    if (s_audio_rb_residual_len > s_audio_rb_residual_pos) {
        audio_residual = s_audio_rb_residual_len - s_audio_rb_residual_pos;
    }
    size_t free_bytes = audio_queue_free_bytes();

    ESP_LOGI(TAG,
             "audio_processor_read[%s]: req=%zu produced=%zu mute=%d beep_remaining=%zu audio_residual=%zu queue_free=%zu underruns=%u overruns=%u",
             tag,
             requested,
             produced,
             (int)s_audio_config.mute,
             s_beep_remaining_bytes,
             audio_residual,
             free_bytes,
             (unsigned)s_audio_stats.buffer_underruns,
             (unsigned)s_audio_stats.buffer_overruns);
}

esp_err_t audio_processor_emit_diag_summary(void)
{
    unsigned ops = __atomic_load_n(&s_i2s_read_ops, __ATOMIC_RELAXED);
    unsigned bytes = __atomic_load_n(&s_i2s_total_read_bytes, __ATOMIC_RELAXED);
    unsigned timeouts = __atomic_load_n(&s_i2s_timeout_count, __ATOMIC_RELAXED);
    uint32_t tag_miss = audio_processor_test_get_tag_miss_count();
    size_t queue_free = audio_queue_free_bytes();

    ESP_LOGI(TAG, "AUDIO-DIAG-SUMMARY: i2s_ops=%u i2s_bytes=%u i2s_timeouts=%u tag_miss=%u queue_free=%zu underruns=%u overruns=%u",
             ops, bytes, timeouts, (unsigned)tag_miss, queue_free,
             (unsigned)s_audio_stats.buffer_underruns,
             (unsigned)s_audio_stats.buffer_overruns);

        /* Also emit a plain-text printf to guarantee visibility in idf_monitor
         * regardless of ESP log filtering or tag-level settings. */
        printf("AUDIO-DIAG-SUMMARY: i2s_ops=%u i2s_bytes=%u i2s_timeouts=%u tag_miss=%u queue_free=%zu underruns=%u overruns=%u\n",
            ops, bytes, timeouts, (unsigned)tag_miss, queue_free,
            (unsigned)s_audio_stats.buffer_underruns,
            (unsigned)s_audio_stats.buffer_overruns);

    return ESP_OK;
}

void audio_processor_arm_probe(size_t n_entries)
{
    if (n_entries == 0) return;
    if (n_entries > I2S_PROBE_MAX_ENTRIES) n_entries = I2S_PROBE_MAX_ENTRIES;
    __atomic_store_n(&s_probe_captured, 0U, __ATOMIC_RELAXED);
    __atomic_store_n(&s_probe_target, (unsigned)n_entries, __ATOMIC_RELAXED);
    ESP_LOGI(TAG, "I2S probe armed for %u entries", (unsigned)n_entries);
}

esp_err_t audio_processor_emit_probe(void)
{
    unsigned captured = __atomic_exchange_n(&s_probe_captured, 0U, __ATOMIC_RELAXED);
    unsigned target = __atomic_exchange_n(&s_probe_target, 0U, __ATOMIC_RELAXED);
    if (captured > target) captured = target;

    if (captured == 0) {
        ESP_LOGI(TAG, "I2S probe: no entries captured");
        printf("I2S-PROBE: none\n");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "I2S probe: captured=%u target=%u", captured, target);
    printf("I2S-PROBE: captured=%u\n", captured);
    for (unsigned i = 0; i < captured && i < I2S_PROBE_MAX_ENTRIES; ++i) {
        i2s_probe_entry_t *e = &s_probe_buf[i];
        /* Print a compact, single-line entry per capture for easy grepping */
        ESP_LOGI(TAG, "I2S-PROBE-ENTRY: idx=%u before=%lld after=%lld dur=%u req=%zu got=%zu err=%d",
                 i,
                 (long long)e->t_before_us,
                 (long long)e->t_after_us,
                 (unsigned)e->dur_us,
                 e->requested,
                 e->got,
                 e->err);
        printf("I2S-PROBE-ENTRY: %u %lld %lld %u %zu %zu %d\n",
               i,
               (long long)e->t_before_us,
               (long long)e->t_after_us,
               (unsigned)e->dur_us,
               e->requested,
               e->got,
               e->err);
    }

    return ESP_OK;
}

esp_err_t audio_processor_dump_tag_queue(size_t max_items, size_t *captured_out)
{
    if (max_items == 0 || max_items > AUDIO_CHUNK_POOL_BLOCKS) {
        max_items = AUDIO_CHUNK_POOL_BLOCKS;
    }

    audio_chunk_t snapshot[AUDIO_CHUNK_POOL_BLOCKS] = {0};
    size_t captured = 0;
    esp_err_t err = audio_descriptor_snapshot(snapshot, max_items, &captured);
    if (captured_out != NULL) {
        *captured_out = captured;
    }

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "AUDIO-TAG-DUMP: snapshot failed (%s)", esp_err_to_name(err));
        return err;
    }

    if (captured == 0) {
        ESP_LOGI(TAG, "AUDIO-TAG-DUMP: queue empty");
        return ESP_OK;
    }

    for (size_t i = 0; i < captured; ++i) {
        ESP_LOGI(TAG, "AUDIO-TAG-DUMP: idx=%zu tag=%d id=%u len=%zu",
                 i,
                 (int)snapshot[i].tag,
                 (unsigned)snapshot[i].tag_id,
                 snapshot[i].len);
    }

    return ESP_OK;
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
        size_t pos = 0;
        size_t tag_len = strlen(tag);
        if (tag_len > sizeof(line) - 3) {
            tag_len = sizeof(line) - 3;
        }
        pos = safe_memcpy(line, sizeof(line), tag, tag_len);
        if (pos < sizeof(line)) {
            line[pos++] = ':';
        }
        if (pos < sizeof(line)) {
            line[pos++] = ' ';
        }
        static const char HEX[] = "0123456789ABCDEF";
        for (size_t i = 0; i < row && (pos + 3U) < sizeof(line); ++i) {
            uint8_t byte = b[off + i];
            line[pos++] = HEX[(byte >> 4) & 0xF];
            line[pos++] = HEX[byte & 0xF];
            line[pos++] = ' ';
        }
        line[(pos < sizeof(line)) ? pos : (sizeof(line) - 1U)] = '\0';
        ESP_LOGI(TAG, "%s", line);
        off += row;
    }
}

/**
 * @brief Apply volume gain to audio buffer
 */
static int16_t clamp_int16(int32_t value)
{
    if (value > INT16_MAX) return INT16_MAX;
    if (value < INT16_MIN) return INT16_MIN;
    return (int16_t)value;
}

static int32_t clamp_int32(int64_t value)
{
    if (value > INT32_MAX) return INT32_MAX;
    if (value < INT32_MIN) return INT32_MIN;
    return (int32_t)value;
}

static void apply_volume(void* buffer, size_t size, uint8_t volume)
{
    if (volume >= 100U || buffer == NULL || size == 0U) {
        return; // No change needed
    }

    if (volume == 0U) {
        safe_memset(buffer, size, 0, size);
        return;
    }

    const int32_t scale = (int32_t)volume;
    const int32_t divisor = 100;

    if (s_audio_config.bit_depth == AUDIO_BIT_DEPTH_16) {
        int16_t* samples = (int16_t*)buffer;
        int sample_count = (int)(size / sizeof(int16_t));

        for (int i = 0; i < sample_count; i++) {
            int32_t scaled = ((int32_t)samples[i] * scale + (divisor / 2)) / divisor;
            samples[i] = clamp_int16(scaled);
        }
    }
    else if (s_audio_config.bit_depth == AUDIO_BIT_DEPTH_24 ||
             s_audio_config.bit_depth == AUDIO_BIT_DEPTH_32) {
        int32_t* samples = (int32_t*)buffer;
        int sample_count = (int)(size / sizeof(int32_t));

        for (int i = 0; i < sample_count; i++) {
            int64_t scaled = ((int64_t)samples[i] * (int64_t)scale + (divisor / 2)) / divisor;
            samples[i] = clamp_int32(scaled);
        }
    }
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

    if (s_audio_config.bit_depth == bit_depth) {
        return ESP_OK; // Nothing to change
    }

    bool was_running = s_is_running;

    if (was_running) {
        esp_err_t ret = audio_processor_stop();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to stop audio processor: %d", ret);
            return ret;
        }
    }

    s_audio_config.bit_depth = bit_depth;

    play_manager_deinit();
    play_manager_buffers_t pm_bufs = {
        .proc_buf = s_proc_buffer,
        .proc_buf2 = s_proc_buffer2,
        .work_bytes = s_runtime_work_bytes,
    };
    esp_err_t ret = play_manager_init(&s_audio_config, &pm_bufs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reconfigure play_manager: %d", ret);
        return ret;
    }

    i2s_manager_deinit();
    i2s_manager_buffers_t i2s_bufs = {
        .raw_buf = s_capture_buffer,
        .raw_buf_bytes = s_runtime_work_bytes,
        .proc_buf = s_proc_buffer,
        .proc_buf2 = s_proc_buffer2,
        .work_bytes = s_runtime_work_bytes,
    };
    ret = i2s_manager_init(&s_audio_config, &i2s_bufs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reconfigure I2S via i2s_manager: %d", ret);
        play_manager_deinit();
        return ret;
    }

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

bool audio_processor_is_i2s_active(void)
{
    AUDIO_PROC_LOG_ONCE();
    return audio_processor_is_i2s_capture_active();
}

static bool audio_processor_is_i2s_capture_active(void)
{
    return s_is_running;
}

/**
 * @brief Arm a one-shot diagnostic dump for the next beep invocation.
 * Call this before issuing `BEEP` to capture worker snapshots.
 */
void audio_processor_enable_next_beep_diag(void)
{
    AUDIO_PROC_LOG_ONCE();
    s_dump_next_beep_diag = true;
    ESP_LOGI(TAG, "audio_processor: next-beep diagnostic enabled");
}

void audio_processor_set_diag_enabled(bool enable)
{
    s_audio_diag_enabled = enable;
    ESP_LOGI(TAG, "audio_processor: diagnostics %s", enable ? "ENABLED" : "DISABLED");
}

bool audio_processor_is_diag_enabled(void)
{
    return s_audio_diag_enabled;
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

bool audio_processor_is_synth_mode_enabled(void)
{
    AUDIO_PROC_LOG_ONCE();
    return s_force_synth;
}

void audio_processor_set_synth_mode(bool enable)
{
    AUDIO_PROC_LOG_ONCE();
    s_force_synth = enable;
    ESP_LOGI(TAG, "audio_processor: synth mode %s", enable ? "ENABLED" : "DISABLED");
}

uint32_t audio_processor_test_get_tag_miss_count(void)
{
    return __atomic_load_n(&s_tag_miss_count, __ATOMIC_RELAXED);
}

void audio_processor_test_reset_tag_miss_count(void)
{
    __atomic_store_n(&s_tag_miss_count, 0U, __ATOMIC_RELAXED);
}

void audio_processor_test_reset_tag_recover_window(void)
{
    s_tag_recover_mute_until = 0;
}

#ifdef CONFIG_BT_MOCK_TESTING
size_t audio_processor_test_get_beep_remaining_bytes(void)
{
    AUDIO_PROC_LOG_ONCE();
    return s_beep_remaining_bytes;
}

size_t audio_processor_test_get_audio_free_bytes(void)
{
    AUDIO_PROC_LOG_ONCE();
    return audio_queue_free_bytes();
}

void audio_processor_test_idle_i2s_failures(int failures, bool synth_enabled, size_t beep_remaining, bool *synth_after, int *failures_after)
{
    AUDIO_PROC_LOG_ONCE();
    s_i2s_consecutive_failures = failures;
    s_force_synth = synth_enabled;
    s_beep_remaining_bytes = beep_remaining;
    s_keepalive_armed = true;
    s_wav_playback_active = false;
    s_last_i2s_failure_log = -I2S_FAILURE_LOG_THROTTLE;
    if (s_i2s_consecutive_failures >= I2S_FAILURE_THRESHOLD &&
        (s_i2s_consecutive_failures - s_last_i2s_failure_log) >= I2S_FAILURE_LOG_THROTTLE &&
        s_keepalive_armed && s_beep_remaining_bytes == 0 && !s_force_synth) {
        s_last_i2s_failure_log = s_i2s_consecutive_failures;
        s_force_synth = true;
        s_i2s_consecutive_failures = 0;
    }
    if (synth_after != NULL) {
        *synth_after = s_force_synth;
    }
    if (failures_after != NULL) {
        *failures_after = s_i2s_consecutive_failures;
    }
}

void audio_processor_test_wav_reset_state(void)
{
    AUDIO_PROC_LOG_ONCE();
    portENTER_CRITICAL(&s_wav_lock);
    s_wav_playback_active = false;
    s_wav_pending_bytes = 0;
    s_wav_prev_valid = false;
    s_wav_prev_force_synth = false;
    s_wav_resume_pipeline = false;
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
    wav_playback_abort(__func__);
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
#endif /* CONFIG_BT_MOCK_TESTING */

#if defined(CONFIG_BT_MOCK_TESTING) || defined(UNIT_TEST)
void audio_processor_test_set_queue_block_override(size_t max_item_bytes)
{
    s_test_queue_block_override = max_item_bytes;
}
#endif /* CONFIG_BT_MOCK_TESTING || UNIT_TEST */

#ifdef CONFIG_BT_MOCK_TESTING
/* Return number of queued audio descriptors (each carries its source tag).
 * Callers can use this to validate producer/consumer balance during tests. */
size_t audio_processor_test_get_tag_used(void)
{
    return audio_descriptor_used();
}
#endif
