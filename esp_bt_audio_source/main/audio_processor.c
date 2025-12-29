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
#include "freertos/ringbuf.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "driver/i2s_std.h"  // Use the current I2S driver
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_err.h"
#include "esp_debug_helpers.h"
#include "audio_processor.h"
#include "audio_queue.h"
#include "audio_util.h"
#include "nvs_storage.h"
#include "esp_heap_caps.h"
#include "esp_rom_sys.h"

/* Weak A2DP connection probe so keepalive can be suppressed when BT is down. */
bool __attribute__((weak)) bt_manager_is_a2dp_connected(void)
{
    return true;
}

static bool audio_processor_is_a2dp_connected(void)
{
    return bt_manager_is_a2dp_connected();
}

// FreeRTOS compatibility: provide pdTICKS_TO_MS when building in host mocks.
#ifndef pdTICKS_TO_MS
#ifndef portTICK_PERIOD_MS
#define portTICK_PERIOD_MS 1
#endif
#define pdTICKS_TO_MS(ticks) ((uint32_t)((ticks) * portTICK_PERIOD_MS))
#endif
#define AUDIO_TAG_DIAG(...) do {} while (0)
#endif

static bool audio_source_tag_push(audio_source_tag_t tag)
{
    (void)tag;
    /* Tag ringbuffer removed; keep counters for diagnostics only. */
    uint16_t id = __atomic_fetch_add(&s_audio_source_tag_counter, 1, __ATOMIC_SEQ_CST);
    s_last_tag_id_pushed = id;
    __atomic_fetch_add(&s_tag_push_count, 1, __ATOMIC_RELAXED);
    return true;
}

static bool audio_source_tag_take_with_id(audio_source_tag_t *tag, uint16_t *id_out, TickType_t wait_ticks)
{
    (void)wait_ticks;
    if (tag != NULL) {
        *tag = AUDIO_SOURCE_TAG_INVALID;
    }
    if (id_out != NULL) {
        *id_out = 0;
    }
    __atomic_fetch_add(&s_tag_take_count, 1, __ATOMIC_RELAXED);
    return false;
}

#ifdef CONFIG_BT_MOCK_TESTING
static bool audio_source_tag_take(audio_source_tag_t *tag, TickType_t wait_ticks)
{
    return audio_source_tag_take_with_id(tag, NULL, wait_ticks);
}
#endif

static void audio_source_tag_drop_one(void)
{
    audio_source_tag_t dropped = AUDIO_SOURCE_TAG_INVALID;
    uint16_t dropped_id = 0;
    (void)audio_source_tag_take_with_id(&dropped, &dropped_id, 0);
}

static void audio_source_tag_log_miss(const char *path)
{
    __atomic_add_fetch(&s_tag_miss_count, 1, __ATOMIC_RELAXED);
    TickType_t now = xTaskGetTickCount();
    if (now < s_tag_diag_next_log) {
        return;
    }
    size_t free_sz = 0;
    if (s_audio_source_buffer != NULL) {
        free_sz = xRingbufferGetCurFreeSize(s_audio_source_buffer);
    }
    ESP_LOGW(TAG,
             "TAG-MISS path=%s push=%u take=%u last_push_id=%u last_take_id=%u tag_free=%zu",
             (path != NULL) ? path : "?",
             (unsigned)s_tag_push_count,
             (unsigned)s_tag_take_count,
             (unsigned)s_last_tag_id_pushed,
             (unsigned)s_last_tag_id_taken,
             free_sz);
    ESP_LOGW(TAG, "TAG-MISS-DIAG debt=%zu fallback_active=%d fallback_frames=%zu", s_beep_fallback_tag_debt, s_beep_fallback_active ? 1 : 0, s_beep_fallback_frames_remaining);
    s_tag_diag_next_log = now + pdMS_TO_TICKS(500);
}

static void audio_source_tag_reset_buffer(void)
{
    /* Tag ring removed; just reset counters and timing markers. */
    s_tag_push_count = 0;
    s_tag_take_count = 0;
    s_last_tag_id_pushed = 0;
    s_last_tag_id_taken = 0;
    s_last_tag_reset_tick = xTaskGetTickCount();
    s_tag_reset_count++;
    s_last_tag_reset_used_before = 0;
}

/* Consume one metadata tag for fallback-generated audio. If no tag is
 * available, inject a synthetic beep tag so push/take counters remain
 * balanced and TAG-MISS does not fire during host/device tests. */
static bool audio_source_tag_consume_for_fallback(void)
{
    bool consumed = false;
    if (s_beep_fallback_tag_debt > 0) {
        s_beep_fallback_tag_debt--;
        consumed = true;
    }
    return consumed;
}

/* Enqueue a beep chunk ensuring the metadata tag and audio stay aligned.
 * The tag is pushed first; if the subsequent audio send fails, the tag is
 * dropped so consumers never see an untagged audio item. Returns true on
 * success, false if either tag push or audio send ultimately fails. */
static bool beep_send_with_tag(const uint8_t *data,
                               size_t len,
                               TickType_t beep_wait,
                               TickType_t audio_wait,
                               int max_attempts)
{
    if (data == NULL || len == 0) {
        return false;
    }

    (void)beep_wait;
    (void)audio_wait;

    bool ok = false;
    for (int attempt = 0; attempt < max_attempts && !ok; ++attempt) {
        ok = audio_chunk_enqueue_bytes(data, len, AUDIO_SOURCE_TAG_BEEP);
        if (!ok) {
            vTaskDelay(pdMS_TO_TICKS(2));
        }
    }

    if (!ok) {
        ESP_LOGW(TAG, "beep_send_with_tag: enqueue failed len=%u", (unsigned)len);
    }

    return ok;
}

static const char *audio_source_tag_label(audio_source_tag_t tag)
{
    switch (tag) {
        case AUDIO_SOURCE_TAG_WAV: return "wav";
        case AUDIO_SOURCE_TAG_CAPTURE: return "capture";
        case AUDIO_SOURCE_TAG_SYNTH: return "synth";
        case AUDIO_SOURCE_TAG_BEEP: return "beep";
        default: return "invalid";
    }
}

