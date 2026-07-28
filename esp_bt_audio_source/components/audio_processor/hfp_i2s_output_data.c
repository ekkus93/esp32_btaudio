#include "hfp_i2s_output_internal.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <string.h>

#include "hfp_voice_convert.h"
#include "util_safe.h"

bool hfp_i2s_output_push_cvsd(const int16_t *samples_8k,
                               size_t sample_count,
                               uint32_t generation)
{
    atomic_fetch_add_explicit(&s_output.push_calls, 1U, memory_order_relaxed);
    if (samples_8k == NULL || sample_count == 0U ||
        sample_count > HFP_I2S_CVSD_MAX_INPUT_SAMPLES) {
        atomic_fetch_add_explicit(&s_output.invalid_pushes, 1U,
                                  memory_order_relaxed);
        atomic_fetch_add_explicit(&s_output.push_failures, 1U,
                                  memory_order_relaxed);
        return false;
    }
    if (!atomic_load_explicit(&s_output.accepting_pushes,
                              memory_order_acquire) ||
        generation != atomic_load_explicit(&s_output.generation,
                                           memory_order_acquire)) {
        atomic_fetch_add_explicit(&s_output.stale_pushes, 1U,
                                  memory_order_relaxed);
        atomic_fetch_add_explicit(&s_output.push_failures, 1U,
                                  memory_order_relaxed);
        return false;
    }

    atomic_fetch_add_explicit(&s_output.active_pushes, 1U, memory_order_acq_rel);
    if (!atomic_load_explicit(&s_output.accepting_pushes,
                              memory_order_acquire) ||
        generation != atomic_load_explicit(&s_output.generation,
                                           memory_order_acquire)) {
        atomic_fetch_sub_explicit(&s_output.active_pushes, 1U,
                                  memory_order_acq_rel);
        atomic_fetch_add_explicit(&s_output.stale_pushes, 1U,
                                  memory_order_relaxed);
        atomic_fetch_add_explicit(&s_output.push_failures, 1U,
                                  memory_order_relaxed);
        return false;
    }

    int16_t converted[HFP_I2S_CVSD_MAX_INPUT_SAMPLES *
                      HFP_I2S_CVSD_UPSAMPLE_FACTOR];
    size_t produced = hfp_cvsd_8k_to_16k(
        samples_8k, sample_count, converted,
        sizeof(converted) / sizeof(converted[0]));
    bool accepted =
        produced == sample_count * HFP_I2S_CVSD_UPSAMPLE_FACTOR &&
        hfp_pcm_ring_write_frame(&s_output.ring, converted,
                                 produced * sizeof(converted[0]), generation);
    if (!accepted) {
        atomic_fetch_add_explicit(&s_output.push_failures, 1U,
                                  memory_order_relaxed);
    }
    atomic_fetch_sub_explicit(&s_output.active_pushes, 1U, memory_order_acq_rel);
    return accepted;
}

