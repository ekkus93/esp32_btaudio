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

        /* Prefill synth until the queue is full or we hit the deadline to
         * maximize headroom before the next SBC read. */
        const TickType_t start_ticks = xTaskGetTickCount();
        const TickType_t max_ticks = pdMS_TO_TICKS(30);
        const size_t target_water = (AUDIO_CHUNK_POOL_BLOCKS * 80U) / 100U; /* ~80% */
        uint8_t synth_buf[AUDIO_CHUNK_BLOCK_BYTES];

        while (audio_descriptor_used() < target_water) {
            TickType_t elapsed = xTaskGetTickCount() - start_ticks;
            if (elapsed >= max_ticks) {
                break;
            }
            size_t gen = synth_manager_generate_audio(synth_buf,
                                                     sizeof(synth_buf),
                                                     &s_audio_config,
                                                     &s_force_synth,
                                                     &s_beep_lock);
            if (gen == 0) {
                break;
            }
            if (!audio_chunk_enqueue_bytes(synth_buf, gen, AUDIO_SOURCE_TAG_SYNTH)) {
                break; /* queue full or failed; stop prefill */
            }
        }

        /* Allow BT TX queue to drain a bit before the next SBC pull. */
        vTaskDelay(pdMS_TO_TICKS(5));
        s_trace_next_read_call = true; /* log next read summary once */
    }

    ESP_LOGI(TAG, "audio_processor_beep: generation completed");  // NOLINT(bugprone-branch-clone)
    printf("DIAG-BEEP-DONE\n");
}

static void drain_beep_buffer(void)
{
    beep_manager_stop();
}

/* Snapshot queue content when beep prep or enqueue fails to identify which
 * producers are occupying the descriptors. Logged only on failure paths. */
static void log_queue_snapshot_on_beep_failure(const char *reason)
{
    audio_chunk_t snap[AUDIO_CHUNK_POOL_BLOCKS];
    size_t captured = 0;
    esp_err_t sret = audio_descriptor_snapshot(snap, AUDIO_CHUNK_POOL_BLOCKS, &captured);
    if (sret != ESP_OK) {
        ESP_LOGW(TAG,  // NOLINT(bugprone-branch-clone)
                 "audio_processor_beep: snapshot failed reason=%s err=%s used=%u",
                 reason,
                 esp_err_to_name(sret),
                 (unsigned)audio_descriptor_used());
        return;
    }

    size_t wav = 0;
    size_t capture = 0;
    size_t synth = 0;
    size_t beep = 0;
    size_t invalid = 0;
    size_t other = 0;
    for (size_t i = 0; i < captured; ++i) {
        switch (snap[i].tag) {
        case AUDIO_SOURCE_TAG_WAV:
            ++wav;
            break;
        case AUDIO_SOURCE_TAG_CAPTURE:
            ++capture;
            break;
        case AUDIO_SOURCE_TAG_SYNTH:
            ++synth;
            break;
        case AUDIO_SOURCE_TAG_BEEP:
            ++beep;
            break;
        case AUDIO_SOURCE_TAG_INVALID:
            ++invalid;
            break;
        default:
            ++other;
            break;
        }
    }

    ESP_LOGW(TAG,  // NOLINT(bugprone-branch-clone)
             "audio_processor_beep: queue snapshot reason=%s used=%u captured=%u tags"
             " {wav=%u cap=%u synth=%u beep=%u invalid=%u other=%u}",
             reason,
             (unsigned)audio_descriptor_used(),
             (unsigned)captured,
             (unsigned)wav,
             (unsigned)capture,
             (unsigned)synth,
             (unsigned)beep,
             (unsigned)invalid,
             (unsigned)other);
}

/* Ensure the audio queue is actually empty before enqueueing a beep. The
 * normal drain clears descriptors, but guard against any immediate refills
 * by waiting until the used count drops to zero or we hit the deadline. */
