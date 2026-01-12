#include "audio_processor_internal.h"

static void audio_processor_beep_done_cb(void *ctx)
{
    (void)ctx;
    portENTER_CRITICAL(&s_beep_lock);
    s_beep_remaining_bytes = 0;
    portEXIT_CRITICAL(&s_beep_lock);
    ESP_LOGI(TAG, "audio_processor_beep: completed");
    printf("DIAG-BEEP-DONE\n");
}

static void drain_beep_buffer(void)
{
    beep_manager_stop();
}

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

    bool beep_active = (beep_manager_get_state() == BEEP_STATE_PLAYING);
    portENTER_CRITICAL(&s_beep_lock);
    if (!beep_active && s_beep_remaining_bytes > 0) {
        beep_active = true;
    }
    portEXIT_CRITICAL(&s_beep_lock);
    if (beep_active) {
        ESP_LOGW(TAG, "audio_processor_beep: busy (beep active)");
        return ESP_ERR_INVALID_STATE;
    }

    if (duration_ms == 0) {
        duration_ms = 50;
    } else if (duration_ms > 20000U) {
        duration_ms = 20000U;
    }

    beep_request_t req = {
        .duration_ms = duration_ms,
        .freq_hz = (freq_hz > 0.0) ? freq_hz : 1000.0,
        .amplitude = 0,
    };

    audio_processor_flush_priority_queues("beep");
    beep_manager_set_done_callback(audio_processor_beep_done_cb, NULL);
    size_t bytes_enqueued = 0;
    esp_err_t ret = beep_manager_play_with_bytes(&req, &s_audio_config, &bytes_enqueued);
    if (ret != ESP_OK || bytes_enqueued == 0) {
        portENTER_CRITICAL(&s_beep_lock);
        s_beep_remaining_bytes = 0;
        portEXIT_CRITICAL(&s_beep_lock);
        return (ret == ESP_OK) ? ESP_ERR_NO_MEM : ret;
    }

    portENTER_CRITICAL(&s_beep_lock);
    s_beep_remaining_bytes = bytes_enqueued;
    portEXIT_CRITICAL(&s_beep_lock);

#ifdef UNIT_TEST
    s_last_beep_duration_ms = duration_ms;
    s_last_beep_freq_hz = freq_hz;
#endif

    ESP_LOGI(TAG, "audio_processor_beep: queued duration_ms=%u freq=%.2f bytes=%zu", (unsigned)duration_ms, freq_hz, bytes_enqueued);
    return ESP_OK;
}

esp_err_t audio_processor_beep(uint32_t duration_ms)
{
    return audio_processor_beep_tone(duration_ms, 1000.0);
}

bool audio_processor_is_beep_active(void)
{
    AUDIO_PROC_LOG_ONCE();
    return s_beep_remaining_bytes > 0;
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
    AUDIO_PROC_LOG_ONCE();
    s_dump_next_beep_diag = true;
    ESP_LOGI(TAG, "audio_processor: next-beep diagnostic enabled");
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
    drain_beep_buffer();
    portENTER_CRITICAL(&s_beep_lock);
    s_beep_remaining_bytes = 0;
    s_beep_prefill_accum_bytes = 0;
    s_beep_prefill_goal_bytes = 0;
    s_beep_restore_synth = false;
    portEXIT_CRITICAL(&s_beep_lock);
}