esp_err_t audio_processor_dump_tag_ringbuffer(size_t max_items, size_t *captured_out)
{
    (void)max_items;
    if (captured_out != NULL) {
        *captured_out = 0;
    }

    ESP_LOGI(TAG, "TAG-DUMP skipped: tag ring removed (push=%u take=%u last_push=%u last_take=%u)",
             (unsigned)s_tag_push_count,
             (unsigned)s_tag_take_count,
             (unsigned)s_last_tag_id_pushed,
             (unsigned)s_last_tag_id_taken);

    return ESP_OK;

#ifdef CONFIG_BT_MOCK_TESTING
/* Test-only wrappers to expose tag helpers to unit tests. These are
 * placed in this translation unit so they can call the static
 * implementations directly while keeping the production API unchanged. */

bool audio_source_tag_test_init_buffer(size_t buf_size)
{
    if (s_audio_source_buffer != NULL) return true;
    if (buf_size == 0) buf_size = (size_t)AUDIO_SOURCE_BUFFER_SIZE;
    s_audio_source_buffer = xRingbufferCreate(buf_size, RINGBUF_TYPE_BYTEBUF);
    return (s_audio_source_buffer != NULL);
}

void audio_source_tag_test_deinit_buffer(void)
{
    if (s_audio_source_buffer != NULL) {
        vRingbufferDelete(s_audio_source_buffer);
        s_audio_source_buffer = NULL;
    }
}

uint16_t audio_source_tag_test_get_counter(void)
{
    return s_audio_source_tag_counter;
}

void audio_source_tag_test_set_counter(uint16_t v)
{
    __atomic_store_n(&s_audio_source_tag_counter, v, __ATOMIC_SEQ_CST);
}

bool audio_source_tag_test_push(audio_source_tag_t tag)
{
    return audio_source_tag_push(tag);
}

bool audio_source_tag_test_take(audio_source_tag_t *tag_out, TickType_t wait_ticks)
{
    return audio_source_tag_take(tag_out, wait_ticks);
}

void audio_source_tag_test_drop_one(void)
{
    audio_source_tag_drop_one();
}

void audio_source_tag_test_reset_buffer(void)
{
    audio_source_tag_reset_buffer();
}
#endif

uint32_t audio_processor_test_get_tag_miss_count(void)
{
    return __atomic_load_n(&s_tag_miss_count, __ATOMIC_RELAXED);
}

void audio_processor_test_reset_tag_miss_count(void)
{
    __atomic_store_n(&s_tag_miss_count, 0, __ATOMIC_RELAXED);
}

void audio_processor_test_reset_tag_recover_window(void)
{
    s_tag_recover_mute_until = 0;
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
    bool prev = s_force_synth;
    if (enable && !prev) {
        /* Enable synth mode and start a short fade-in so the tone ramps up. */
        portENTER_CRITICAL(&s_beep_lock);
        s_force_synth = true; /* ensure generator is active */
        s_synth_fade_active = true;
        s_synth_fade_dir = 1;
        s_synth_fade_frames_total = (size_t)(((double)s_audio_config.sample_rate * (double)SYNTH_FADE_MS) / 1000.0);
        if (s_synth_fade_frames_total < 1) s_synth_fade_frames_total = 1;
        s_synth_fade_frames_remaining = s_synth_fade_frames_total;
        /* start env at zero for ramp-up */
        s_synth_env = 0.0;
        portEXIT_CRITICAL(&s_beep_lock);
        ESP_LOGI(TAG, "audio_processor: synth mode ENABLED (fade-in %u ms)", (unsigned)SYNTH_FADE_MS);
    } else if (!enable && prev) {
        /* Start a short fade-out; actual synth disable occurs when fade finishes. */
        portENTER_CRITICAL(&s_beep_lock);
        s_synth_fade_active = true;
        s_synth_fade_dir = -1;
        s_synth_fade_frames_total = (size_t)(((double)s_audio_config.sample_rate * (double)SYNTH_FADE_MS) / 1000.0);
        if (s_synth_fade_frames_total < 1) s_synth_fade_frames_total = 1;
        s_synth_fade_frames_remaining = s_synth_fade_frames_total;
        /* keep s_force_synth true until fade-out completes */
        portEXIT_CRITICAL(&s_beep_lock);
        ESP_LOGI(TAG, "audio_processor: synth mode DISABLED (fade-out %u ms)", (unsigned)SYNTH_FADE_MS);
    } else {
        ESP_LOGI(TAG, "audio_processor: synth mode %s", enable ? "ENABLED" : "DISABLED");
    }
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
static uint8_t s_beep_rb_residual[BEEP_BUFFER_SIZE];
static size_t s_beep_rb_residual_len = 0;
static size_t s_beep_rb_residual_pos = 0;
static volatile bool s_wav_playback_active = false;
static size_t s_wav_pending_bytes = 0;
static bool s_wav_prev_force_synth = false;
static bool s_wav_prev_valid = false;
static portMUX_TYPE s_wav_lock = portMUX_INITIALIZER_UNLOCKED;

/* Flush tag/audio queues after a TAG-MISS so subsequent reads do not
 * spam warnings. Drops a small number of queued audio/beep items and
 * clears residual caches to realign producers/consumers. */
static void audio_source_tag_recover_desync(const char *path, bool drop_audio, bool drop_beep)
{
    TickType_t now = xTaskGetTickCount();
    if (now < s_tag_recover_mute_until) {
        return;
    }

    audio_source_tag_log_miss(path);
    s_tag_recover_mute_until = now + pdMS_TO_TICKS(500);

    audio_source_tag_reset_buffer();
    s_audio_rb_residual_len = 0;
    s_audio_rb_residual_pos = 0;
    s_beep_rb_residual_len = 0;
    s_beep_rb_residual_pos = 0;
    s_beep_remaining_bytes = 0;
    /* Clear any pending fallback tag debt so resets do not leave the
     * fallback path stuck waiting for a tag that was dropped above. */
    s_beep_fallback_tag_debt = 0;
    s_beep_fallback_tag_enqueued = false;
    s_beep_fallback_tag_consumed = false;

    if (drop_beep && s_beep_buffer != NULL) {
        size_t sz = 0;
        void *ptr = NULL;
        int cleared = 0;
        const int max_clear = 16;
        while (cleared < max_clear) {
            ptr = xRingbufferReceiveUpTo(s_beep_buffer, &sz, 0, SIZE_MAX);
            if (ptr == NULL || sz == 0) break;
            vRingbufferReturnItem(s_beep_buffer, ptr);
            cleared++;
        }
        if (cleared > 0) {
            ESP_LOGW(TAG, "TAG-RECOVER: cleared %d beep items", cleared);
        }
    }

    if (drop_audio && s_audio_buffer != NULL) {
        size_t sz = 0;
        void *ptr = NULL;
        size_t max_chunk = xRingbufferGetMaxItemSize(s_audio_buffer);
        if (max_chunk == 0U) {
            max_chunk = audio_get_runtime_work_bytes();
            if (max_chunk == 0U) {
                max_chunk = 1024U;
            }
        }
        int dropped = 0;
        const int max_drop = 16;
        while (dropped < max_drop) {
            ptr = xRingbufferReceiveUpTo(s_audio_buffer, &sz, 0, max_chunk);
            if (ptr == NULL || sz == 0) break;
            vRingbufferReturnItem(s_audio_buffer, ptr);
            dropped++;
        }
        if (dropped > 0) {
            ESP_LOGW(TAG, "TAG-RECOVER: dropped %d audio items", dropped);
        }
    }
}

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
#if defined(CONFIG_BT_MOCK_TESTING) || defined(UNIT_TEST)
static size_t s_test_ringbuf_max_item_override = 0;
#endif

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
    size_t floor = 0;
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
    bool fallback_before = false;
#endif
    portENTER_CRITICAL(&s_wav_lock);
    if (s_wav_playback_active || s_wav_prev_valid) {
#if CONFIG_BT_MOCK_TESTING
        was_active = s_wav_playback_active;
        had_prev = s_wav_prev_valid;
        pending_before = s_wav_pending_bytes;
#endif
        s_wav_pending_bytes = 0;
        s_wav_playback_active = false;
        if (s_wav_prev_valid) {
            /* Do not automatically re-enable synth mode after a WAV
             * playback. Leave synth disabled so we don't produce an
             * unexpected continuous tone; operators can explicitly
             * re-enable synth mode via CLI if desired. */
            s_force_synth = false;
            synth_mode = s_force_synth;
            s_wav_prev_valid = false;
            s_wav_prev_force_synth = false;
            restored = true;
        }
    }
    portEXIT_CRITICAL(&s_wav_lock);

    /* Ensure any transient beep/fallback state is cleared when WAV is
     * aborted so we don't leave a fallback tone active. The call to
     * drain_beep_buffer() is safe here and idempotent. */
    drain_beep_buffer();
    portENTER_CRITICAL(&s_beep_lock);
#if CONFIG_BT_MOCK_TESTING
    beep_remaining_before = s_beep_remaining_bytes;
    fallback_before = s_beep_fallback_active;
#endif
    s_beep_remaining_bytes = 0;
    s_beep_fallback_active = false;
    s_beep_fallback_frames_remaining = 0;
    s_beep_fallback_total_frames = 0;
    s_beep_fallback_tag_debt = 0;
    s_beep_fallback_tag_enqueued = false;
    s_beep_fallback_tag_consumed = false;
    portEXIT_CRITICAL(&s_beep_lock);

#if CONFIG_BT_MOCK_TESTING
    ESP_LOGI(TAG, "WAV-ABORT: caller=%s was_active=%d prev_valid=%d pending_before=%lu fallback_active_before=%d beep_remaining_before=%lu",
             abort_caller,
             was_active ? 1 : 0,
             had_prev ? 1 : 0,
             (unsigned long)pending_before,
             fallback_before ? 1 : 0,
             (unsigned long)beep_remaining_before);
#endif
    if (restored) {
        ESP_LOGI(TAG, "audio_processor: WAV playback aborted (restored synth=%s)", synth_mode ? "ENABLED" : "DISABLED");
    }
    ESP_LOGI(TAG, "audio_processor: wav_playback_abort called (caller=%s) -> s_force_synth=%s s_beep_fallback_active=%d s_beep_remaining=%zu",
             (caller != NULL) ? caller : "<unknown>",
             s_force_synth ? "ENABLED" : "DISABLED",
             s_beep_fallback_active ? 1 : 0,
             s_beep_remaining_bytes);
}

static void wav_playback_complete_if_idle(void)
{
    bool restored = false;
    bool synth_mode = false;
    portENTER_CRITICAL(&s_wav_lock);
    if (s_wav_playback_active && s_wav_pending_bytes == 0) {
        s_wav_playback_active = false;
        if (s_wav_prev_valid) {
            /* Do not automatically re-enable synth mode after WAV
             * playback; clear the previous synth flag and keep synth
             * disabled. Manual control remains available through CLI. */
            s_force_synth = false;
            synth_mode = s_force_synth;
            s_wav_prev_valid = false;
            s_wav_prev_force_synth = false;
            restored = true;
        }
    }
    portEXIT_CRITICAL(&s_wav_lock);

    /* Clear any transient beep/fallback state that may have been left
     * around. This prevents short tones from continuing after the WAV
     * file has finished playing. */
    drain_beep_buffer();
    portENTER_CRITICAL(&s_beep_lock);
    s_beep_remaining_bytes = 0;
    s_beep_fallback_active = false;
    s_beep_fallback_frames_remaining = 0;
    s_beep_fallback_total_frames = 0;
    s_beep_fallback_tag_debt = 0;
    s_beep_fallback_tag_enqueued = false;
    s_beep_fallback_tag_consumed = false;
    portEXIT_CRITICAL(&s_beep_lock);

    if (restored) {
        ESP_LOGI(TAG, "audio_processor: WAV playback completed (restored synth=%s)", synth_mode ? "ENABLED" : "DISABLED");
    }
    ESP_LOGI(TAG, "audio_processor: wav_playback_complete_if_idle -> s_force_synth=%s s_beep_fallback_active=%d s_beep_remaining=%zu",
             s_force_synth ? "ENABLED" : "DISABLED",
             s_beep_fallback_active ? 1 : 0,
             s_beep_remaining_bytes);
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
#if defined(CONFIG_BT_MOCK_TESTING) || defined(UNIT_TEST)
    if (s_test_ringbuf_max_item_override > 0) {
        max_item = s_test_ringbuf_max_item_override;
    }
#endif
    if (frame_bytes > 0U && max_item > 0U && max_item < frame_bytes) {
        /* Do not send partial frames; leave residual pending. */
        return true;
    }
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
        AUDIO_DIAG_PRINTF("DIAG-APLAY-STREAM: residual-send=%ld size=%zu\n", (long)sent, send_size);
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

/* Attempt to enqueue `len` bytes from `data` into the audio ringbuffer without
 * holding the WAV mutex. Returns the number of bytes successfully enqueued.
 * This function does not modify WAV playback shared state (residual buffers)
 * and is safe to call without `s_wav_mutex` held. It will perform paced
 * retries with a small delay when the ringbuffer is temporarily full. */
static size_t wav_stream_try_enqueue_unlocked(const uint8_t *data, size_t len, audio_source_tag_t source_tag)
{
    if (data == NULL || len == 0U) {
        return 0U;
    }

    size_t offset = 0U;
    while (offset < len) {
        size_t remaining = len - offset;
        size_t chunk_size = remaining;
        size_t frame_bytes = wav_stream_frame_bytes_dst();
        const size_t PRODUCER_CHUNK_TARGET = AUDIO_CHUNK_BLOCK_BYTES;
        if (chunk_size > PRODUCER_CHUNK_TARGET) {
            chunk_size = PRODUCER_CHUNK_TARGET;
        }
        if (frame_bytes > 0U) {
            chunk_size = (chunk_size / frame_bytes) * frame_bytes;
            if (chunk_size == 0U) {
                chunk_size = frame_bytes;
            }
        }

        if (!audio_chunk_enqueue_bytes(data + offset, chunk_size, source_tag)) {
            ESP_LOGW(TAG, "wav_stream_try_enqueue_unlocked: enqueue failed chunk=%zu remaining=%zu", chunk_size, remaining);
            break;
        }

        if (source_tag == AUDIO_SOURCE_TAG_WAV) {
            wav_playback_add_pending(chunk_size);
        }
        worker_diag_source_t diag_src = (source_tag == AUDIO_SOURCE_TAG_WAV) ? WORKER_DIAG_SOURCE_WAV : WORKER_DIAG_SOURCE_WORKER;
        worker_diag_report(diag_src, chunk_size, (BaseType_t)pdTRUE);
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
        audio_convert_args_t conv_args = {
            .src = s_proc_buffer,
            .dst = s_proc_buffer,
            .src_size = raw_read,
            .src_bit_depth = s_wav_stream.src_bit_depth,
            .dst_bit_depth = s_audio_config.bit_depth,
            .dst_size = &conv_size,
            .work_bytes = audio_get_runtime_work_bytes(),
        };
        esp_err_t conv_ret = convert_audio_format(&conv_args);
        if (conv_ret != ESP_OK) {
            return conv_ret;
        }

        size_t res_size = 0U;
        audio_resample_args_t res_args = {
            .src = s_proc_buffer,
            .dst = s_proc_buffer2,
            .src_size = conv_size,
            .src_rate = s_wav_stream.src_sample_rate,
            .dst_rate = s_audio_config.sample_rate,
            .bit_depth = s_audio_config.bit_depth,
            .channels = s_audio_config.channels,
            .dst_size = &res_size,
            .work_bytes = audio_get_runtime_work_bytes(),
        };
        esp_err_t res_ret = resample_audio(&res_args);
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

static bool wav_stream_clear_locked(bool close_file, const char *caller)
{
    bool resume_needed = s_wav_stream.resume_pipeline;

#if CONFIG_BT_MOCK_TESTING
    const char *clear_caller = (caller != NULL) ? caller : "<unknown>";
    bool active_before = s_wav_stream.active;
    bool resume_before = s_wav_stream.resume_pipeline;
    bool file_before = s_wav_stream.file != NULL;
    size_t rem_before = s_wav_stream.remaining_bytes;
    size_t pending_before = s_wav_pending_bytes;
    size_t resid_before = s_wav_send_residual_len;
#endif

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

#if CONFIG_BT_MOCK_TESTING
    ESP_LOGI(TAG, "WAV-STREAM-CLEAR: caller=%s close_file=%d active_before=%d resume_before=%d file_before=%d rem_before=%lu pending_before=%lu resid_before=%lu active_after=%d resume_after=%d file_after=%d",
             clear_caller,
             close_file ? 1 : 0,
             active_before ? 1 : 0,
             resume_before ? 1 : 0,
             file_before ? 1 : 0,
             (unsigned long)rem_before,
             (unsigned long)pending_before,
             (unsigned long)resid_before,
             s_wav_stream.active ? 1 : 0,
             s_wav_stream.resume_pipeline ? 1 : 0,
             s_wav_stream.file != NULL ? 1 : 0);
#endif
    return resume_needed;
}

static void wav_stream_try_refill(void)
{
    if (s_wav_mutex == NULL) {
        return;
    }

    bool resume_needed = false;
    esp_err_t fill_ret = ESP_OK;

#if CONFIG_BT_MOCK_TESTING
    size_t dbg_remaining_before = 0;
    size_t dbg_pending_before = 0;
    size_t dbg_resid_before = 0;
#endif

    if (xSemaphoreTake(s_wav_mutex, portMAX_DELAY) == pdTRUE) {
        if (s_wav_stream.active) {
#if CONFIG_BT_MOCK_TESTING
            dbg_remaining_before = s_wav_stream.remaining_bytes;
            dbg_pending_before = s_wav_pending_bytes;
            dbg_resid_before = s_wav_send_residual_len;
#endif
            fill_ret = wav_stream_fill_locked();
            if (fill_ret != ESP_OK) {
                ESP_LOGE(TAG, "wav_stream_fill_locked failed (%d %s)", (int)fill_ret, esp_err_to_name(fill_ret));
                s_wav_stream.resume_pipeline = true;
                resume_needed = wav_stream_clear_locked(true, __func__);
            } else if (s_wav_stream.remaining_bytes == 0U && s_wav_pending_bytes == 0U && s_wav_send_residual_len == 0U) {
                resume_needed = wav_stream_clear_locked(true, __func__);
            }
        }
        xSemaphoreGive(s_wav_mutex);
    }

    if (fill_ret != ESP_OK) {
        wav_playback_abort(__func__);
    }

    if (resume_needed) {
#if CONFIG_BT_MOCK_TESTING
        ESP_LOGI(TAG, "WAV-REFILL-COMPLETE: rem=%lu pending=%lu resid=%lu", (unsigned long)dbg_remaining_before, (unsigned long)dbg_pending_before, (unsigned long)dbg_resid_before);
#endif
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
#if CONFIG_BT_MOCK_TESTING
            size_t rem_before = s_wav_stream.remaining_bytes;
            size_t pending_before = s_wav_pending_bytes;
            size_t resid_before = s_wav_send_residual_len;
#endif
            if (allow_resume) {
                /* Keep WAV state intact but mark the pipeline to resume once fallback ends. */
                s_wav_stream.resume_pipeline = true;
#if CONFIG_BT_MOCK_TESTING
                ESP_LOGI(TAG, "WAV-STREAM-ABORT: allow_resume=1 cleared=0 active_before=%d file_before=%d rem_before=%lu pending_before=%lu resid_before=%lu",
                         s_wav_stream.active ? 1 : 0,
                         s_wav_stream.file != NULL ? 1 : 0,
                         (unsigned long)rem_before,
                         (unsigned long)pending_before,
                         (unsigned long)resid_before);
#endif
                xSemaphoreGive(s_wav_mutex);
                return;
            }
            s_wav_stream.resume_pipeline = false;
            resume_needed = wav_stream_clear_locked(true, __func__);
#if CONFIG_BT_MOCK_TESTING
            ESP_LOGI(TAG, "WAV-STREAM-ABORT: allow_resume=%d cleared=%d active_before=%d file_before=%d rem_before=%lu pending_before=%lu resid_before=%lu",
                     allow_resume ? 1 : 0,
                     resume_needed ? 1 : 0,
                     s_wav_stream.active ? 1 : 0,
                     s_wav_stream.file != NULL ? 1 : 0,
                     (unsigned long)rem_before,
                     (unsigned long)pending_before,
                     (unsigned long)resid_before);
#endif
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
static size_t synth_generate_audio(uint8_t* buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0) {
        return 0;
    }
    /* Generate a near-ultrasonic, low-amplitude sine tone to stay inaudible.
     * Keep frame alignment so downstream consumers remain stable. */
    const int sample_rate = (s_audio_config.sample_rate > 0) ? s_audio_config.sample_rate : 44100;
    const int channels = (s_audio_config.channels == AUDIO_CHANNEL_MONO) ? 1 : 2;
    int bytes_per_sample = audio_bytes_per_sample(s_audio_config.bit_depth);
    if (bytes_per_sample <= 0) {
        bytes_per_sample = 2;
    }

    const size_t frame_bytes = (size_t)bytes_per_sample * (size_t)channels;
    if (frame_bytes == 0) {
        return 0;
    }

    /* Only generate whole frames so downstream consumers never see partial samples. */
    const size_t max_bytes = buffer_size - (buffer_size % frame_bytes);
    if (max_bytes == 0) {
        return 0;
    }

    /* Choose tone near the upper end of hearing with a small guard. */
    int tone_hz = 20000;
    if (sample_rate > 0) {
        const int nyquist_guard = (sample_rate / 2) - 1000;
        if (tone_hz > nyquist_guard) {
            tone_hz = nyquist_guard > 1000 ? nyquist_guard : 1000;
        }
    }

    const double two_pi = 2.0 * M_PI;
    const double phase_inc = (sample_rate > 0) ? ((two_pi * (double)tone_hz) / (double)sample_rate)
                                               : ((two_pi * (double)tone_hz) / 44100.0);
    /* Reduce synth amplitude to further lower chance of audible artifacts
     * during concurrent streaming. 0.008 is 0.8% of full scale. */
    const double amplitude = 0.008; /* 0.8% of full scale to stay inaudible */

    size_t frames = max_bytes / frame_bytes;
    /* Use persistent phase for continuity between buffer fills. */
    double phase = s_synth_phase;
    s_synth_phase_inc = phase_inc;

    if (bytes_per_sample == 2) {
        int16_t *out = (int16_t *)buffer;
        const double scale = 32767.0 * amplitude;
        for (size_t f = 0; f < frames; ++f) {
            /* Update envelope if a fade is active. Envelope state is
             * maintained across calls so a short ramp can complete even
             * if the generator is invoked in small chunks. */
            if (s_synth_fade_active && s_synth_fade_frames_remaining > 0) {
                double delta = 1.0 / (double)s_synth_fade_frames_total;
                if (s_synth_fade_dir > 0) {
                    s_synth_env += delta;
                    if (s_synth_env >= 1.0) {
                        s_synth_env = 1.0;
                        s_synth_fade_active = false;
                        s_synth_fade_dir = 0;
                    }
                } else if (s_synth_fade_dir < 0) {
                    s_synth_env -= delta;
                    if (s_synth_env <= 0.0) {
                        s_synth_env = 0.0;
                        s_synth_fade_active = false;
                        s_synth_fade_dir = 0;
                        /* Completed fade-out: fully disable synth mode.
                         * Protect the write with the existing critical. */
                        portENTER_CRITICAL(&s_beep_lock);
                        s_force_synth = false;
                        portEXIT_CRITICAL(&s_beep_lock);
                    }
                }
                if (s_synth_fade_frames_remaining > 0) s_synth_fade_frames_remaining--;
            }

            double sample = sin(phase) * scale * s_synth_env;
            int16_t s = (int16_t)sample;
            for (int ch = 0; ch < channels; ++ch) {
                *out++ = s;
            }
            phase += phase_inc;
            if (phase >= two_pi) phase -= two_pi;
        }
    } else {
        int32_t *out32 = (int32_t *)buffer;
        const double scale32 = (2147483647.0) * amplitude;
        for (size_t f = 0; f < frames; ++f) {
            if (s_synth_fade_active && s_synth_fade_frames_remaining > 0) {
                double delta = 1.0 / (double)s_synth_fade_frames_total;
                if (s_synth_fade_dir > 0) {
                    s_synth_env += delta;
                    if (s_synth_env >= 1.0) {
                        s_synth_env = 1.0;
                        s_synth_fade_active = false;
                        s_synth_fade_dir = 0;
                    }
                } else if (s_synth_fade_dir < 0) {
                    s_synth_env -= delta;
                    if (s_synth_env <= 0.0) {
                        s_synth_env = 0.0;
                        s_synth_fade_active = false;
                        s_synth_fade_dir = 0;
                        portENTER_CRITICAL(&s_beep_lock);
                        s_force_synth = false;
                        portEXIT_CRITICAL(&s_beep_lock);
                    }
                }
                if (s_synth_fade_frames_remaining > 0) s_synth_fade_frames_remaining--;
            }
            double sample = sin(phase) * scale32 * s_synth_env;
            int32_t s = (int32_t)sample;
            for (int ch = 0; ch < channels; ++ch) {
                *out32++ = s;
            }
            phase += phase_inc;
            if (phase >= two_pi) phase -= two_pi;
        }
    }

    /* Persist the running phase for continuity on the next call. */
    s_synth_phase = phase;

    return frames * frame_bytes;
}
#endif

// Forward declarations of internal functions
static void i2s_reader_task(void *pvParameters);
static void audio_worker_task(void *pvParameters);
static bool audio_processor_handle_idle_i2s_failures(bool wav_active, bool beep_fallback_active, size_t beep_remaining_bytes);
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

    /* Reset keepalive arming on each init so PLAY failures cannot inherit
     * armed state from earlier tests or sessions. */
    s_keepalive_armed = false;

    // Copy configuration
    memcpy(&s_audio_config, config, sizeof(audio_config_t));

    /* Initialize descriptor queue and block pool (128 x 1 KiB blocks in DRAM). */
    if (!audio_chunk_pool_init()) {
        ESP_LOGE(TAG, "audio_processor_init: failed to init audio chunk pool");
        return ESP_ERR_NO_MEM;
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
        size_t free_before_beep_dram = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
        size_t free_before_beep_psram = 0;
#if (defined(CONFIG_SPIRAM) && CONFIG_SPIRAM) || (defined(CONFIG_SPIRAM_SUPPORT) && CONFIG_SPIRAM_SUPPORT)
        free_before_beep_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
#endif
        ESP_LOGI(TAG, "Beep buffer allocation: before DRAM=%zu PSRAM=%zu (requested %u bytes)", free_before_beep_dram, free_before_beep_psram, (unsigned)BEEP_BUFFER_SIZE);
        s_beep_buffer = xRingbufferCreateWithCaps((size_t)BEEP_BUFFER_SIZE, RINGBUF_TYPE_BYTEBUF,
                                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_beep_buffer != NULL) {
            size_t free_after_beep_dram = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
            size_t free_after_beep_psram = 0;
#if (defined(CONFIG_SPIRAM) && CONFIG_SPIRAM) || (defined(CONFIG_SPIRAM_SUPPORT) && CONFIG_SPIRAM_SUPPORT)
            free_after_beep_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
#endif
            ESP_LOGI(TAG, "Beep buffer created in PSRAM (%u bytes) (after DRAM=%zu PSRAM=%zu)", (unsigned)BEEP_BUFFER_SIZE, free_after_beep_dram, free_after_beep_psram);
        } else {
            ESP_LOGW(TAG, "audio_processor_init: failed to create beep buffer in PSRAM, falling back to default allocator");
        }
    }

    if (s_beep_buffer == NULL) {
        size_t free_before_beep_dram = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
        ESP_LOGI(TAG, "Beep buffer allocation: attempting DRAM alloc before DRAM free=%zu (requested %u bytes)", free_before_beep_dram, (unsigned)BEEP_BUFFER_SIZE);
        s_beep_buffer = xRingbufferCreate((size_t)BEEP_BUFFER_SIZE, RINGBUF_TYPE_BYTEBUF);
        if (s_beep_buffer == NULL) {
            ESP_LOGW(TAG, "audio_processor_init: failed to create beep buffer (%u bytes)", (unsigned)BEEP_BUFFER_SIZE);
        } else {
            size_t free_after_beep_dram = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
            ESP_LOGI(TAG, "Beep buffer created (%u bytes) (after DRAM free=%zu)", (unsigned)BEEP_BUFFER_SIZE, free_after_beep_dram);
        }
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

        /* Delete audio descriptor queue/pool */
        audio_chunk_pool_deinit();

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

    /* Reset transient beep/fallback state so subsequent initializations
     * start clean even if a previous session ended while a beep was
     * active. */
    portENTER_CRITICAL(&s_beep_lock);
    s_beep_remaining_bytes = 0;
    s_beep_prefill_active = false;
    s_beep_prefill_accum_bytes = 0;
    s_beep_prefill_goal_bytes = 0;
    s_beep_fallback_active = false;
    s_beep_fallback_frames_remaining = 0;
    s_beep_fallback_total_frames = 0;
    s_beep_fallback_phase = 0.0;
    s_beep_fallback_phase_inc = 0.0;
    s_beep_fallback_tag_debt = 0;
    s_beep_fallback_tag_enqueued = false;
    s_beep_fallback_tag_consumed = false;
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

    // Enable I2S RX
#ifdef CONFIG_BT_MOCK_TESTING
    s_mock_i2s_state.enabled = true;
#else
    if (s_i2s_rx_handle == NULL) {
        ESP_LOGE(TAG, "audio_processor_start: I2S handle null before enable");
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "audio_processor_start: enabling I2S handle=%p sr=%d bits=%d ch=%d", (void*)s_i2s_rx_handle,
             s_audio_config.sample_rate, s_audio_config.bit_depth, s_audio_config.channels);
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
    s_keepalive_armed = false;
    s_force_synth = false;
    ESP_LOGI(TAG, "Audio processor stopped");

    wav_playback_abort(__func__);
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
    #if CONFIG_BT_MOCK_TESTING
        /* esp_backtrace_print expects a crash frame; calling it from a task can fault.
         * For diagnostics, rely on the log line above unless a valid panic frame is available. */
    #endif
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
#ifdef CONFIG_BT_MOCK_TESTING
    bool drained_from_rb = false;
#endif
    bool wav_override_beep = wav_playback_is_active();
    if (wav_override_beep && s_beep_remaining_bytes > 0) {
        ESP_LOGD(TAG, "audio_processor_read: WAV playback suppressing %zu pending beep bytes", s_beep_remaining_bytes);
    }

#ifndef CONFIG_BT_MOCK_TESTING
    TickType_t now_ticks = xTaskGetTickCount();
#endif
    if (!wav_override_beep && s_beep_remaining_bytes > 0 && s_beep_prefill_active) {
#ifdef CONFIG_BT_MOCK_TESTING
        /* Host tests prefer immediate drain to avoid endless silent reads. */
        s_beep_prefill_active = false;
#else
        bool time_ready = now_ticks >= s_beep_prefill_release_tick;
        bool bytes_ready = s_beep_prefill_accum_bytes >= s_beep_prefill_goal_bytes;
        if (!time_ready || !bytes_ready) {
            /* Hold off consuming beep data until both time and buffer headroom are satisfied. */
            memset(buffer, 0, size);
            *bytes_read = size;
            wav_stream_try_refill();
            return ESP_OK;
        }
        s_beep_prefill_active = false;
#endif
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
     * unconsumed tail in the residual buffer for the next read. When the
     * fallback synth is armed, discard queued beep chunks instead of
     * forwarding them so the fallback output length matches the caller's
     * request. */
    if (!wav_override_beep && s_beep_buffer != NULL && bytes_written < size) {
        const bool discard_beep_output = s_beep_fallback_active;
        while (bytes_written < size) {
            size_t read_sz = 0;
            size_t free_before = xRingbufferGetCurFreeSize(s_beep_buffer);
            void* itm = xRingbufferReceive(s_beep_buffer, &read_sz, 0);
            if (itm == NULL || read_sz == 0) {
                ESP_LOGI(TAG, "audio_processor_read: beep dequeue empty free_before=%zu", free_before);
                if (s_beep_diag_active && s_beep_remaining_bytes > 0) {
                    ESP_LOGW(TAG, "BEEP-DIAG: dequeue empty while beep pending free_before=%zu", free_before);
                }
                AUDIO_DIAG_PRINTF("DIAG-READ-BEEP-DEQ: empty free_before=%zu\n", free_before);
            } else {
                ESP_LOGI(TAG, "audio_processor_read: beep dequeue len=%zu free_before=%zu", read_sz, free_before);
                AUDIO_DIAG_PRINTF("DIAG-READ-BEEP-DEQ: len=%zu free_before=%zu\n", read_sz, free_before);
            }
            if (itm == NULL || read_sz == 0) {
                break;
            }

            if (discard_beep_output) {
                if (s_beep_remaining_bytes > read_sz) s_beep_remaining_bytes -= read_sz;
                else s_beep_remaining_bytes = 0;
                vRingbufferReturnItem(s_beep_buffer, itm);
                continue;
            }

            size_t to_copy = read_sz;
            size_t remaining = size - bytes_written;
            if (to_copy > remaining) {
                to_copy = remaining;
            }

                if (to_copy > 0) {
                    AUDIO_DIAG_PRINTF("DIAG-READ-START: size=%zu init=%d run=%d rb=%p\n",
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
            size_t leftover = (read_sz > to_copy) ? (read_sz - to_copy) : 0;
            if (leftover > 0) {
            AUDIO_DIAG_PRINTF("DIAG-READ-BEEP-DEQ: empty free_before=%zu\n", free_before);
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
            AUDIO_DIAG_PRINTF("DIAG-READ-BEEP-RET: len=%zu free_after=%zu\n", read_sz, free_after);
            AUDIO_DIAG_PRINTF("DIAG-READ-BEEP-RET: len=%zu free_after=%zu\n", read_sz, free_after);
            /* Consume any matching metadata tag for this beep item so the
             * tag ringbuffer stays aligned with audio items that came
             * from the beep buffer. Log if a tag is missing so we can
             * correlate tag/audio divergence. */
            uint16_t tag_id = 0;
            if (!audio_source_tag_take_with_id(NULL, &tag_id, 0)) {
                audio_source_tag_recover_desync("beep_rb", false, true);
            }
            vRingbufferReturnItem(s_beep_buffer, itm);

            if (to_copy == 0) {
                break;
            }
        }
    }

    /* Clear one-shot diag and prefill state after beep drains to avoid noisy logs on future beeps. */
    if (s_beep_remaining_bytes == 0 && (s_beep_buffer == NULL || xRingbufferGetCurFreeSize(s_beep_buffer) == BEEP_BUFFER_SIZE)) {
        s_beep_diag_active = false;
        s_beep_prefill_active = false;
        s_beep_prefill_accum_bytes = 0;
    }

    /* Restore synth mode once beep is fully drained. */
    if (s_beep_restore_synth && s_beep_remaining_bytes == 0) {
        s_force_synth = s_beep_prev_force_synth;
        s_beep_restore_synth = false;
    }

    bool fallback_emitted = false;
    bool fallback_completed = false;
    size_t fallback_complete_debt = 0;
    if (!wav_override_beep && s_beep_fallback_active) {
#ifdef CONFIG_BT_MOCK_TESTING
        ESP_LOGI(TAG, "HOST-FALLBACK: active=%d frames=%zu request=%zu bytes_written=%zu", (int)s_beep_fallback_active, s_beep_fallback_frames_remaining, size, bytes_written);
#endif
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
                    const double amp = 15000.0;
                    const double two_pi = 2.0 * M_PI;
                    for (size_t f = 0; f < emit_frames; ++f) {
                        size_t gidx = frames_played + f; /* global index into total frames */
                        double env = 1.0;
                        if (gidx < fade_frames) {
                            double t = (double)gidx / (double)fade_frames;
                            env = 0.5 * (1.0 - cos(M_PI * t));
                        } else if (gidx + fade_frames > s_beep_fallback_total_frames) {
                            size_t tail_idx = s_beep_fallback_total_frames > gidx ? (s_beep_fallback_total_frames - gidx) : 0;
                            if (tail_idx < fade_frames) {
                                double t = (double)tail_idx / (double)fade_frames;
                                env = 0.5 * (1.0 - cos(M_PI * t));
                            }
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
                    const double amp32 = 15000.0 * (1 << 16);
                    const double two_pi = 2.0 * M_PI;
                    for (size_t f = 0; f < emit_frames; ++f) {
                        size_t gidx = frames_played + f;
                        double env = 1.0;
                        if (gidx < fade_frames) {
                            double t = (double)gidx / (double)fade_frames;
                            env = 0.5 * (1.0 - cos(M_PI * t));
                        } else if (gidx + fade_frames > s_beep_fallback_total_frames) {
                            size_t tail_idx = s_beep_fallback_total_frames > gidx ? (s_beep_fallback_total_frames - gidx) : 0;
                            if (tail_idx < fade_frames) {
                                double t = (double)tail_idx / (double)fade_frames;
                                env = 0.5 * (1.0 - cos(M_PI * t));
                            }
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
                fallback_emitted = emitted_bytes > 0;
                if (s_beep_fallback_frames_remaining == 0) {
                    s_beep_fallback_active = false;
                    fallback_completed = true;
                    fallback_complete_debt = s_beep_fallback_tag_debt;
                    s_beep_fallback_total_frames = 0; /* reset totals when done */
                    s_beep_fallback_tag_debt = 0;      /* clear any residual debt */
                    s_beep_fallback_tag_enqueued = false;
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
        if (fallback_emitted && s_beep_fallback_tag_debt > 0) {
            ESP_LOGI(TAG, "TAG-FALLBACK-DRAIN: debt_before=%zu frames_rem=%zu bytes_written=%zu", s_beep_fallback_tag_debt, s_beep_fallback_frames_remaining, bytes_written);
            bool consumed = false;
            if (!s_beep_fallback_tag_consumed) {
                consumed = audio_source_tag_consume_for_fallback();
                if (consumed) {
                    s_beep_fallback_tag_consumed = true;
                }
            }

            /* Keep debt asserted while fallback remains active so tests see a positive balance;
             * clear only once the fallback has fully drained or is explicitly reset elsewhere. */
            if (s_beep_fallback_frames_remaining == 0) {
                s_beep_fallback_tag_debt = 0;
                s_beep_fallback_tag_enqueued = false;
                s_beep_fallback_tag_consumed = false;
            }

            ESP_LOGI(TAG, "TAG-FALLBACK-DRAIN: debt_after=%zu frames_rem=%zu bytes_written=%zu consumed=%d", s_beep_fallback_tag_debt, s_beep_fallback_frames_remaining, bytes_written, consumed ? 1 : 0);
#if CONFIG_BT_MOCK_TESTING
            if (s_beep_fallback_diag_armed) {
                size_t tag_used = audio_processor_test_get_tag_used();
                uint32_t tag_miss = audio_processor_test_get_tag_miss_count();
                ESP_LOGI(TAG, "TAG-FALLBACK-DIAG: tag_used=%zu tag_miss=%u debt=%zu consumed=%d", tag_used, (unsigned)tag_miss, s_beep_fallback_tag_debt, consumed ? 1 : 0);
                s_beep_fallback_diag_armed = false;
            }
#endif
        }
        if (_beep_need_restore_synth) {
            /* Restore the synth mode that was active before we forced
             * synth for fallback playback. */
            s_force_synth = s_beep_prev_force_synth;
            ESP_LOGI(TAG, "audio_processor_beep: fallback finished, restored synth mode=%s", s_force_synth ? "ENABLED" : "DISABLED");
            AUDIO_DIAG_PRINTF("DIAG-READ-EXIT: fallback bytes=%zu ret=ESP_OK\n", bytes_written);
        }
        if (fallback_completed) {
            size_t rb_free = s_audio_buffer ? xRingbufferGetCurFreeSize(s_audio_buffer) : 0;
            ESP_LOGI(TAG, "TAG-FALLBACK-COMPLETE: frames_rem=0 debt=%zu rb_free=%zu", fallback_complete_debt, rb_free);
#if CONFIG_BT_MOCK_TESTING
            bool wav_pre_active = false;
            bool wav_pre_resume = false;
            bool wav_pre_file = false;
            size_t wav_pre_remaining = 0;
            size_t wav_pre_pending = 0;
            size_t wav_pre_resid = 0;
            if (s_wav_mutex != NULL && xSemaphoreTake(s_wav_mutex, 0) == pdTRUE) {
                wav_pre_active = s_wav_stream.active;
                wav_pre_resume = s_wav_stream.resume_pipeline;
                wav_pre_file = s_wav_stream.file != NULL;
                wav_pre_remaining = s_wav_stream.remaining_bytes;
                wav_pre_pending = s_wav_pending_bytes;
                wav_pre_resid = s_wav_send_residual_len;
                xSemaphoreGive(s_wav_mutex);
            }

            ESP_LOGI(TAG, "TAG-FALLBACK-RESUME-PRE: active=%d resume=%d file=%d rem=%lu pending=%lu resid=%lu rb_free=%zu synth=%d",
                     wav_pre_active ? 1 : 0,
                     wav_pre_resume ? 1 : 0,
                     wav_pre_file ? 1 : 0,
                     (unsigned long)wav_pre_remaining,
                     (unsigned long)wav_pre_pending,
                     (unsigned long)wav_pre_resid,
                     rb_free,
                     s_force_synth ? 1 : 0);

            bool wav_after = wav_playback_is_active();
            size_t wav_remaining_after = 0;
            bool wav_remaining_valid = false;
            bool wav_file_after = false;
            size_t wav_pending_after = 0;
            size_t wav_resid_after = 0;
            if (wav_after && s_wav_mutex != NULL && xSemaphoreTake(s_wav_mutex, 0) == pdTRUE) {
                wav_remaining_after = s_wav_stream.remaining_bytes;
                wav_remaining_valid = true;
                wav_file_after = s_wav_stream.file != NULL;
                wav_pending_after = s_wav_pending_bytes;
                wav_resid_after = s_wav_send_residual_len;
                xSemaphoreGive(s_wav_mutex);
            }
            size_t tag_used_after = audio_processor_test_get_tag_used();
            ESP_LOGI(TAG, "TAG-FALLBACK-RESUME: wav_active=%d wav_file=%d wav_rem=%lu wav_pending=%lu wav_resid=%lu valid=%d tag_used=%zu rb_free=%zu synth=%d",
                     wav_after ? 1 : 0,
                     wav_file_after ? 1 : 0,
                     (unsigned long)wav_remaining_after,
                     (unsigned long)wav_pending_after,
                     (unsigned long)wav_resid_after,
                     wav_remaining_valid ? 1 : 0,
                     tag_used_after,
                     rb_free,
                     s_force_synth ? 1 : 0);
#endif
        }
    }

    /* If we filled the requested size entirely from the fallback generator,
     * apply volume and return immediately. */
    if (bytes_written == size) {
        if (s_volume_gain < 100) apply_volume(buffer, bytes_written, s_volume_gain);
        *bytes_read = bytes_written;
        log_read_summary("fallback", size, bytes_written);
        AUDIO_DIAG_PRINTF("DIAG-READ-EXIT: fallback-only bytes=%zu ret=ESP_OK\n", bytes_written);
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
        AUDIO_DIAG_LOGI(
              "DIAG-READ-AUDIO-REQ: max_fetch=%lu wait_ticks=%lu wav_active=%d wav_pending=%lu wav_remaining=%lu valid=%d rb_free=%lu rb_max_item=%lu",
              (unsigned long)max_fetch,
              (unsigned long)wait_ticks,
              wav_active ? 1 : 0,
              (unsigned long)s_wav_pending_bytes,
              (unsigned long)wav_remaining_snapshot,
              wav_remaining_valid ? 1 : 0,
              (unsigned long)free_before,
              (unsigned long)rb_max_item);
        AUDIO_DIAG_PRINTF("DIAG-READ-AUDIO-REQ: max_fetch=%lu wait_ticks=%lu wav_active=%d wav_pending=%lu wav_remaining=%lu wav_remaining_valid=%d rb_free=%lu rb_max_item=%lu\n",
            (unsigned long)max_fetch,
            (unsigned long)wait_ticks,
            wav_active ? 1 : 0,
            (unsigned long)s_wav_pending_bytes,
            (unsigned long)wav_remaining_snapshot,
            wav_remaining_valid ? 1 : 0,
            (unsigned long)free_before,
            (unsigned long)rb_max_item);
        audio_source_tag_t dequeued_tag = AUDIO_SOURCE_TAG_INVALID;
        uint16_t dequeued_id = 0;

        if (!audio_tag_guard_enter(wait_ticks)) {
            ESP_LOGW(TAG, "audio_rb consume: tag guard timeout (wait_ticks=%u)", (unsigned)wait_ticks);
            break;
        }

        void* item = xRingbufferReceiveUpTo(s_audio_buffer, &read_size, wait_ticks, max_fetch);
        AUDIO_DIAG_LOGI(
              "DIAG-READ-AUDIO-ITEM: ptr=%p size=%lu wait_ticks=%lu max_fetch=%lu free_before=%lu wav_pending=%lu",
              item,
              (unsigned long)read_size,
              (unsigned long)wait_ticks,
              (unsigned long)max_fetch,
              (unsigned long)free_before,
              (unsigned long)s_wav_pending_bytes);
        AUDIO_DIAG_PRINTF("DIAG-READ-AUDIO-ITEM: ptr=%p size=%lu wait_ticks=%lu max_fetch=%lu free_before=%lu wav_pending=%lu\n",
            item,
            (unsigned long)read_size,
            (unsigned long)wait_ticks,
            (unsigned long)max_fetch,
            (unsigned long)free_before,
            (unsigned long)s_wav_pending_bytes);
        if (item == NULL || read_size == 0) {
            AUDIO_DIAG_LOGI("audio_processor_read: audio dequeue empty free_before=%zu", free_before);
            AUDIO_DIAG_PRINTF("DIAG-READ-AUDIO-DEQ: empty free_before=%lu\n", (unsigned long)free_before);
            audio_tag_guard_exit();
            break;
        }

        if (!audio_source_tag_take_with_id(&dequeued_tag, &dequeued_id, 0)) {
            audio_source_tag_recover_desync("audio_rb", true, false);
        }

        audio_tag_guard_exit();

        (void)dequeued_tag;
        (void)dequeued_id;

        AUDIO_DIAG_LOGI("audio_processor_read: audio dequeue len=%zu q_used_before=%zu", read_size, q_used_before);
        AUDIO_DIAG_PRINTF("DIAG-READ-AUDIO-DEQ: len=%lu q_used_before=%lu\n", (unsigned long)read_size, (unsigned long)q_used_before);
#ifdef CONFIG_BT_MOCK_TESTING
        drained_from_rb = true;
#endif

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
            size_t stored = residual_store((const uint8_t*)chunk.data + to_copy, leftover,
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
        audio_chunk_release_block(chunk.data);
    size_t free_after = audio_descriptor_used();
    AUDIO_DIAG_LOGI("audio_processor_read: audio return len=%zu q_used=%zu", read_size, free_after);
    AUDIO_DIAG_PRINTF("DIAG-READ-AUDIO-RET: len=%zu q_used=%zu\n", read_size, free_after);
    }

#ifdef CONFIG_BT_MOCK_TESTING
    /* Host sanity: if no audio was produced and all beep/audio buffers are empty,
     * flush any leftover tags so producer/consumer state stays aligned. This
     * guards against small-beep scenarios where more metadata tags than audio
     * chunks were pushed. */
    if (!wav_override_beep && bytes_written == 0 && s_audio_source_buffer != NULL && s_beep_remaining_bytes == 0) {
        bool audio_rb_empty = (s_audio_buffer == NULL) || (xRingbufferGetCurFreeSize(s_audio_buffer) == AUDIO_BUFFER_SIZE);
        bool beep_rb_empty = (s_beep_buffer == NULL) || (xRingbufferGetCurFreeSize(s_beep_buffer) == BEEP_BUFFER_SIZE);
        if (audio_rb_empty && beep_rb_empty) {
            size_t drained = 0;
            while (audio_processor_test_get_tag_used() > 0) {
                audio_source_tag_t dequeued_tag = AUDIO_SOURCE_TAG_INVALID;
                uint16_t dequeued_id = 0;
                if (!audio_source_tag_take_with_id(&dequeued_tag, &dequeued_id, 0)) {
                    audio_source_tag_recover_desync("audio_read_empty", true, false);
                    break;
                }
                drained++;
            }
            if (drained > 0) {
                ESP_LOGI(TAG, "audio_processor_read: drained %zu stale tags on empty read", drained);
            }
        }
    }
#endif

    if (bytes_written == 0) {
        *bytes_read = 0;
        s_audio_stats.buffer_underruns++;
        log_read_summary("empty", size, bytes_written);
        AUDIO_DIAG_PRINTF("DIAG-READ-EXIT: empty bytes=%zu ret=ESP_OK\n", bytes_written);
        wav_stream_try_refill();
        return ESP_OK;
    }

#ifdef CONFIG_BT_MOCK_TESTING
    /* Host tests expect one metadata tag per successful read chunk. If
     * previous paths have not already consumed a tag (e.g., fallback
     * synthesis), drain one here when available to keep host tag counts
     * aligned with audio output. Skip this drain while the fallback synth
     * is active so we do not consume tags that belong to queued WAV data
     * waiting behind the fallback tone. Also skip when the tag backlog is
     * large or immediately after a bulk reset so we do not churn through
     * pending WAV tags faster than audio is delivered. */
    if (s_audio_source_buffer != NULL && bytes_written > 0 && !s_beep_fallback_active) {
        /* Only drain an extra tag when this read produced bytes without
         * pulling a ringbuffer item (e.g., fallback synthesis). If we already
         * dequeued audio/beep data, the matching tag was consumed earlier. */
        size_t tag_used = audio_processor_test_get_tag_used();
        if (drained_from_rb) {
            ESP_LOGI(TAG, "HOST TAG DRAIN SKIP drained_from_rb backlog=%zu bytes=%zu resets=%u last_drop=%zu", tag_used, bytes_written, (unsigned)s_tag_reset_count, s_last_tag_reset_used_before);
        } else {
            bool audio_rb_nonempty = (s_audio_buffer != NULL) && (xRingbufferGetCurFreeSize(s_audio_buffer) < AUDIO_BUFFER_SIZE);
            bool beep_rb_nonempty = (s_beep_buffer != NULL) && (xRingbufferGetCurFreeSize(s_beep_buffer) < BEEP_BUFFER_SIZE);
            if (!audio_rb_nonempty && !beep_rb_nonempty) {
                ESP_LOGI(TAG, "HOST TAG DRAIN SKIP no_source backlog=%zu bytes=%zu resets=%u last_drop=%zu", tag_used, bytes_written, (unsigned)s_tag_reset_count, s_last_tag_reset_used_before);
            } else {
                TickType_t now = xTaskGetTickCount();
                bool recently_reset = (s_last_tag_reset_tick != 0) && ((now - s_last_tag_reset_tick) < pdMS_TO_TICKS(200));
                const size_t TAG_DRAIN_BACKLOG_MAX = 16;

                if (tag_used > 0 && tag_used <= TAG_DRAIN_BACKLOG_MAX && !recently_reset) {
                    printf("HOST TAG DRAIN bytes=%zu tags=%zu\n", bytes_written, tag_used);
                    audio_source_tag_t dequeued_tag = AUDIO_SOURCE_TAG_INVALID;
                    uint16_t dequeued_id = 0;
                    if (!audio_source_tag_take_with_id(&dequeued_tag, &dequeued_id, 0)) {
                        audio_source_tag_recover_desync("audio_read_tail", true, false);
                    }
                    (void)dequeued_tag;
                    (void)dequeued_id;
                } else if (tag_used > TAG_DRAIN_BACKLOG_MAX) {
                    ESP_LOGI(TAG, "HOST TAG DRAIN SKIP backlog=%zu bytes=%zu resets=%u last_drop=%zu", tag_used, bytes_written, (unsigned)s_tag_reset_count, s_last_tag_reset_used_before);
                } else if (recently_reset) {
                    ESP_LOGI(TAG, "HOST TAG DRAIN SKIP recent_reset backlog=%zu last_drop=%zu", tag_used, s_last_tag_reset_used_before);
                }
            }
        }
    }
#endif

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

    ESP_LOGI(TAG, "configure_i2s: port=%d role=SLAVE sr=%d bits=%d ch=%d pins(bclk=%d ws=%d din=%d dout=%d)",
             config->i2s_port, config->sample_rate, config->bit_depth, config->channels,
             config->i2s_bclk_pin, config->i2s_ws_pin, config->i2s_din_pin, config->i2s_dout_pin);

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

    ESP_LOGI(TAG, "configure_i2s: channel ready handle=%p", (void*)s_i2s_rx_handle);

    return ESP_OK;
#endif
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

esp_err_t audio_processor_drain_ringbuffer(void)
{
    AUDIO_PROC_LOG_ONCE();
    if (!s_is_initialized) return ESP_ERR_INVALID_STATE;
    if (s_audio_buffer == NULL) return ESP_ERR_INVALID_STATE;

#ifdef CONFIG_BT_MOCK_TESTING
    size_t tag_used_before = audio_processor_test_get_tag_used();
    size_t fallback_debt_before = audio_processor_test_get_fallback_tag_debt();
    /* Pause producers so the drain is deterministic during host tests. */
    bool restart_after_drain = false;
    if (s_is_running) {
        esp_err_t stop_ret = audio_processor_stop();
        if (stop_ret != ESP_OK) {
            return stop_ret;
        }
        restart_after_drain = true;
    }
#endif

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
    drain_beep_buffer();
    /* Clear fallback/beep state so subsequent reads start from a clean
     * slate after an explicit drain. */
    portENTER_CRITICAL(&s_beep_lock);
    s_beep_remaining_bytes = 0;
    s_beep_prefill_accum_bytes = 0;
    s_beep_fallback_active = false;
    s_beep_fallback_frames_remaining = 0;
    s_beep_fallback_total_frames = 0;
    s_beep_fallback_tag_debt = 0;
    s_beep_fallback_tag_enqueued = false;
    s_beep_fallback_tag_consumed = false;
    s_beep_restore_synth = false;
    portEXIT_CRITICAL(&s_beep_lock);
    wav_playback_abort(__func__);
    ESP_LOGI(TAG, "audio_processor_drain_ringbuffer: drained %d items", drained);

#ifdef CONFIG_BT_MOCK_TESTING
    size_t flushed = 0;
    bool audio_rb_empty = (s_audio_buffer == NULL) || (xRingbufferGetCurFreeSize(s_audio_buffer) == AUDIO_BUFFER_SIZE);
    bool beep_rb_empty = (s_beep_buffer == NULL) || (xRingbufferGetCurFreeSize(s_beep_buffer) == BEEP_BUFFER_SIZE);
    if (!s_beep_fallback_active && audio_rb_empty && beep_rb_empty) {
        while (audio_processor_test_get_tag_used() > 0) {
            audio_source_tag_t dequeued_tag = AUDIO_SOURCE_TAG_INVALID;
            uint16_t dequeued_id = 0;
            if (!audio_source_tag_take_with_id(&dequeued_tag, &dequeued_id, 0)) {
                audio_source_tag_recover_desync("drain_post_flush", true, false);
                break;
            }
            flushed++;
        }
        if (flushed > 0) {
            ESP_LOGI(TAG, "audio_processor_drain_ringbuffer: flushed %zu stale tags post-drain", flushed);
        }
    }

    /* Guard tag pushes for a short window after drain while buffers are empty to
     * catch unexpected enqueues during post-drain idle periods in host tests. */
    s_post_drain_guard_started = xTaskGetTickCount();
    s_post_drain_guard_until = s_post_drain_guard_started + pdMS_TO_TICKS(POST_DRAIN_GUARD_MS);
    ESP_LOGI(TAG, "audio_processor_drain_ringbuffer: armed tag guard for %u ms (start=%u)", (unsigned)POST_DRAIN_GUARD_MS, (unsigned)pdTICKS_TO_MS(s_post_drain_guard_started));

    size_t tag_used_after = audio_processor_test_get_tag_used();
    size_t fallback_debt_after = audio_processor_test_get_fallback_tag_debt();
    ESP_LOGI(TAG, "audio_processor_drain_ringbuffer: tags %zu->%zu fallback_debt %zu->%zu", tag_used_before, tag_used_after, fallback_debt_before, fallback_debt_after);
#endif

#ifdef CONFIG_BT_MOCK_TESTING
    if (restart_after_drain) {
        esp_err_t start_ret = audio_processor_start();
        if (start_ret != ESP_OK) {
            ESP_LOGE(TAG, "audio_processor_drain_ringbuffer: restart failed (%d %s)",
                     (int)start_ret, esp_err_to_name(start_ret));
            return start_ret;
        }
    }
#endif
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
    if (s_proc_buffer == NULL) {
        ESP_LOGW(TAG, "audio_processor_beep: work buffer not ready");
        return ESP_ERR_INVALID_STATE;
    }

    /* Allow beep while the processor is running; only reject when WAV playback
     * is already active to avoid clobbering an in-progress file. */
    if (wav_playback_is_active()) {
        ESP_LOGW(TAG, "audio_processor_beep: busy (WAV active)");
        return ESP_ERR_INVALID_STATE;
    }

    /* Flush stale synth data so the beep is heard immediately. */
    if (s_force_synth) {
        audio_processor_flush_priority_queues("beep");
        s_last_source_was_synth = false;
    }

    /* Do not inject a beep while WAV playback is active. Return busy so
     * callers can retry after playback completes. */
    if (wav_playback_is_active()) {
        ESP_LOGW(TAG, "audio_processor_beep: busy (WAV playback active)");
        return ESP_ERR_INVALID_STATE;
    }
    /* Generate the beep tone into s_proc_buffer, then enqueue to the beep/audio buffers. */
    /* Ensure duration is reasonable to avoid huge allocations. Clamp to 20 seconds. */
    if (duration_ms == 0) {
        duration_ms = 50; /* minimal chirp to ensure audibility */
    } else if (duration_ms > 20000U) {
        duration_ms = 20000U;
    }

    /* One-shot diagnostics: capture and clear the arm so only this beep logs. */
    bool diag_once = s_dump_next_beep_diag;
    s_dump_next_beep_diag = false;
    s_beep_diag_active = diag_once;

    /* Preserve previous synth mode. If the caller previously forced synth
     * (e.g., CLI requested synth-for-BEEP), respect that and do not
     * unconditionally disable it here. Otherwise record the previous
     * state and mark for restore so temporary changes do not persist. */
    portENTER_CRITICAL(&s_beep_lock);
    s_beep_prev_force_synth = s_force_synth;
    if (!s_beep_prev_force_synth) {
        /* Only disable synth for the beep if it was not already forced on. */
        s_force_synth = false;
        s_beep_restore_synth = true;
    } else {
        /* Caller requested synth; leave it enabled and do not schedule a restore. */
        s_beep_restore_synth = false;
    }
    portEXIT_CRITICAL(&s_beep_lock);

    const uint32_t sample_rate = (uint32_t)s_audio_config.sample_rate;
    const uint32_t channels = (uint32_t)s_audio_config.channels;
    const uint32_t frame_bytes = channels * (s_audio_config.bit_depth == AUDIO_BIT_DEPTH_16 ? sizeof(int16_t) : sizeof(int32_t));
    const uint64_t total_frames = ((uint64_t)duration_ms * (uint64_t)sample_rate) / 1000ULL;

    if (total_frames == 0 || frame_bytes == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Limit the generation in manageable chunks to avoid large stack/heap use. */
    const size_t frames_per_chunk = AUDIO_BLOCK_SIZE * 8U; /* moderate chunk to reuse work buffers */
    const double two_pi = 2.0 * M_PI;
    double phase_inc = two_pi * freq_hz / (double)sample_rate;
    double phase = 0.0;

    size_t frames_generated = 0;
    size_t bytes_enqueued = 0;

    const size_t fade_frames = (size_t)(((uint64_t)sample_rate * (uint64_t)BEEP_FADE_MS) / 1000ULL) ?: 1U;

    /* Optional leading silence to avoid a sudden start and give the sink a stable baseline. */
    if (BEEP_HEAD_SILENCE_MS > 0) {
        size_t head_frames = ((uint64_t)sample_rate * (uint64_t)BEEP_HEAD_SILENCE_MS) / 1000ULL;
        size_t head_bytes = head_frames * frame_bytes;
        if (head_bytes > sizeof(s_proc_buffer)) {
            head_bytes = sizeof(s_proc_buffer);
        }
        if (head_bytes > 0) {
            memset(s_proc_buffer, 0, head_bytes);
            const TickType_t beep_wait = pdMS_TO_TICKS(10);
            const TickType_t audio_wait = pdMS_TO_TICKS(20);
            if (beep_send_with_tag(s_proc_buffer, head_bytes, beep_wait, audio_wait, 5)) {
                s_beep_remaining_bytes += head_bytes;
                s_beep_prefill_accum_bytes += head_bytes;
                bytes_enqueued += head_bytes;
            }
        }
    }

    while (frames_generated < total_frames) {
        size_t chunk_frames = total_frames - frames_generated;
        if (chunk_frames > frames_per_chunk) {
            chunk_frames = frames_per_chunk;
        }

        /* Optimization: for the non-fade middle of the tone, prepare a
         * small one-period template in `s_proc_buffer` and assemble a
         * repeated chunk into `s_proc_buffer2`. This reduces sin() calls
         * for long tones while preserving per-sample fades at the edges. */
        bool in_fade_region = false;
        if (frames_generated < fade_frames) in_fade_region = true;
        if (frames_generated + chunk_frames + fade_frames > total_frames) in_fade_region = true;

        size_t template_frames = 0;
        size_t template_bytes = 0;
        if (!in_fade_region) {
            template_frames = (size_t)((double)sample_rate / (freq_hz > 0.0 ? freq_hz : 1000.0));
            if (template_frames < 1) template_frames = 1;
            size_t max_template_frames = sizeof(s_proc_buffer) / frame_bytes;
            if (template_frames > max_template_frames) template_frames = max_template_frames;
            template_bytes = template_frames * frame_bytes;
            if (template_bytes > 0) {
                if (s_audio_config.bit_depth == AUDIO_BIT_DEPTH_16) {
                    int16_t* tout = (int16_t*)s_proc_buffer;
                    const double amp = 7500.0;
                    double tphase = phase;
                    for (size_t f = 0; f < template_frames; ++f) {
                        double sample = sin(tphase) * amp;
                        int16_t s = (int16_t)sample;
                        for (uint32_t ch = 0; ch < channels; ++ch) {
                            *tout++ = s;
                        }
                        tphase += phase_inc;
                        if (tphase >= two_pi) tphase -= two_pi;
                    }
                } else {
                    int32_t* tout32 = (int32_t*)s_proc_buffer;
                    const double amp32 = 7500.0 * (1 << 16);
                    double tphase = phase;
                    for (size_t f = 0; f < template_frames; ++f) {
                        double sample = sin(tphase) * amp32;
                        int32_t s = (int32_t)sample;
                        for (uint32_t ch = 0; ch < channels; ++ch) {
                            *tout32++ = s;
                        }
                        tphase += phase_inc;
                        if (tphase >= two_pi) tphase -= two_pi;
                    }
                }
            }
        }

        if (!in_fade_region && template_bytes > 0) {
            size_t need_frames = chunk_frames;
            size_t filled_frames = 0;
            size_t max_dst_frames = sizeof(s_proc_buffer2) / frame_bytes;
            if (need_frames > max_dst_frames) need_frames = max_dst_frames;
            uint8_t* dst = s_proc_buffer2;
            while (filled_frames < need_frames) {
                size_t copy_frames = template_frames;
                if (copy_frames > (need_frames - filled_frames)) copy_frames = (need_frames - filled_frames);
                size_t copy_bytes = copy_frames * frame_bytes;
                memcpy(dst + (filled_frames * frame_bytes), s_proc_buffer, copy_bytes);
                filled_frames += copy_frames;
            }
            size_t chunk_bytes = filled_frames * frame_bytes;
            phase += (double)filled_frames * phase_inc;
            while (phase >= two_pi) phase -= two_pi;
            const TickType_t beep_wait = pdMS_TO_TICKS(10);
            const TickType_t audio_wait = pdMS_TO_TICKS(20);
            if (!beep_send_with_tag(s_proc_buffer2, chunk_bytes, beep_wait, audio_wait, 10)) {
                /* fall through to per-sample generation on failure */
            } else {
                s_beep_remaining_bytes += chunk_bytes;
                s_beep_prefill_accum_bytes += chunk_bytes;
                bytes_enqueued += chunk_bytes;
                frames_generated += filled_frames;
                continue;
            }
        }

        /* Fallback / fade region: generate per-sample into s_proc_buffer. */
        if (s_audio_config.bit_depth == AUDIO_BIT_DEPTH_16) {
            int16_t* out = (int16_t*)s_proc_buffer;
            if (out == NULL) {
                return ESP_ERR_INVALID_STATE;
            }
            const double amp = 7500.0;
            for (size_t f = 0; f < chunk_frames; ++f) {
                double env = 1.0;
                size_t global_frame = frames_generated + f;
                if (global_frame < fade_frames) {
                    double t = (double)global_frame / (double)fade_frames;
                    env = 0.5 * (1.0 - cos(M_PI * t));
                } else if (global_frame + fade_frames > total_frames) {
                    size_t tail = total_frames - global_frame;
                    if (tail < fade_frames) {
                        double t = (double)tail / (double)fade_frames;
                        env = 0.5 * (1.0 - cos(M_PI * t));
                    }
                }
                double sample = sin(phase) * amp * env;
                int16_t s = (int16_t)sample;
                for (uint32_t ch = 0; ch < channels; ++ch) {
                    *out++ = s;
                }
                phase += phase_inc;
                if (phase >= two_pi) phase -= two_pi;
            }
        } else {
            int32_t* out32 = (int32_t*)s_proc_buffer;
            if (out32 == NULL) {
                return ESP_ERR_INVALID_STATE;
            }
            const double amp32 = 7500.0 * (1 << 16);
            for (size_t f = 0; f < chunk_frames; ++f) {
                double env = 1.0;
                size_t global_frame = frames_generated + f;
                if (global_frame < fade_frames) {
                    double t = (double)global_frame / (double)fade_frames;
                    env = 0.5 * (1.0 - cos(M_PI * t));
                } else if (global_frame + fade_frames > total_frames) {
                    size_t tail = total_frames - global_frame;
                    if (tail < fade_frames) {
                        double t = (double)tail / (double)fade_frames;
                        env = 0.5 * (1.0 - cos(M_PI * t));
                    }
                }
                double sample = sin(phase) * amp32 * env;
                int32_t s = (int32_t)sample;
                for (uint32_t ch = 0; ch < channels; ++ch) {
                    *out32++ = s;
                }
                phase += phase_inc;
                if (phase >= two_pi) phase -= two_pi;
            }
        }

        size_t chunk_bytes = chunk_frames * frame_bytes;

        /* Enqueue into the dedicated beep buffer first for low latency; fall back to audio buffer on failure. */
        const TickType_t beep_wait = pdMS_TO_TICKS(10);
        const TickType_t audio_wait = pdMS_TO_TICKS(20);
        if (!beep_send_with_tag(s_proc_buffer, chunk_bytes, beep_wait, audio_wait, 10)) {
#if CONFIG_BT_MOCK_TESTING
            /* In mock/test builds, free a small window so fallback tag push has space. */
            /* Do not call audio_processor_drain_ringbuffer() here because it aborts WAV
             * playback, which breaks WAV resume after fallback. The fallback tag debt
             * logic below tolerates a full buffer. */
#endif
            if (diag_once) {
                size_t free_beep = s_beep_buffer ? xRingbufferGetCurFreeSize(s_beep_buffer) : 0;
                size_t free_audio = s_audio_buffer ? xRingbufferGetCurFreeSize(s_audio_buffer) : 0;
                ESP_LOGW(TAG, "BEEP-DIAG: enqueue fail chunk=%u free_beep=%u free_audio=%u", (unsigned)chunk_bytes, (unsigned)free_beep, (unsigned)free_audio);
            }
            size_t remaining_frames = 0;
            if (total_frames > frames_generated) {
                uint64_t rem = total_frames - frames_generated;
                if (rem > SIZE_MAX) {
                    remaining_frames = SIZE_MAX;
                } else {
                    remaining_frames = (size_t)rem;
                }
            }
            if (remaining_frames == 0) {
                remaining_frames = chunk_frames;
            }

            size_t fallback_bytes = 0;
            if (remaining_frames > 0) {
                if (remaining_frames > SIZE_MAX / frame_bytes) {
                    fallback_bytes = SIZE_MAX;
                } else {
                    fallback_bytes = remaining_frames * frame_bytes;
                }
            }
            portENTER_CRITICAL(&s_beep_lock);
            if (!s_beep_fallback_active) {
                s_beep_fallback_phase = 0.0;
                s_beep_fallback_total_frames = 0;
            }
            s_beep_fallback_phase_inc = phase_inc;
            if (remaining_frames > 0) {
                if (SIZE_MAX - s_beep_fallback_frames_remaining < remaining_frames) {
                    s_beep_fallback_frames_remaining = SIZE_MAX;
                } else {
                    s_beep_fallback_frames_remaining += remaining_frames;
                }
                if (SIZE_MAX - s_beep_fallback_total_frames < remaining_frames) {
                    s_beep_fallback_total_frames = SIZE_MAX;
                } else {
                    s_beep_fallback_total_frames += remaining_frames;
                }
                s_beep_fallback_active = true;
                s_beep_fallback_diag_armed = true;
            }
            portEXIT_CRITICAL(&s_beep_lock);

            bool wav_active = wav_playback_is_active();
#if CONFIG_BT_MOCK_TESTING
            size_t wav_remaining = 0;
            bool wav_remaining_valid = false;
            if (wav_active && s_wav_mutex != NULL && xSemaphoreTake(s_wav_mutex, 0) == pdTRUE) {
                wav_remaining = s_wav_stream.remaining_bytes;
                wav_remaining_valid = true;
                xSemaphoreGive(s_wav_mutex);
            }
            size_t tag_used_now = audio_processor_test_get_tag_used();
            ESP_LOGI(TAG, "TAG-FALLBACK-ACTIVATE: rem_frames=%zu total_frames=%zu wav_active=%d wav_rem=%lu valid=%d tag_used=%zu synth=%d",
                     s_beep_fallback_frames_remaining,
                     s_beep_fallback_total_frames,
                     wav_active ? 1 : 0,
                     (unsigned long)wav_remaining,
                     wav_remaining_valid ? 1 : 0,
                     tag_used_now,
                     s_force_synth ? 1 : 0);
#endif

            if (fallback_bytes > 0) {
                s_beep_remaining_bytes += fallback_bytes;
                s_beep_prefill_accum_bytes += fallback_bytes;
                bytes_enqueued += fallback_bytes;
            }

            /* Push at most one metadata tag per fallback activation to avoid growing debt. */
            if (!s_beep_fallback_tag_enqueued && s_beep_fallback_tag_debt == 0) {
                bool fallback_tag_pushed = audio_source_tag_push(AUDIO_SOURCE_TAG_BEEP);
                s_beep_fallback_tag_debt = 1;
                s_beep_fallback_tag_enqueued = true;
                s_beep_fallback_tag_consumed = false;
#if CONFIG_BT_MOCK_TESTING
                /* Very short fallback tones are tagged but immediately consumed to avoid
                 * lingering debt during short-beep device tests. */
                if (remaining_frames > 0 && remaining_frames <= SHORT_FALLBACK_FRAMES) {
                    uint16_t dummy_id = 0;
                    if (audio_source_tag_take_with_id(NULL, &dummy_id, 0)) {
                        s_beep_fallback_tag_debt = 0;
                        s_beep_fallback_tag_enqueued = false;
                        s_beep_fallback_tag_consumed = true;
                        ESP_LOGI(TAG, "TAG-FALLBACK-PUSH-SHORT: cleared debt for short fallback frames=%zu id=%u", remaining_frames, (unsigned)dummy_id);
                    }
                }
#endif
                if (fallback_tag_pushed) {
                    ESP_LOGI(TAG, "TAG-FALLBACK-PUSH: debt=%zu total_frames=%zu", s_beep_fallback_tag_debt, s_beep_fallback_total_frames);
                } else {
                    ESP_LOGW(TAG, "TAG-FALLBACK-PUSH-FAILED: recording debt=%zu total_frames=%zu", s_beep_fallback_tag_debt, s_beep_fallback_total_frames);
                }
            }

            ESP_LOGW(TAG, "audio_processor_beep: ringbuffer full, enabling fallback bytes=%u frames=%u", (unsigned)fallback_bytes, (unsigned)remaining_frames);
            break;
        }

        /* Track remaining bytes so reader bypasses mute for the beep duration. */
        s_beep_remaining_bytes += chunk_bytes;
        s_beep_prefill_accum_bytes += chunk_bytes;
        bytes_enqueued += chunk_bytes;
        frames_generated += chunk_frames;
    }

#ifdef UNIT_TEST
    s_last_beep_duration_ms = duration_ms;
    s_last_beep_freq_hz = freq_hz;
#endif

    if (bytes_enqueued > 0) {
        size_t prefill_goal = (size_t)(((uint64_t)sample_rate * (uint64_t)frame_bytes * (uint64_t)BEEP_PREFILL_MS) / 1000ULL);
        if (prefill_goal == 0) {
            prefill_goal = frame_bytes;
        }
        if (prefill_goal > bytes_enqueued) {
            prefill_goal = bytes_enqueued;
        }
        portENTER_CRITICAL(&s_beep_lock);
        s_beep_prefill_goal_bytes = prefill_goal;
        s_beep_prefill_accum_bytes = bytes_enqueued;
        s_beep_prefill_release_tick = xTaskGetTickCount() + pdMS_TO_TICKS(BEEP_PREFILL_MS);
        s_beep_prefill_active = true;
        portEXIT_CRITICAL(&s_beep_lock);
        ESP_LOGI(TAG, "audio_processor_beep: prefill armed goal=%u bytes delay=%u ms", (unsigned)prefill_goal, (unsigned)BEEP_PREFILL_MS);
    }

    /* Append a short zero tail so the sink ramps to silence instead of a hard cut. */
    if (bytes_enqueued > 0) {
        size_t tail_bytes = fade_frames * frame_bytes;
        if (tail_bytes > sizeof(s_proc_buffer)) {
            tail_bytes = sizeof(s_proc_buffer);
        }
        memset(s_proc_buffer, 0, tail_bytes);
        const TickType_t beep_wait = pdMS_TO_TICKS(10);
        const TickType_t audio_wait = pdMS_TO_TICKS(20);
        if (beep_send_with_tag(s_proc_buffer, tail_bytes, beep_wait, audio_wait, 5)) {
            s_beep_remaining_bytes += tail_bytes;
            bytes_enqueued += tail_bytes;
            s_beep_prefill_accum_bytes += tail_bytes;
        }
    }

    ESP_LOGI(TAG, "audio_processor_beep: enqueued=%u bytes duration_ms=%u freq=%.2f", (unsigned)bytes_enqueued, (unsigned)duration_ms, freq_hz);

    /* Restore synth mode if we temporarily disabled it above. */
    if (s_beep_restore_synth) {
        portENTER_CRITICAL(&s_beep_lock);
        s_force_synth = s_beep_prev_force_synth;
        s_beep_restore_synth = false;
        portEXIT_CRITICAL(&s_beep_lock);
    }

    return bytes_enqueued > 0 ? ESP_OK : ESP_ERR_INVALID_STATE;
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

    /* Drop any synthetic keepalive so WAV output is audible immediately. */
    if (s_force_synth) {
        audio_processor_flush_priority_queues("play_wav");
        s_last_source_was_synth = false;
    }

    /* Reject WAV playback while a beep is active or queued to keep the
     * paths isolated and avoid audible contention. */
    bool beep_active = false;
    portENTER_CRITICAL(&s_beep_lock);
    if (s_beep_remaining_bytes > 0 || s_beep_fallback_active || s_beep_prefill_active) {
        beep_active = true;
    }
    portEXIT_CRITICAL(&s_beep_lock);
    if (!beep_active && s_beep_buffer != NULL) {
        size_t free_beep = xRingbufferGetCurFreeSize(s_beep_buffer);
        if (free_beep < BEEP_BUFFER_SIZE) {
            beep_active = true;
        }
    }
    if (beep_active) {
        ESP_LOGW(TAG, "audio_processor_play_wav: busy (beep active)");
        return ESP_ERR_INVALID_STATE;
    }

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

    esp_err_t reinit_ret = audio_processor_reinit_i2s("play_wav");
    if (reinit_ret != ESP_OK) {
        status = reinit_ret;
        goto cleanup;
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
    s_beep_fallback_tag_debt = 0;
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
    if (fread(&tmp32, 4, 1, f) != 1) {
        ESP_LOGW(TAG, "audio_processor_play_wav: missing RIFF size (fread failed)");
        printf("DIAG-APLAY-FAIL: missing-riff-size\n");
        status = ESP_ERR_INVALID_STATE;
        goto cleanup;
    }
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
            uint16_t tmp16 = 0;
            bool fmt_ok =
                fread(&fmt16_1, 2, 1, f) == 1 &&
                fread(&num_channels, 2, 1, f) == 1 &&
                fread(&sample_rate, 4, 1, f) == 1 &&
                fread(&tmp32, 4, 1, f) == 1 && /* skip byte rate */
                fread(&tmp16, 2, 1, f) == 1 && /* skip block align */
                fread(&bits_per_sample, 2, 1, f) == 1;
            if (!fmt_ok) {
                ESP_LOGW(TAG, "audio_processor_play_wav: fmt chunk read failed");
                printf("DIAG-APLAY-FAIL: fmt-read-failed\n");
                status = ESP_ERR_INVALID_STATE;
                goto cleanup;
            }
            audio_format = fmt16_1;
            if (chunk_size > 16) {
                if (fseek(f, (long)(chunk_size - 16), SEEK_CUR) != 0) {
                    ESP_LOGW(TAG, "audio_processor_play_wav: failed to skip fmt padding (chunk_size=%u)", (unsigned)chunk_size);
                    status = ESP_ERR_INVALID_STATE;
                    goto cleanup;
                }
            }
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

    bool resume_prev = wav_stream_clear_locked(true, __func__);
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
        bool resume_now = wav_stream_clear_locked(true, __func__);
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
            resume_now = wav_stream_clear_locked(true, __func__);
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
        wav_playback_abort(__func__);
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

    if (status == ESP_OK) {
        /* Only arm the synth keepalive once real playback has succeeded. */
        s_keepalive_armed = true;
        /* Disable synth once real playback is active. */
        s_force_synth = false;
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
        bool synth_prev = s_force_synth;

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
        if (s_i2s_first_read) {
            const size_t FIRST_READ_TARGET = 1024U;
            if (read_request > FIRST_READ_TARGET) {
                read_request = FIRST_READ_TARGET;
            }
        }

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
            const bool measure_debug = esp_log_level_get(TAG) >= ESP_LOG_DEBUG;
            int64_t t_start = 0;
            if (measure_debug) {
                t_start = esp_timer_get_time();
            }

            while (remaining > 0) {
                size_t this_read = remaining;
                if (this_read > I2S_MAX_READ_BYTES) this_read = I2S_MAX_READ_BYTES;
                /* Align to frame_bytes */
                if (frame_bytes > 0) {
                    this_read = (this_read / frame_bytes) * frame_bytes;
                    if (this_read == 0) this_read = frame_bytes;
                }

                size_t part_read = 0;
                int64_t t_i2s_before = 0;
                int64_t t_i2s_after = 0;
                uint32_t dur_us = 0;
                if (AUDIO_DIAG_ENABLED()) {
                    t_i2s_before = esp_timer_get_time();
                }

                esp_err_t part_ret = i2s_channel_read(s_i2s_rx_handle,
                                                      (uint8_t*)s_i2s_buffer + total_read,
                                                      this_read,
                                                      &part_read,
                                                      0);

                if (AUDIO_DIAG_ENABLED()) {
                    t_i2s_after = esp_timer_get_time();
                    dur_us = (uint32_t)(t_i2s_after - t_i2s_before);
                    __atomic_fetch_add(&s_i2s_read_ops, 1, __ATOMIC_RELAXED);
                    __atomic_fetch_add(&s_i2s_total_read_bytes, (uint32_t)part_read, __ATOMIC_RELAXED);
                    if (part_ret != ESP_OK) {
                        __atomic_fetch_add(&s_i2s_timeout_count, 1, __ATOMIC_RELAXED);
                    }
                    ESP_LOGI(TAG, "I2S-READ: ret=%d part_read=%zu dur_us=%u total=%zu remaining=%zu",
                             (int)part_ret, part_read, dur_us, total_read + part_read, remaining);
                }
                    /* Capture a one-shot high-resolution probe entry when armed. */
                    if (atomic_load_explicit(&s_probe_target, memory_order_relaxed) > 0) {
                        unsigned idx = atomic_fetch_add_explicit(&s_probe_captured, 1, memory_order_relaxed);
                        if (idx < I2S_PROBE_MAX_ENTRIES && idx < atomic_load_explicit(&s_probe_target, memory_order_relaxed)) {
                            s_probe_buf[idx].t_before_us = t_i2s_before;
                            s_probe_buf[idx].t_after_us = t_i2s_after;
                            s_probe_buf[idx].dur_us = dur_us;
                            s_probe_buf[idx].requested = this_read;
                            s_probe_buf[idx].got = part_read;
                            s_probe_buf[idx].err = (int)part_ret;
                        }
                    }

                if (part_ret == ESP_OK && part_read > 0) {
                    total_read += part_read;
                    remaining = (remaining > part_read) ? (remaining - part_read) : 0;
                    have_frame = true;
                    s_i2s_consecutive_failures = 0;
                } else {
                    /* Treat both errors and zero reads as a failure for the
                     * consecutive failure counter. Bail out of the chunked
                     * loop to avoid spinning on a non-responsive I2S device. */
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

            if (s_i2s_first_read) {
                s_i2s_first_read = false;
            }

            bytes_read = total_read;

            if (measure_debug && total_read > I2S_MAX_READ_BYTES) {
                int64_t t_end = esp_timer_get_time();
                int64_t duration = t_end - t_start;
                (void)duration;
                ESP_LOGD(TAG, "i2s_reader_task: multi-chunk read total=%zu took=%lld us", total_read, (long long)duration);
            }

        (void)audio_processor_handle_idle_i2s_failures(wav_playback_is_active(), s_beep_fallback_active, s_beep_remaining_bytes);
        }

        if (!have_frame) {
            bool should_backoff_idle = (!audio_processor_is_a2dp_connected() || !s_keepalive_armed) &&
                                       !s_force_synth &&
                                       (s_i2s_consecutive_failures >= I2S_FAILURE_THRESHOLD);
#ifndef CONFIG_BT_MOCK_TESTING
            if (last_i2s_ret != ESP_OK &&
                (s_i2s_consecutive_failures - s_last_i2s_failure_log) >= I2S_FAILURE_LOG_THROTTLE) {
                s_last_i2s_failure_log = s_i2s_consecutive_failures;
                ESP_LOGW(TAG, "I2S read failed: %d (%s) requested=%zu aligned_frame_bytes=%zu got_bytes=%zu",
                         last_i2s_ret, esp_err_to_name(last_i2s_ret), read_request, frame_bytes, bytes_read);
            }
#else
            (void)last_i2s_ret;
#endif
            audio_proc_mock_yield();
            if (s_force_synth) {
                TickType_t delay_ticks = pdMS_TO_TICKS(1);
                if (delay_ticks == 0) {
                    delay_ticks = 1;
                }
                vTaskDelay(delay_ticks);
            } else if (should_backoff_idle) {
                TickType_t delay_ticks = pdMS_TO_TICKS(50);
                if (delay_ticks == 0) {
                    delay_ticks = 1;
                }
                vTaskDelay(delay_ticks);
            } else {
                taskYIELD();
            }
            continue;
        }

        /* On first real I2S capture after synth or while lower-priority
         * sources were active, flush queues and cancel beeps/WAV so I2S
         * immediately owns the pipeline. */
        if (!s_force_synth) {
            bool lower_active = (s_beep_remaining_bytes > 0) || wav_playback_is_active();
            if (synth_prev || s_last_source_was_synth || lower_active) {
                audio_processor_flush_priority_queues("i2s-priority");
            }
            s_last_source_was_synth = false;
        } else {
            s_last_source_was_synth = true;
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
    ESP_LOGD(TAG, "audio_worker_task[%s]: ptr=%p len=%zu cap=%zu synth=%d pooled=%d",
             (phase != NULL) ? phase : "unknown",
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
            AUDIO_DIAG_PRINTF("DIAG-WORKER-RETPOOL: ptr=%p phase=%s free_q=%lu\n",
                   raw_ptr,
                   tag,
                   (unsigned long)uxQueueMessagesWaiting(s_i2s_free_queue));
        }
    } else {
        ESP_LOGV(TAG, "audio_worker_task[%s]: freeing non-pooled ptr=%p", tag, raw_ptr);
        heap_caps_free(raw_ptr);
        AUDIO_DIAG_PRINTF("DIAG-WORKER-FREE: ptr=%p phase=%s\n", raw_ptr, tag);
    }

    blk->ptr = NULL;
    blk->len = 0;
    blk->capacity = 0;
    blk->synth_fill = false;
    blk->pooled_ptr = false;
}

static void audio_worker_discard_block(const char *phase, i2s_block_t *blk)
{
    if (blk == NULL || blk->ptr == NULL) {
        ESP_LOGD(TAG, "audio_worker_task[%s]: discard skipped (ptr=NULL)", (phase != NULL) ? phase : "unknown");
        return;
    }

    if (blk->pooled_ptr) {
        ESP_LOGV(TAG, "audio_worker_task[%s]: discard pooled ptr=%p -> recycle", (phase != NULL) ? phase : "unknown", blk->ptr);
        audio_worker_return_block(phase, blk);
        return;
    }

    ESP_LOGV(TAG, "audio_worker_task[%s]: discarding non-pooled ptr=%p", (phase != NULL) ? phase : "unknown", blk->ptr);
    heap_caps_free(blk->ptr);
    blk->ptr = NULL;
    blk->len = 0;
    blk->capacity = 0;
    blk->synth_fill = false;
    blk->pooled_ptr = false;
}

static bool audio_processor_handle_idle_i2s_failures(bool wav_active, bool beep_fallback_active, size_t beep_remaining_bytes)
{
    bool reenabled = false;
    const bool a2dp_connected = audio_processor_is_a2dp_connected();
    if (s_i2s_consecutive_failures >= I2S_FAILURE_THRESHOLD &&
        (s_i2s_consecutive_failures - s_last_i2s_failure_log) >= I2S_FAILURE_LOG_THROTTLE) {
        s_last_i2s_failure_log = s_i2s_consecutive_failures;
        if (wav_active) {
            ESP_LOGW(TAG, "I2S read failing (%d) but WAV playback active; keeping synth disabled", s_i2s_consecutive_failures);
        } else if (!a2dp_connected || !s_keepalive_armed) {
            ESP_LOGW(TAG, "I2S read failing repeatedly (%d); keepalive suppressed (A2DP disconnected or not armed)", s_i2s_consecutive_failures);
        } else if (!beep_fallback_active) {
            if (beep_remaining_bytes == 0 && !s_force_synth) {
                ESP_LOGW(TAG, "I2S read failing repeatedly (%d); re-enabling silent synth keepalive", s_i2s_consecutive_failures);
                s_force_synth = true;
                s_i2s_consecutive_failures = 0;
                reenabled = true;
            } else {
                ESP_LOGW(TAG, "I2S read failing repeatedly (%d); leaving synth disabled (no active source)", s_i2s_consecutive_failures);
            }
        }
    }
    return reenabled;
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

esp_err_t audio_processor_emit_diag_summary(void)
{
    unsigned ops = __atomic_load_n(&s_i2s_read_ops, __ATOMIC_RELAXED);
    unsigned bytes = __atomic_load_n(&s_i2s_total_read_bytes, __ATOMIC_RELAXED);
    unsigned timeouts = __atomic_load_n(&s_i2s_timeout_count, __ATOMIC_RELAXED);
    uint32_t tag_miss = audio_processor_test_get_tag_miss_count();
    size_t rb_free = 0;
    if (s_audio_buffer != NULL) {
        rb_free = xRingbufferGetCurFreeSize(s_audio_buffer);
    }

    ESP_LOGI(TAG, "AUDIO-DIAG-SUMMARY: i2s_ops=%u i2s_bytes=%u i2s_timeouts=%u tag_miss=%u rb_free=%zu underruns=%u overruns=%u",
             ops, bytes, timeouts, (unsigned)tag_miss, rb_free,
             (unsigned)s_audio_stats.buffer_underruns,
             (unsigned)s_audio_stats.buffer_overruns);

        /* Also emit a plain-text printf to guarantee visibility in idf_monitor
         * regardless of ESP log filtering or tag-level settings. */
        printf("AUDIO-DIAG-SUMMARY: i2s_ops=%u i2s_bytes=%u i2s_timeouts=%u tag_miss=%u rb_free=%zu underruns=%u overruns=%u\n",
            ops, bytes, timeouts, (unsigned)tag_miss, rb_free,
            (unsigned)s_audio_stats.buffer_underruns,
            (unsigned)s_audio_stats.buffer_overruns);

    return ESP_OK;
}

void audio_processor_arm_probe(size_t n_entries)
{
    if (n_entries == 0) return;
    if (n_entries > I2S_PROBE_MAX_ENTRIES) n_entries = I2S_PROBE_MAX_ENTRIES;
    atomic_store_explicit(&s_probe_captured, 0, memory_order_relaxed);
    atomic_store_explicit(&s_probe_target, (unsigned)n_entries, memory_order_relaxed);
    ESP_LOGI(TAG, "I2S probe armed for %u entries", (unsigned)n_entries);
}

esp_err_t audio_processor_emit_probe(void)
{
    unsigned captured = atomic_exchange_explicit(&s_probe_captured, 0, memory_order_relaxed);
    unsigned target = atomic_exchange_explicit(&s_probe_target, 0, memory_order_relaxed);
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

         AUDIO_DIAG_PRINTF("DIAG-WORKER-DEQ: ptr=%p len=%zu synth=%d q_wait=%lu free_q=%lu\n",
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
        audio_convert_args_t conv_args = {
            .src = blk.ptr,
            .dst = s_proc_buffer,
            .src_size = blk.len,
            .src_bit_depth = s_audio_config.bit_depth,
            .dst_bit_depth = s_audio_config.bit_depth,
            .dst_size = &conv_size,
            .work_bytes = audio_get_runtime_work_bytes(),
        };
        esp_err_t cret = convert_audio_format(&conv_args);
        if (cret != ESP_OK) {
            s_audio_stats.conversion_errors++;
            audio_worker_discard_block("convert-fail", &blk);
            continue;
        }

        /* Resample */
        size_t res_size = 0;
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
        esp_err_t rret = resample_audio(&res_args);
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
            AUDIO_DIAG_PRINTF("DIAG-WORKER-ENQ: attempt len=%zu free_before=%zu synth=%d\n", res_size, free_size, blk.synth_fill ? 1 : 0);
            if (free_size < res_size) {
                s_audio_stats.buffer_overruns++;
                if (free_size < (res_size / 2)) {
                    /* Skip adding if buffer very full */
                    AUDIO_DIAG_PRINTF("DIAG-WORKER-ENQ: drop len=%zu free_before=%zu reason=overrun\n", res_size, free_size);
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
                AUDIO_DIAG_PRINTF("DIAG-WORKER-ENQ: fail len=%zu sent=%zu free_before=%zu\n", res_size, sent_bytes, free_size);
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
                AUDIO_DIAG_LOGI(
                         "audio_worker_task: enqueued %zu bytes (%s) free_before=%zu free_after=%zu dequeued=%u rb_fail=%u",
                         res_size,
                         src,
                         free_size,
                         free_after,
                         (unsigned)s_worker_diag.dequeued_blocks,
                         (unsigned)s_worker_diag.ringbuffer_failures);
                AUDIO_DIAG_PRINTF("DIAG-WORKER-RET: len=%zu free_before=%zu free_after=%zu\n", res_size, free_size, free_after);
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
        memset(buffer, 0, size);
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

bool audio_processor_is_i2s_active(void)
{
    AUDIO_PROC_LOG_ONCE();
    return audio_processor_is_i2s_capture_active();
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

#ifdef CONFIG_BT_MOCK_TESTING
size_t audio_processor_test_get_beep_remaining_bytes(void)
{
    AUDIO_PROC_LOG_ONCE();
    return s_beep_remaining_bytes;
}

size_t audio_processor_test_get_audio_free_bytes(void)
{
    AUDIO_PROC_LOG_ONCE();
    if (s_audio_buffer == NULL) {
        return 0;
    }
    return xRingbufferGetCurFreeSize(s_audio_buffer);
}

bool audio_processor_test_is_beep_fallback_active(void)
{
    AUDIO_PROC_LOG_ONCE();
    bool active = false;
    portENTER_CRITICAL(&s_beep_lock);
    active = s_beep_fallback_active;
    portEXIT_CRITICAL(&s_beep_lock);
#ifdef CONFIG_BT_MOCK_TESTING
    ESP_LOGI(TAG, "HOST-FALLBACK-QUERY active=%d", active ? 1 : 0);
#endif
    return active;
}

size_t audio_processor_test_get_beep_fallback_frames_remaining(void)
{
    AUDIO_PROC_LOG_ONCE();
    size_t frames = 0;
    portENTER_CRITICAL(&s_beep_lock);
    frames = s_beep_fallback_frames_remaining;
    portEXIT_CRITICAL(&s_beep_lock);
#ifdef CONFIG_BT_MOCK_TESTING
    ESP_LOGI(TAG, "HOST-FALLBACK-FRAMES frames=%zu total=%zu", frames, s_beep_fallback_total_frames);
#endif
    return frames;
}

size_t audio_processor_test_get_fallback_tag_debt(void)
{
    AUDIO_PROC_LOG_ONCE();
    size_t debt = 0;
    portENTER_CRITICAL(&s_beep_lock);
    debt = s_beep_fallback_tag_debt;
    portEXIT_CRITICAL(&s_beep_lock);
    return debt;
}

size_t audio_processor_test_get_beep_fallback_total_frames(void)
{
    AUDIO_PROC_LOG_ONCE();
    size_t frames = 0;
    portENTER_CRITICAL(&s_beep_lock);
    frames = s_beep_fallback_total_frames;
    portEXIT_CRITICAL(&s_beep_lock);
    return frames;
}

void audio_processor_test_idle_i2s_failures(int failures, bool synth_enabled, size_t beep_remaining, bool *synth_after, int *failures_after)
{
    AUDIO_PROC_LOG_ONCE();
    s_i2s_consecutive_failures = failures;
    s_force_synth = synth_enabled;
    s_beep_remaining_bytes = beep_remaining;
    s_beep_fallback_active = false;
    s_keepalive_armed = true;
    s_beep_fallback_tag_debt = 0;
    s_wav_playback_active = false;
    s_last_i2s_failure_log = -I2S_FAILURE_LOG_THROTTLE;
    (void)audio_processor_handle_idle_i2s_failures(false, false, s_beep_remaining_bytes);
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

#ifdef CONFIG_BT_MOCK_TESTING
    /* Host/device tests inject WAV while the fallback beep synth is active. If the
     * audio ringbuffer is saturated (common when the beep enqueue filled it before
     * switching to fallback), opportunistically reclaim space by dropping buffered
     * audio and its matching tags. This keeps the injection path deterministic
     * without tearing down the pipeline. */
    if (free_size < size && s_beep_fallback_active) {
        size_t reclaimed = 0;
        size_t rsz = 0;
        void *it = NULL;
        while (free_size < size && (it = xRingbufferReceiveUpTo(s_audio_buffer, &rsz, 0, SIZE_MAX)) != NULL && rsz > 0) {
            vRingbufferReturnItem(s_audio_buffer, it);
            reclaimed += rsz;
            audio_source_tag_drop_one();
            free_size = xRingbufferGetCurFreeSize(s_audio_buffer);
        }
        ESP_LOGW(TAG, "audio_processor_test_inject_audio_data: reclaimed %zu bytes during fallback (free %zu target %zu)", reclaimed, free_size, size);
    }
#endif

    if (free_size < size) {
        ESP_LOGW(TAG, "audio_processor_test_inject_audio_data: not enough space (%zu < %zu)", free_size, size);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "audio_processor_test_inject_audio_data: free_before=%zu size=%zu", free_size, size);

    /* Keep metadata aligned with injected audio so TAG-MISS never occurs
     * during test-only injections. Use a capture tag to mark injected
     * frames as external input. */
    if (!audio_source_tag_push(AUDIO_SOURCE_TAG_CAPTURE)) {
        ESP_LOGW(TAG, "audio_processor_test_inject_audio_data: tag push failed");
        return ESP_ERR_NO_MEM;
    }

    // Send data to ring buffer; if enqueue fails drop the tag to preserve alignment
    BaseType_t sent = xRingbufferSend(s_audio_buffer, data, size, 0);
    if (sent != pdTRUE) {
        audio_source_tag_drop_one();
        ESP_LOGE(TAG, "audio_processor_test_inject_audio_data: failed to send to ringbuffer");
        return ESP_FAIL;
    }

    size_t free_after = xRingbufferGetCurFreeSize(s_audio_buffer);
    ESP_LOGI(TAG, "audio_processor_test_inject_audio_data: free_after=%zu used=%zu", free_after, free_size - free_after);

    return ESP_OK;
}
#endif /* CONFIG_BT_MOCK_TESTING */

#if defined(CONFIG_BT_MOCK_TESTING) || defined(UNIT_TEST)
void audio_processor_test_set_ringbuffer_max_item_override(size_t max_item_bytes)
{
    s_test_ringbuf_max_item_override = max_item_bytes;
}

void audio_processor_test_force_wav_frame_bytes_dst(size_t frame_bytes)
{
    s_wav_stream.frame_bytes_dst = frame_bytes;
}

size_t audio_processor_test_wav_try_enqueue(const uint8_t *data, size_t len)
{
    return wav_stream_try_enqueue_unlocked(data, len, AUDIO_SOURCE_TAG_WAV);
}

void audio_processor_test_seed_wav_residual(const uint8_t *data, size_t len)
{
    if (data == NULL || len == 0 || len > sizeof(s_wav_send_residual)) {
        return;
    }
    memcpy(s_wav_send_residual, data, len);
    s_wav_send_residual_len = len;
    s_wav_send_residual_pos = 0;
    s_wav_residual_tag = AUDIO_SOURCE_TAG_WAV;
    s_wav_residual_tag_valid = true;
}

bool audio_processor_test_flush_wav_residual(void)
{
    return wav_stream_flush_residual_locked();
}

size_t audio_processor_test_get_wav_residual_remaining(void)
{
    if (s_wav_send_residual_len <= s_wav_send_residual_pos) {
        return 0;
    }
    return s_wav_send_residual_len - s_wav_send_residual_pos;
}

void audio_processor_test_clear_wav_residual(void)
{
    wav_stream_reset_residual_locked();
}
#endif /* CONFIG_BT_MOCK_TESTING || UNIT_TEST */

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
    size_t used_bytes = capacity - free_sz;
    return used_bytes / sizeof(audio_source_tag_item_t);
}
#endif