static bool wait_for_queue_empty(TickType_t max_wait_ticks)
{
    const TickType_t start = xTaskGetTickCount();
    while (audio_descriptor_used() > 0) {
        audio_chunk_clear();
        if ((xTaskGetTickCount() - start) >= max_wait_ticks) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }

    return true;
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

    /* Do not allow beep while WAV/PLAY is active. PLAY owns its path and must
     * not be interrupted by BEEP. */
    if (play_manager_is_active()) {
        ESP_LOGW(TAG, "audio_processor_beep: busy (play active)");  // NOLINT(bugprone-branch-clone)
        return ESP_ERR_INVALID_STATE;
    }

    if (wav_playback_is_active()) {
        ESP_LOGW(TAG, "audio_processor_beep: busy (WAV active)");  // NOLINT(bugprone-branch-clone)
        return ESP_ERR_INVALID_STATE;
    }

    /* Allow BEEP even when the I2S manager is running; any stale capture
     * content will be drained when the beep is enqueued. */

    bool beep_active = (beep_manager_get_state() == BEEP_STATE_PLAYING);
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

    /* Producer pacing is handled inside beep_manager; we no longer reject
     * requests just because the queue is currently full. */
    /* Guarantee a clean slate for the tone: block other producers, clear
     * queued audio, and wait until the queue is empty before enqueueing. */
    audio_queue_beep_exclusive_begin();
    s_force_synth = false;
    s_keepalive_armed = false;
    (void)audio_processor_drain_audio_queue();
    bool queue_empty = wait_for_queue_empty(pdMS_TO_TICKS(200));
    if (!queue_empty && audio_descriptor_used() > 0) {
        log_queue_snapshot_on_beep_failure("pre_beep_queue_not_empty");
        audio_queue_beep_exclusive_end();
        ESP_LOGW(TAG, "audio_processor_beep: queue not empty after wait (used=%u)", (unsigned)audio_descriptor_used());  // NOLINT(bugprone-branch-clone)
        return ESP_ERR_TIMEOUT;
    }

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

    audio_processor_flush_priority_queues("beep");
    beep_manager_set_done_callback(audio_processor_beep_done_cb, NULL);
    size_t bytes_enqueued = 0;
    esp_err_t ret = beep_manager_play_with_bytes(&req, &s_audio_config, &bytes_enqueued);
    if (ret != ESP_OK || bytes_enqueued == 0) {
        log_queue_snapshot_on_beep_failure("beep_play_failed");
        audio_queue_beep_exclusive_end();
        portENTER_CRITICAL(&s_beep_lock);
        s_beep_remaining_bytes = 0;
        portEXIT_CRITICAL(&s_beep_lock);
        return (ret == ESP_OK) ? ESP_ERR_NO_MEM : ret;
    }

    audio_queue_beep_exclusive_end();

    portENTER_CRITICAL(&s_beep_lock);
    s_beep_remaining_bytes = bytes_enqueued;
    portEXIT_CRITICAL(&s_beep_lock);

#ifdef UNIT_TEST
    s_last_beep_duration_ms = duration_ms;
    s_last_beep_freq_hz = freq_hz;
#endif

    ESP_LOGI(TAG, "audio_processor_beep: queued duration_ms=%u freq=%.2f bytes=%zu", (unsigned)duration_ms, freq_hz, bytes_enqueued);  // NOLINT(bugprone-branch-clone)
    return ESP_OK;
}

esp_err_t audio_processor_beep(uint32_t duration_ms)
{
    return audio_processor_beep_tone(duration_ms, 1000.0);
}

bool audio_processor_is_beep_active(void)
{
    AUDIO_PROC_LOG_ONCE();  // NOLINT(bugprone-branch-clone)
    if (s_beep_remaining_bytes == 0) {
        return false;
    }

    /* If the manager reports stopped but the counter remains nonzero, check
     * the queue snapshot for any lingering beep chunks. Clear stale busy
     * state so commands can proceed after a completed beep. */
    if (beep_manager_get_state() != BEEP_STATE_PLAYING) {
        audio_chunk_t snap[AUDIO_CHUNK_POOL_BLOCKS];
        size_t captured = 0;
        bool beep_present = false;
        esp_err_t sret = audio_descriptor_snapshot(snap, AUDIO_CHUNK_POOL_BLOCKS, &captured);
        if (sret == ESP_OK && captured > 0) {
            for (size_t i = 0; i < captured; ++i) {
                if (snap[i].tag == AUDIO_SOURCE_TAG_BEEP) {
                    beep_present = true;
                    break;
                }
            }
        }
        if (!beep_present) {
            s_beep_remaining_bytes = 0;
            ESP_LOGW(TAG, "audio_processor_beep: clearing stale busy flag (captured=%u)", (unsigned)captured);  // NOLINT(bugprone-branch-clone)
            printf("DIAG-BEEP-DONE\n");
            return false;
        }
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
    drain_beep_buffer();
    portENTER_CRITICAL(&s_beep_lock);
    s_beep_remaining_bytes = 0;
    s_beep_prefill_accum_bytes = 0;
    s_beep_prefill_goal_bytes = 0;
    s_beep_restore_synth = false;
    portEXIT_CRITICAL(&s_beep_lock);
}