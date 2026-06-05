#include "audio_processor_internal.h"
#include <stdint.h>
#include <limits.h>
#include <inttypes.h>

static void audio_processor_beep_done_cb(void *ctx)
{
    (void)ctx;
    /* Beep generation finished. Clear remaining bytes so commands unblock and
     * emit a completion diagnostic immediately. */
    portENTER_CRITICAL(&s_beep_lock);
    s_beep_remaining_bytes = 0;
    portEXIT_CRITICAL(&s_beep_lock);

    /* F1.3.1: Restore exactly what was active before beep (replaces old keepalive logic).
     *
     * Old behavior: Force SYNTH ON when A2DP connected, regardless of prior state.
     * This trampled user intent - if they had I2S active, it stayed off after beep.
     *
     * New behavior: Restore the source that was preempted by beep.
     * - If SYNTH was active → restore SYNTH
     * - If I2S was active → restart I2S
     * - If neither was active → leave silent (correct behavior for idle state)
     */
    bool restore_synth = false;
    bool restore_i2s = false;

    portENTER_CRITICAL(&s_beep_lock);
    restore_synth = s_beep_restore_synth;
    restore_i2s = s_beep_restore_i2s;
    s_beep_restore_synth = false;  /* F1.3.2: Clear restore flags */
    s_beep_restore_i2s = false;
    portEXIT_CRITICAL(&s_beep_lock);

    if (restore_synth) {
        ESP_LOGI(TAG, "audio_processor_beep: restoring SYNTH source");  // NOLINT(bugprone-branch-clone)
        s_force_synth = true;
        s_trace_next_read_call = true; /* log next read summary once */
    } else if (restore_i2s && s_is_running) {
        ESP_LOGI(TAG, "audio_processor_beep: restoring I2S source");  // NOLINT(bugprone-branch-clone)
        i2s_manager_start();
        s_trace_next_read_call = true;
    }

    ESP_LOGI(TAG, "audio_processor_beep: generation completed");  // NOLINT(bugprone-branch-clone)
    printf("DIAG-BEEP-DONE\n");
}

