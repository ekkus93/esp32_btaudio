#include "audio_processor_internal.h"

bool wav_playback_is_active(void)
{
    return s_wav_playback_active;
}

void wav_playback_begin(void)
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
void wav_playback_add_pending(size_t bytes)
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

bool wav_playback_consume(size_t bytes)
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

void wav_playback_abort(const char *caller)
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

    audio_processor_beep_reset();

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

void wav_playback_complete_if_idle(void)
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

    audio_processor_beep_reset();

    if (restored) {
        ESP_LOGI(TAG, "audio_processor: WAV playback completed (restored synth=%s)", synth_mode ? "ENABLED" : "DISABLED");
    }
    ESP_LOGI(TAG, "audio_processor: wav_playback_complete_if_idle -> s_force_synth=%s s_beep_remaining=%zu",
             s_force_synth ? "ENABLED" : "DISABLED",
             s_beep_remaining_bytes);

    (void)restart_needed;
}

void wav_refill_from_manager(void)
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
        s_wav_resume_pipeline = false;
    }
}

#ifdef CONFIG_BT_MOCK_TESTING
size_t audio_processor_test_wav_pending_bytes(void)
{
    AUDIO_PROC_LOG_ONCE();
    size_t pending = 0;
    portENTER_CRITICAL(&s_wav_lock);
    pending = s_wav_pending_bytes;
    portEXIT_CRITICAL(&s_wav_lock);
    return pending;
}
#endif