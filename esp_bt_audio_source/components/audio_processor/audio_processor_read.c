#include "audio_processor_internal.h"
#include "platform_timing.h"
#include "util_safe.h"

/* ── A2DP pull-rate tracking (UARTAUDIO diagnostics) ─────────────────────
 * audio_processor_read() is the A2DP data callback's only data path, so
 * its request cadence measures how fast Bluedroid actually pulls audio.
 * Burst-windowed: a >1 s gap between reads starts a fresh window so the
 * reported rate always describes the current stream.
 * Guarded by s_audio_stats_lock (same contexts as the other read stats). */

#define READ_RATE_GAP_MS   1000U  /* silence longer than this ends a burst */
#define READ_RATE_MIN_WINDOW_MS 100U /* don't report rates from tiny samples */

static uint32_t s_rr_first_ms;
static uint32_t s_rr_last_ms;
static uint32_t s_rr_bytes;
static uint32_t s_rr_calls;

#ifdef UNIT_TEST
static uint32_t s_rr_test_now;
static bool s_rr_test_now_set;
#endif

static uint32_t read_rate_now(void)
{
#ifdef UNIT_TEST
    if (s_rr_test_now_set) {
        return s_rr_test_now;
    }
#endif
    return (uint32_t)platform_get_time_ms();
}

static void read_rate_note_locked(uint32_t now_ms, size_t req_bytes)
{
    if (s_rr_calls == 0U || (now_ms - s_rr_last_ms) > READ_RATE_GAP_MS) {
        s_rr_first_ms = now_ms;
        s_rr_bytes = 0;
        s_rr_calls = 0;
    }
    s_rr_last_ms = now_ms;
    s_rr_bytes += (uint32_t)req_bytes;
    s_rr_calls++;
}

esp_err_t audio_processor_get_read_rate(audio_read_rate_t* out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    uint32_t now_ms = read_rate_now();
    portENTER_CRITICAL(&s_audio_stats_lock);
    /* a burst that stopped is history, not a current rate — report idle
     * instead of freezing the last value forever */
    if (s_rr_calls > 0U && (now_ms - s_rr_last_ms) > READ_RATE_GAP_MS) {
        s_rr_first_ms = 0;
        s_rr_last_ms = 0;
        s_rr_bytes = 0;
        s_rr_calls = 0;
    }
    out->calls = s_rr_calls;
    out->bytes_requested = s_rr_bytes;
    out->window_ms = (s_rr_calls > 0U) ? (s_rr_last_ms - s_rr_first_ms) : 0U;
    portEXIT_CRITICAL(&s_audio_stats_lock);

    out->rate_bps = 0;
    if (out->window_ms >= READ_RATE_MIN_WINDOW_MS) {
        out->rate_bps = (uint32_t)(((uint64_t)out->bytes_requested * 1000ULL) /
                                   (uint64_t)out->window_ms);
    }
    return ESP_OK;
}

#ifdef UNIT_TEST
void audio_processor_test_read_rate_note(uint32_t now_ms, size_t req_bytes)
{
    s_rr_test_now = now_ms;
    s_rr_test_now_set = true;
    portENTER_CRITICAL(&s_audio_stats_lock);
    read_rate_note_locked(now_ms, req_bytes);
    portEXIT_CRITICAL(&s_audio_stats_lock);
}

void audio_processor_test_read_rate_set_now(uint32_t now_ms)
{
    s_rr_test_now = now_ms;
    s_rr_test_now_set = true;
}

void audio_processor_test_read_rate_reset(void)
{
    s_rr_test_now_set = false;
    portENTER_CRITICAL(&s_audio_stats_lock);
    s_rr_first_ms = 0;
    s_rr_last_ms = 0;
    s_rr_bytes = 0;
    s_rr_calls = 0;
    portEXIT_CRITICAL(&s_audio_stats_lock);
}
#endif

