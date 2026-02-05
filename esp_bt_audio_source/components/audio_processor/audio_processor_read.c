#include "audio_processor_internal.h"
#include "util_safe.h"

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

    /* Ring buffer read (CODE_REVIEW6 Phase 3.5)
     * WHY: Ring buffer architecture replaces queue + residual buffer
     * HOW: Direct non-blocking read from ring, zero-fill on underrun
     * CORRECTNESS: Never blocks (A2DP callback safe), tracks underruns */
    
    size_t read = audio_rb_read(s_audio_ring, buffer, size);
    
    if (read < size) {
        /* Underrun - zero-fill remainder
         * WHY: Prevent glitches from stale data
         * HOW: util_safe_memset remaining bytes to silence
         * CORRECTNESS: Only fills what wasn't read from ring */
        util_safe_memset(buffer + read, size - read, 0, size - read);
        s_audio_stats.buffer_underruns++;
        s_audio_stats.underrun_bytes += (size - read);
    }
    
    /* Update statistics */
    s_audio_stats.bytes_read += size;
    
    int bytes_per_sample = audio_bytes_per_sample(s_audio_config.bit_depth);
    if (bytes_per_sample <= 0) {
        bytes_per_sample = 2;
    }
    size_t frame_bytes = (size_t)bytes_per_sample * (size_t)((s_audio_config.channels == AUDIO_CHANNEL_MONO) ? 1U : 2U);
    if (frame_bytes > 0) {
        s_audio_stats.samples_processed += (uint32_t)(size / frame_bytes);
    }
    
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
