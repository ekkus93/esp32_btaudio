#include "audio_processor_internal.h"

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

static void log_read_summary(const char *phase, size_t requested, size_t produced)
{
    const char *tag = (phase != NULL) ? phase : "done";
    size_t audio_residual = 0;
    if (s_audio_rb_residual_len > s_audio_rb_residual_pos) {
        audio_residual = s_audio_rb_residual_len - s_audio_rb_residual_pos;
    }
    size_t free_bytes = audio_processor_queue_free_bytes();

    if (s_trace_read_until_beep_done) {
        /* Emit an explicit trace line so monitor logs clearly show reader activity during a beep. */
        printf("TRACE-READ: phase=%s req=%zu produced=%zu beep_remaining=%zu queue_free=%zu underruns=%u overruns=%u\n",
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
        ESP_LOGI(TAG, "I2S-OP-STATS: ops=%u bytes=%u timeouts=%u",
                 (unsigned)s_i2s_read_ops,
                 (unsigned)s_i2s_total_read_bytes,
                 (unsigned)s_i2s_timeout_count);
    }

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

esp_err_t audio_processor_acquire_chunk_internal(audio_chunk_t *out_chunk, TickType_t wait_ticks)
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
        bool was_active = (s_beep_remaining_bytes > 0);
        if (s_beep_remaining_bytes > to_copy) {
            s_beep_remaining_bytes -= to_copy;
        } else {
            s_beep_remaining_bytes = 0;
        }
        if (was_active && s_beep_remaining_bytes == 0) {
            ESP_LOGI(TAG, "audio_processor_beep: completed (drained)");
            printf("DIAG-BEEP-DONE\n");
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

    if (s_trace_read_until_beep_done) {
        size_t queue_used = audio_descriptor_used();
        size_t queue_free = audio_processor_queue_free_bytes();
        printf("TRACE-READ-ENTRY: request_size=%zu queue_used=%zu queue_free=%zu beep_remaining=%zu\n",
               size,
               queue_used,
               queue_free,
               s_beep_remaining_bytes);
    }

    if (s_beep_remaining_bytes > 0 && audio_descriptor_used() == 0) {
        /* Failsafe: if the queue is empty but the counter never drained, clear
         * the beep state and emit completion so commands unblock. */
        s_beep_remaining_bytes = 0;
        ESP_LOGI(TAG, "audio_processor_beep: completed (queue empty)");
        printf("DIAG-BEEP-DONE\n");
    }

    /* If a WAV is pending but nothing is queued yet, try to prefill before the
     * BT stack pulls and underruns. */
    if (play_manager_pending_bytes() > 0 && audio_descriptor_used() == 0) {
        wav_refill_from_manager();
    }

    if (!bt_manager_is_a2dp_connected()) {
        bool wav_active = play_manager_is_active() || play_manager_pending_bytes() > 0 || s_wav_playback_active;
        s_force_synth = false;
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

    /* 
     * Check if audio sources are inactive AND residual buffer is empty.
     * Must flush residual buffer before early-return to prevent tail truncation.
     * (CODE_REVIEW4 Task 1.3 - Option B)
     */
    size_t residual_remaining = (s_audio_rb_residual_len > s_audio_rb_residual_pos) 
                               ? (s_audio_rb_residual_len - s_audio_rb_residual_pos) 
                               : 0;
    
    if (!play_manager_is_active() && s_beep_remaining_bytes == 0 && !s_force_synth && !s_wav_playback_active && residual_remaining == 0) {
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
    }

    size_t bytes_written = 0;

    bytes_written += residual_copy(buffer, size, s_audio_rb_residual, &s_audio_rb_residual_pos, &s_audio_rb_residual_len);

    while (bytes_written < size) {
        audio_chunk_t chunk = {0};
        esp_err_t acq = audio_processor_acquire_chunk_internal(&chunk, pdMS_TO_TICKS(25));
        if (acq != ESP_OK) {
            /* Keepalive synth: when armed and queue is empty, generate audio
             * directly into the caller's buffer until the request is filled. */
            if (play_manager_pending_bytes() > 0 && audio_descriptor_used() == 0) {
                wav_refill_from_manager();
                acq = audio_processor_acquire_chunk_internal(&chunk, pdMS_TO_TICKS(10));
                if (acq == ESP_OK) {
                    /* fall through to normal copy path */
                }
            }
            if (s_keepalive_armed && bytes_written < size) {
                s_force_synth = true;
                size_t remaining = size - bytes_written;
                size_t gen = synth_manager_generate_audio(buffer + bytes_written,
                                                         remaining,
                                                         &s_audio_config,
                                                         &s_force_synth,
                                                         &s_beep_lock);
                if (gen > remaining) {
                    gen = remaining;
                }
                bytes_written += gen;
                if (bytes_written < size) {
                    /* Loop to top and try to fill more (either synth or queue). */
                    continue;
                }
            }
            break;
        }

        /* Enforce BEEP/PLAY exclusivity: if a beep chunk slips through while
         * WAV playback is active, discard it and clear any lingering beep
         * state so PLAY output is not contaminated. */
        if ((chunk.tag == AUDIO_SOURCE_TAG_BEEP) && wav_playback_is_active()) {
            audio_chunk_release_block(chunk.data);
            portENTER_CRITICAL(&s_beep_lock);
            s_beep_remaining_bytes = 0;
            portEXIT_CRITICAL(&s_beep_lock);
            ESP_LOGW(TAG, "audio_processor_read: dropped beep chunk during WAV");
            continue;
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

    /* If still short and keepalive armed, synthesize the remainder directly. */
    if (bytes_written < size && s_keepalive_armed) {
        s_force_synth = true;
        size_t remaining = size - bytes_written;
        size_t gen = synth_manager_generate_audio(buffer + bytes_written,
                                                 remaining,
                                                 &s_audio_config,
                                                 &s_force_synth,
                                                 &s_beep_lock);
        if (gen > remaining) {
            gen = remaining;
        }
        bytes_written += gen;
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

void audio_processor_log_read_summary(const char *phase, size_t requested, size_t produced)
{
    log_read_summary(phase, requested, produced);
}