esp_err_t audio_processor_beep_tone(uint32_t duration_ms, double freq_hz)
{
    AUDIO_PROC_LOG_ONCE();  // NOLINT(bugprone-branch-clone)
    if (!s_is_initialized) {
        ESP_LOGW(TAG, "audio_processor_beep: not initialized");  // NOLINT(bugprone-branch-clone)
        return ESP_ERR_INVALID_STATE;
    }

    /* F1.2.2: Snapshot active source state for restoration after beep */
    bool was_synth = s_force_synth;
    bool was_i2s = i2s_manager_is_running();

    /* F1.2.3: Enforce mutual exclusion invariant - I2S and SYNTH should never both be active.
     * If both are active, treat as protocol violation and prioritize SYNTH (safer). */
    if (was_synth && was_i2s) {
        ESP_LOGW(TAG, "audio_processor_beep: invariant violation - both SYNTH and I2S active, forcing I2S off");  // NOLINT(bugprone-branch-clone)
        was_i2s = false;  /* Deterministic priority: SYNTH wins */
    }

    /* WAV playback removed (play_manager deleted) */

    /* Allow BEEP even when the I2S manager is running; any stale capture
     * content will be drained when the beep is enqueued. */

    bool beep_active = beep_overlay_is_active();
    portENTER_CRITICAL(&s_beep_lock);
    if (!beep_active && s_beep_remaining_bytes > 0) {
        beep_active = true;
    }
    portEXIT_CRITICAL(&s_beep_lock);
    if (beep_active) {
        ESP_LOGW(TAG, "audio_processor_beep: busy (beep active)");  // NOLINT(bugprone-branch-clone)
        return ESP_ERR_INVALID_STATE;
    }

    /* F1.2.4: Stop active source before playing beep (BEEP has priority) */
    if (was_i2s) {
        ESP_LOGI(TAG, "audio_processor_beep: preempting I2S source");  // NOLINT(bugprone-branch-clone)
        i2s_manager_stop();
    }
    if (was_synth) {
        ESP_LOGI(TAG, "audio_processor_beep: preempting SYNTH source");  // NOLINT(bugprone-branch-clone)
        s_force_synth = false;
    }

    /* F1.2.5: Set restore flags for post-beep recovery */
    portENTER_CRITICAL(&s_beep_lock);
    s_beep_restore_synth = was_synth;
    s_beep_restore_i2s = was_i2s;
    portEXIT_CRITICAL(&s_beep_lock);

    /* F1.4.2: Set drain flag to discard buffered audio from previous source.
     * WHY: Without draining, old audio from I2S/SYNTH plays before beep,
     *      causing "late beep" perception. Ring buffer can hold ~32KB (several
     *      hundred milliseconds of audio at 44.1kHz stereo 16-bit).
     * HOW: audio_processor_read() checks this flag, drains ring, returns silence.
     * SAFETY: Set after stopping sources, before starting beep generation. */
    s_drop_ring_audio = true;

    if (duration_ms == 0) {
        duration_ms = 50;
    } else if (duration_ms > 20000U) {
        duration_ms = 20000U;
    }

    /* F1.2.6: Removed state-breaking lines - keepalive managed independently
     * Old code: s_force_synth = false; s_keepalive_armed = false;
     * Why removed: These lines trampled user-intended source state and conflicted
     * with restoration logic. Source preemption is now explicit (above). */

    uint32_t channels = (s_audio_config.channels == AUDIO_CHANNEL_MONO) ? 1U : 2U;
    uint32_t sample_bytes = (s_audio_config.bit_depth == AUDIO_BIT_DEPTH_32) ? 4U : 2U;
    uint64_t bytes_per_ms = ((uint64_t)s_audio_config.sample_rate * (uint64_t)channels * (uint64_t)sample_bytes) / 1000ULL;
    if (bytes_per_ms == 0) {
        ESP_LOGW(TAG, "audio_processor_beep: invalid format (bytes_per_ms=0)");  // NOLINT(bugprone-branch-clone)
        return ESP_ERR_NO_MEM;
    }

    beep_request_t req = {
        .duration_ms = duration_ms,
        .freq_hz = (freq_hz > 0.0) ? freq_hz : 1000.0,
        .amplitude = 0,
    };

    beep_manager_set_done_callback(audio_processor_beep_done_cb, NULL);
    esp_err_t ret = beep_manager_play(&req, &s_audio_config);
    if (ret != ESP_OK) {
        portENTER_CRITICAL(&s_beep_lock);
        s_beep_remaining_bytes = 0;
        portEXIT_CRITICAL(&s_beep_lock);
        return ret;
    }

    /* BUG-8: guard against overflow before narrowing to size_t (32-bit on ESP32).
     * At 96kHz/stereo/32-bit the product is ~768 bytes/ms; 20000ms max → 15.36MB,
     * which fits in uint64_t but would silently truncate on a 32-bit size_t. */
    uint64_t total_bytes = (uint64_t)duration_ms * bytes_per_ms;
    if (total_bytes > (uint64_t)SIZE_MAX) {
        ESP_LOGE(TAG, "audio_processor_beep: byte count overflows size_t (%" PRIu64 " bytes)", total_bytes);
        return ESP_ERR_INVALID_ARG;
    }
    portENTER_CRITICAL(&s_beep_lock);
    s_beep_remaining_bytes = (size_t)total_bytes;
    portEXIT_CRITICAL(&s_beep_lock);

#ifdef UNIT_TEST
    s_last_beep_duration_ms = duration_ms;
    s_last_beep_freq_hz = freq_hz;
#endif

    ESP_LOGI(TAG, "audio_processor_beep: started duration_ms=%u freq=%.2f bytes=%zu", (unsigned)duration_ms, freq_hz, (size_t)((uint64_t)duration_ms * bytes_per_ms));  // NOLINT(bugprone-branch-clone)
    return ESP_OK;
}

esp_err_t audio_processor_beep(uint32_t duration_ms)
{
    return audio_processor_beep_tone(duration_ms, 1000.0);
}

bool audio_processor_is_beep_active(void)
{
    AUDIO_PROC_LOG_ONCE();  // NOLINT(bugprone-branch-clone)
    if (s_beep_remaining_bytes == 0 && !beep_overlay_is_active()) {
        return false;
    }
    return true;
}

#ifdef CONFIG_BT_MOCK_TESTING
size_t audio_processor_test_get_beep_remaining_bytes(void)
{
    AUDIO_PROC_LOG_ONCE();
    return s_beep_remaining_bytes;
}
#endif

void audio_processor_enable_next_beep_diag(void)
{
    AUDIO_PROC_LOG_ONCE();  // NOLINT(bugprone-branch-clone)
    s_dump_next_beep_diag = true;
    s_trace_read_until_beep_done = true;
    s_trace_next_read_call = true;
    ESP_LOGI(TAG, "audio_processor: next-beep diagnostic enabled");  // NOLINT(bugprone-branch-clone)
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

void audio_processor_beep_reset(void)
{
    beep_manager_stop();
    portENTER_CRITICAL(&s_beep_lock);
    s_beep_remaining_bytes = 0;
    s_beep_prefill_accum_bytes = 0;
    s_beep_prefill_goal_bytes = 0;
    s_beep_restore_synth = false;
    s_beep_restore_i2s = false;  /* F1.2: Clear I2S restore flag */
    portEXIT_CRITICAL(&s_beep_lock);
}