esp_err_t hfp_i2s_output_writer_iteration(void)
{
    if (s_output.writer_pcm == NULL || s_output.channel == NULL ||
        s_output.config.writer_samples == 0U) {
        return ESP_ERR_INVALID_STATE;
    }
    size_t requested_bytes =
        s_output.config.writer_samples * sizeof(s_output.writer_pcm[0]);
    size_t read_bytes = hfp_pcm_ring_read(
        &s_output.ring, s_output.writer_pcm, requested_bytes,
        atomic_load_explicit(&s_output.generation, memory_order_acquire));
    if (read_bytes < requested_bytes) {
        memset((uint8_t *)s_output.writer_pcm + read_bytes, 0,
               requested_bytes - read_bytes);
        if (hfp_i2s_output_lock() == ESP_OK) {
            s_output.silence_intervals++;
            s_output.silence_samples +=
                (requested_bytes - read_bytes) / sizeof(int16_t);
            s_output.consecutive_underflows++;
            if (s_output.consecutive_underflows ==
                s_output.config.underflow_degraded_threshold) {
                s_output.degraded = true;
                s_output.degraded_events++;
                s_output.last_error = ESP_ERR_TIMEOUT;
                HFP_I2S_LOGW("sustained underflow: intervals=%" PRIu32,
                             s_output.consecutive_underflows);
            }
            (void)hfp_i2s_output_unlock(ESP_OK);
        }
    } else if (hfp_i2s_output_lock() == ESP_OK) {
        s_output.consecutive_underflows = 0U;
        s_output.degraded = false;
        (void)hfp_i2s_output_unlock(ESP_OK);
    }

    size_t bytes_written = 0U;
    esp_err_t err = hfp_i2s_output_ops_channel_write(
        s_output.channel, s_output.writer_pcm, requested_bytes,
        &bytes_written, s_output.config.write_timeout_ms);
    if (hfp_i2s_output_lock() != ESP_OK) return ESP_FAIL;
    s_output.write_calls++;
    if (err != ESP_OK || bytes_written != requested_bytes) {
        esp_err_t write_error = err != ESP_OK ? err : ESP_ERR_INVALID_SIZE;
        size_t confirmed_written = bytes_written < requested_bytes
            ? bytes_written : requested_bytes;
        uint64_t lost_bytes = (uint64_t)(requested_bytes - confirmed_written);
        bool terminal_fault = false;
        s_output.write_failures++;
        if (err == ESP_OK && bytes_written != requested_bytes) {
            s_output.short_writes++;
        }
        s_output.write_lost_bytes += lost_bytes;
        s_output.consecutive_write_failures++;
        hfp_i2s_output_set_error_locked(write_error);
        if (s_output.consecutive_write_failures >=
            s_output.config.max_consecutive_write_failures) {
            s_output.state = HFP_I2S_OUTPUT_FAULTED;
            terminal_fault = true;
            HFP_I2S_LOGE(
                "writer faulted after %" PRIu32
                " consecutive failures: error=%d written=%u expected=%u lost=%" PRIu64,
                s_output.consecutive_write_failures,
                (int)s_output.last_error, (unsigned)bytes_written,
                (unsigned)requested_bytes, lost_bytes);
        }
        esp_err_t unlock_error = hfp_i2s_output_unlock(write_error);
        if (!terminal_fault) return unlock_error;

        esp_err_t disable_error =
            hfp_i2s_output_ops_channel_disable(s_output.channel);
        if (hfp_i2s_output_lock() != ESP_OK) return ESP_FAIL;
        if (disable_error == ESP_OK) {
            s_output.channel_enabled = false;
        } else {
            hfp_i2s_output_enter_quarantine_locked(disable_error);
        }
        (void)hfp_i2s_output_unlock(ESP_OK);
        return disable_error != ESP_OK ? disable_error : unlock_error;
    }
    s_output.consecutive_write_failures = 0U;
    return hfp_i2s_output_unlock(ESP_OK);
}

esp_err_t hfp_i2s_output_get_snapshot(hfp_i2s_output_snapshot_t *out)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    esp_err_t err = hfp_i2s_output_lock();
    if (err != ESP_OK) return err;
    memset(out, 0, sizeof(*out));
    out->initialized = s_output.initialized;
    out->state = s_output.state;
    out->quarantined = s_output.state == HFP_I2S_OUTPUT_QUARANTINED;
    out->degraded = s_output.degraded;
    out->generation = atomic_load_explicit(&s_output.generation,
                                           memory_order_acquire);
    util_safe_copy_str(out->peer_mac, sizeof(out->peer_mac), s_output.peer_mac);
    out->config = s_output.config;
    out->start_calls = s_output.start_calls;
    out->stop_calls = s_output.stop_calls;
    out->start_failures = s_output.start_failures;
    out->stop_timeouts = s_output.stop_timeouts;
    out->write_calls = s_output.write_calls;
    out->write_failures = s_output.write_failures;
    out->short_writes = s_output.short_writes;
    out->write_lost_bytes = s_output.write_lost_bytes;
    out->silence_intervals = s_output.silence_intervals;
    out->silence_samples = s_output.silence_samples;
    out->degraded_events = s_output.degraded_events;
    out->consecutive_underflows = s_output.consecutive_underflows;
    out->push_calls = atomic_load_explicit(&s_output.push_calls,
                                           memory_order_relaxed);
    out->push_failures = atomic_load_explicit(&s_output.push_failures,
                                              memory_order_relaxed);
    out->stale_pushes = atomic_load_explicit(&s_output.stale_pushes,
                                             memory_order_relaxed);
    out->invalid_pushes = atomic_load_explicit(&s_output.invalid_pushes,
                                               memory_order_relaxed);
    out->quarantine_events = s_output.quarantine_events;
    out->last_error = s_output.last_error;
    if (s_output.ring_storage != NULL) {
        err = hfp_pcm_ring_get_snapshot(&s_output.ring, &out->ring);
    }
    return hfp_i2s_output_unlock(err);
}

const char *hfp_i2s_output_state_to_string(hfp_i2s_output_state_t state)
{
    switch (state) {
    case HFP_I2S_OUTPUT_UNINITIALIZED: return "uninitialized";
    case HFP_I2S_OUTPUT_STOPPED: return "stopped";
    case HFP_I2S_OUTPUT_STARTING: return "starting";
    case HFP_I2S_OUTPUT_RUNNING: return "running";
    case HFP_I2S_OUTPUT_STOPPING: return "stopping";
    case HFP_I2S_OUTPUT_FAULTED: return "faulted";
    case HFP_I2S_OUTPUT_QUARANTINED: return "quarantined";
    default: return "invalid";
    }
}
