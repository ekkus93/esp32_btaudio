#include "audio_processor_internal.h"

static void audio_processor_beep_done_cb(void *ctx)
{
    (void)ctx;
    /* Beep generation finished. Clear remaining bytes so commands unblock and
     * emit a completion diagnostic immediately. */
    portENTER_CRITICAL(&s_beep_lock);
    s_beep_remaining_bytes = 0;
    portEXIT_CRITICAL(&s_beep_lock);

    /* Re-enable and prefill synth keepalive after a beep so the audio queue
     * stays fed and A2DP doesn't underrun after the tone ends. Only do this
     * when A2DP is connected to avoid injecting tone when idle. Prefill up
     * to a low watermark to bridge the handoff. */
    if (bt_manager_is_a2dp_connected()) {
        s_keepalive_armed = true;
        s_force_synth = true;
        s_trace_next_read_call = true; /* log next read summary once */
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

    if (i2s_manager_is_running()) {
        ESP_LOGW(TAG, "audio_processor_beep: busy (I2S active)");  // NOLINT(bugprone-branch-clone)
        return ESP_ERR_INVALID_STATE;
    }

    if (wav_playback_is_active()) {
        ESP_LOGW(TAG, "audio_processor_beep: busy (legacy WAV stub active)");  // NOLINT(bugprone-branch-clone)
        return ESP_ERR_INVALID_STATE;
    }

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

    /* Arm keepalive for post-beep synth if we are connected so the queue
     * stays fed after the tone drains. */
    if (bt_manager_is_a2dp_connected()) {
        s_keepalive_armed = true;
    }

    if (duration_ms == 0) {
        duration_ms = 50;
    } else if (duration_ms > 20000U) {
        duration_ms = 20000U;
    }

    s_force_synth = false;
    s_keepalive_armed = false;

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

    portENTER_CRITICAL(&s_beep_lock);
    s_beep_remaining_bytes = (size_t)((uint64_t)duration_ms * bytes_per_ms);
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
    portEXIT_CRITICAL(&s_beep_lock);
}