static void log_read_summary(const char *phase, size_t requested, size_t produced)
{
    const char *tag = (phase != NULL) ? phase : "done";
    size_t audio_residual = 0;
    if (s_audio_rb_residual_len > s_audio_rb_residual_pos) {
        audio_residual = s_audio_rb_residual_len - s_audio_rb_residual_pos;
    }
    size_t free_bytes = s_audio_ring ? audio_rb_available_to_write(s_audio_ring) : 0;

    if (s_trace_read_until_beep_done) {
        /* Emit an explicit trace line so monitor logs clearly show reader activity during a beep. */
        printf("TRACE-READ: phase=%s req=%zu produced=%zu beep_remaining=%zu ring_free=%zu underruns=%u overruns=%u\n",
               tag,
               requested,
               produced,
               s_beep_remaining_bytes,
               free_bytes,
               (unsigned)s_audio_stats.buffer_underruns,
               (unsigned)s_audio_stats.buffer_overruns);

        if (s_beep_remaining_bytes == 0) {
            s_trace_read_until_beep_done = false;
        }
    }

    if (esp_log_level_get(TAG) < ESP_LOG_INFO) {
        return;
    }

    if (AUDIO_DIAG_ENABLED()) {
        ESP_LOGI(TAG, "I2S-OP-STATS: ops=%u bytes=%u timeouts=%u",  // NOLINT(bugprone-branch-clone)
                 (unsigned)s_i2s_read_ops,
                 (unsigned)s_i2s_total_read_bytes,
                 (unsigned)s_i2s_timeout_count);
    }

    ESP_LOGI(TAG,  // NOLINT(bugprone-branch-clone)
             "audio_processor_read[%s]: req=%zu produced=%zu mute=%d beep_remaining=%zu audio_residual=%zu ring_free=%zu underruns=%u overruns=%u",
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

    /* F1.4.3: Drain ring buffer if beep is starting (drop old audio from previous source)
     * WHY: Ensures beep plays immediately without old I2S/SYNTH audio first.
     * HOW: Discard all buffered audio, return silence this callback.
     * SAFETY: Bounded iteration (max ring capacity), flag cleared after drain. */
    if (s_drop_ring_audio && s_audio_ring != NULL) {
        size_t drained = 0;
        uint8_t drain_buf[512];
        size_t ring_capacity = audio_rb_capacity(s_audio_ring);
        size_t max_iterations = (ring_capacity / sizeof(drain_buf)) + 1;  /* Prevent infinite loop */
        size_t iterations = 0;

        while (audio_rb_available_to_read(s_audio_ring) > 0 && iterations < max_iterations) {
            size_t chunk = audio_rb_read(s_audio_ring, drain_buf, sizeof(drain_buf));
            drained += chunk;
            iterations++;
        }
        
        s_drop_ring_audio = false;  /* Clear flag after drain */
        
        ESP_LOGI(TAG, "audio_processor_read: drained %zu bytes from ring buffer (beep starting)", drained);  // NOLINT(bugprone-branch-clone)
        
        /* Return silence for this callback - beep overlay will mix over it */
        util_safe_memset(buffer, size, 0, size);
        portENTER_CRITICAL(&s_audio_stats_lock);
        read_rate_note_locked((uint32_t)platform_get_time_ms(), size);
        portEXIT_CRITICAL(&s_audio_stats_lock);
        *bytes_read = size;
        log_read_summary("drain", size, size);
        return ESP_OK;
    }

    /* Ring buffer read (CODE_REVIEW6 Phase 3.5)
     * WHY: Ring buffer architecture replaces queue + residual buffer
     * HOW: Direct non-blocking read from ring, zero-fill on underrun
     * CORRECTNESS: Never blocks (A2DP callback safe), tracks underruns */
    
    size_t read = audio_rb_read(s_audio_ring, buffer, size);
    
    /* Update statistics (CODE_REVIEW 2602101453, P1.2.4)
     * Protected by spinlock - audio_processor_read() called from ISR-like context */
    portENTER_CRITICAL(&s_audio_stats_lock);
    read_rate_note_locked((uint32_t)platform_get_time_ms(), size);
    if (read < size) {
        /* Underrun - zero-fill remainder
         * WHY: Prevent glitches from stale data
         * HOW: util_safe_memset remaining bytes to silence
         * CORRECTNESS: Only fills what wasn't read from ring */
        s_audio_stats.buffer_underruns++;
        s_audio_stats.underrun_bytes += (size - read);
    }
    s_audio_stats.bytes_read += size;
    
    int bytes_per_sample = audio_bytes_per_sample(s_audio_config.bit_depth);
    if (bytes_per_sample <= 0) {
        bytes_per_sample = 2;
    }
    size_t frame_bytes = (size_t)bytes_per_sample * (size_t)((s_audio_config.channels == AUDIO_CHANNEL_MONO) ? 1U : 2U);
    if (frame_bytes > 0) {
        s_audio_stats.samples_processed += (uint32_t)(size / frame_bytes);
    }    portEXIT_CRITICAL(&s_audio_stats_lock);
    
    /* Zero-fill underrun bytes outside critical section to minimize interrupt latency */
    if (read < size) {
        util_safe_memset(buffer + read, size - read, 0, size - read);
    }

    /* Apply master volume to the final mixed PCM (I2S/synth/beep already combined
     * in the ring upstream). This is the single output point for the A2DP data
     * callback, so scaling here makes the VOLUME command actually work. Fixes the
     * long-standing bug where apply_volume() existed but was never called.
     *
     * Use the s16 path explicitly: the A2DP/SBC output buffer is ALWAYS 16-bit,
     * whereas apply_volume() dispatches on s_audio_config.bit_depth (which is the
     * I2S input depth, e.g. 32 for 16-in-32 slots). Using the 32-bit branch on a
     * 16-bit buffer scrambles the samples -> loud static. volume>=100 no-ops. */
    apply_volume_s16((int16_t*)buffer, size / sizeof(int16_t), s_volume_gain);

    *bytes_read = size;  /* Always return full size (zero-filled if needed) */
    
    /* Trace/log if enabled */
    if (s_trace_read_until_beep_done || esp_log_level_get(TAG) >= ESP_LOG_INFO) {
        size_t ring_used = audio_rb_capacity(s_audio_ring) - audio_rb_available_to_write(s_audio_ring);
        ESP_LOGI(TAG, "audio_processor_read: req=%zu read=%zu underrun=%s ring_used=%zu",
                 size, read, (read < size) ? "yes" : "no", ring_used);
    }
    
    return ESP_OK;
}

void audio_processor_log_read_summary(const char *phase, size_t requested, size_t produced)
{
    log_read_summary(phase, requested, produced);
}
