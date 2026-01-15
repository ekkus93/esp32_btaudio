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

#include "audio_processor_internal.h"
#include "nvs_storage.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#if (defined(CONFIG_SPIRAM) && CONFIG_SPIRAM) || (defined(CONFIG_SPIRAM_SUPPORT) && CONFIG_SPIRAM_SUPPORT)
#include "esp_psram.h"
#endif

static bool audio_processor_is_a2dp_connected(void)
{
    return bt_manager_is_a2dp_connected();
}

static void audio_source_tag_reset_buffer(void)
{
    /* Metadata buffer removed in the audio_queue path; nothing to reset. */
}

#ifdef CONFIG_BT_MOCK_TESTING
typedef struct {
    bool enabled;
    uint32_t frame_counter;
} mock_i2s_state_t;

static mock_i2s_state_t s_mock_i2s_state = {0};

/* Simple mock I2S audio generator used for host-unit tests. Produces a
 * deterministic byte pattern so consumers can validate reads. Returns the
 * number of bytes written into `buffer`. */
static size_t mock_generate_i2s_audio(uint8_t* buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0) {
        return 0;
    }

    for (size_t i = 0; i < buffer_size; ++i) {
        buffer[i] = (uint8_t)((i + s_mock_i2s_state.frame_counter) & 0xFF);
    }

    s_mock_i2s_state.frame_counter += (uint32_t)(buffer_size / 2);
    return buffer_size;
}
#endif

static esp_err_t configure_i2s(const audio_config_t* config);

void audio_processor_set_dram_only(bool enable)
{
    AUDIO_PROC_LOG_ONCE();
    s_dram_only_alloc = enable ? true : false;
    ESP_LOGI(TAG, "audio_processor: DRAM-only allocations %s", s_dram_only_alloc ? "ENABLED" : "DISABLED");
}

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

    /* Reset keepalive arming on each init so PLAY failures cannot inherit
     * armed state from earlier tests or sessions. */
    s_keepalive_armed = false;

    synth_manager_reset_state();

    // Copy configuration
    safe_memcpy(&s_audio_config, sizeof(s_audio_config), config, sizeof(audio_config_t));

    /* Initialize descriptor queue and block pool (128 x 1 KiB blocks in DRAM). */
    if (!audio_chunk_pool_init()) {
        ESP_LOGE(TAG, "audio_processor_init: failed to init audio chunk pool");
        return ESP_ERR_NO_MEM;
    }

    bool runtime_psram_ready = false;
#if (defined(CONFIG_SPIRAM) && CONFIG_SPIRAM) || (defined(CONFIG_SPIRAM_SUPPORT) && CONFIG_SPIRAM_SUPPORT)
    runtime_psram_ready = esp_psram_is_initialized();
#endif
    if (s_dram_only_alloc) {
        runtime_psram_ready = false;
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
     * available. */
    const size_t min_work_bytes = 1024U;

    esp_err_t ret = ESP_FAIL;
    while (try_work_bytes >= min_work_bytes && s_runtime_work_bytes == 0U) {
        const uint32_t caps = runtime_psram_ready ? (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : MALLOC_CAP_8BIT;
        s_capture_buffer = heap_caps_malloc(try_work_bytes, caps);
        s_proc_buffer = heap_caps_malloc(try_work_bytes, caps);
        s_proc_buffer2 = heap_caps_malloc(try_work_bytes, caps);
        if (s_capture_buffer != NULL && s_proc_buffer != NULL && s_proc_buffer2 != NULL) {
            s_runtime_work_bytes = try_work_bytes;
            break;
        }

        if (s_capture_buffer) { heap_caps_free(s_capture_buffer); s_capture_buffer = NULL; }
        if (s_proc_buffer) { heap_caps_free(s_proc_buffer); s_proc_buffer = NULL; }
        if (s_proc_buffer2) { heap_caps_free(s_proc_buffer2); s_proc_buffer2 = NULL; }

        try_work_bytes /= 2U;
    }

    if (s_runtime_work_bytes == 0U) {
        ESP_LOGE(TAG, "audio_processor_init: failed to allocate work buffers");
        return ESP_ERR_NO_MEM;
    }

    play_manager_buffers_t pm_bufs = {
        .work_bytes = s_runtime_work_bytes,
    };
    ret = play_manager_init(&s_audio_config, &pm_bufs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "audio_processor_init: play_manager_init failed (%d)", (int)ret);
        return ret;
    }

    i2s_manager_buffers_t i2s_bufs = {
        .raw_buf = s_capture_buffer,
        .raw_buf_bytes = s_runtime_work_bytes,
        .proc_buf = s_proc_buffer,
        .proc_buf2 = s_proc_buffer2,
        .work_bytes = s_runtime_work_bytes,
    };
    ret = i2s_manager_init(&s_audio_config, &i2s_bufs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "audio_processor_init: i2s_manager_init failed (%d)", (int)ret);
        play_manager_deinit();
        return ret;
    }

    ret = beep_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "audio_processor_init: beep_manager_init failed (%d)", (int)ret);
        i2s_manager_deinit();
        play_manager_deinit();
        return ret;
    }

    s_is_initialized = true;
    ESP_LOGI(TAG, "audio_processor_init: work_bytes=%zu psram=%s", s_runtime_work_bytes, runtime_psram_ready ? "yes" : "no");
    return ESP_OK;
}

esp_err_t audio_processor_start(void)
{
    AUDIO_PROC_LOG_ONCE();
    if (!s_is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_is_running) {
        return ESP_OK;
    }

    /* I2S capture has highest priority. Stop any ongoing WAV/BEEP playback
     * and clear queued audio so capture owns the pipeline. */
    wav_playback_abort(__func__);
    audio_processor_beep_reset();
    play_manager_abort(false);
    (void)audio_processor_drain_audio_queue();

    esp_err_t ret = i2s_manager_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "audio_processor_start: i2s_manager_start failed (%d)", (int)ret);
        return ret;
    }

    s_is_running = true;
    return ESP_OK;
}

esp_err_t audio_processor_stop(void)
{
    AUDIO_PROC_LOG_ONCE();
    if (!s_is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_is_running) {
        return ESP_OK;
    }

    i2s_manager_stop();
    s_is_running = false;
    s_keepalive_armed = false;
    s_force_synth = false;
    (void)audio_processor_drain_audio_queue();
    return ESP_OK;
}

esp_err_t audio_processor_deinit(void)
{
    AUDIO_PROC_LOG_ONCE();
    if (!s_is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_is_running) {
        (void)audio_processor_stop();
    }

    /* Reset playback state so re-init starts cleanly. */
    wav_playback_abort("audio_processor_deinit");
    audio_processor_flush_priority_queues("deinit");
    audio_processor_beep_reset();
    s_keepalive_armed = false;
    s_force_synth = false;
    s_last_source_was_synth = false;

    beep_manager_deinit();
    i2s_manager_deinit();
    play_manager_deinit();
    audio_chunk_pool_deinit();
    synth_manager_reset_state();

    if (s_capture_buffer) {
        heap_caps_free(s_capture_buffer);
        s_capture_buffer = NULL;
    }
    if (s_proc_buffer) {
        heap_caps_free(s_proc_buffer);
        s_proc_buffer = NULL;
    }
    if (s_proc_buffer2) {
        heap_caps_free(s_proc_buffer2);
        s_proc_buffer2 = NULL;
    }

    s_runtime_work_bytes = 0;
    s_audio_rb_residual_len = 0;
    s_audio_rb_residual_pos = 0;
    s_beep_prefill_active = false;
    s_beep_prefill_accum_bytes = 0;
    s_beep_prefill_goal_bytes = 0;
    s_beep_remaining_bytes = 0;
    s_wav_pending_bytes = 0;
    s_wav_playback_active = false;
    s_wav_prev_valid = false;
    s_wav_prev_force_synth = false;
    s_trace_next_read_call = false;
    s_trace_read_until_beep_done = false;
    s_diag_next_log_tick = 0;
    s_diag_last_conv_size = SIZE_MAX;
    s_diag_last_frame_bytes = SIZE_MAX;
    s_diag_last_src_rate = -1;
    s_diag_last_dst_rate = -1;
    s_i2s_read_ops = 0;
    s_i2s_total_read_bytes = 0;
    s_i2s_timeout_count = 0;
    s_probe_captured = 0;
    s_probe_target = 0;
    s_tag_miss_count = 0;
    s_tag_recover_mute_until = 0;
    s_volume_gain = 100;
    s_audio_diag_enabled = false;
    s_beep_restore_synth = false;
#ifdef CONFIG_BT_MOCK_TESTING
    s_i2s_consecutive_failures = 0;
    s_last_i2s_failure_log = 0;
#endif

    safe_memset(&s_audio_config, sizeof(s_audio_config), 0, sizeof(s_audio_config));
    safe_memset(&s_audio_stats, sizeof(s_audio_stats), 0, sizeof(s_audio_stats));

    s_is_initialized = false;
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

    if (s_audio_config.sample_rate == sample_rate) {
        return ESP_OK;
    }

    bool was_running = s_is_running;
    if (was_running) {
        esp_err_t ret = audio_processor_stop();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to stop audio processor: %d", ret);
            return ret;
        }
    }

    s_audio_config.sample_rate = sample_rate;

    play_manager_deinit();
    play_manager_buffers_t pm_bufs = {
        .work_bytes = s_runtime_work_bytes,
    };
    esp_err_t ret = play_manager_init(&s_audio_config, &pm_bufs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reconfigure play_manager: %d", ret);
        return ret;
    }

    i2s_manager_deinit();
    i2s_manager_buffers_t i2s_bufs = {
        .raw_buf = s_capture_buffer,
        .raw_buf_bytes = s_runtime_work_bytes,
        .proc_buf = s_proc_buffer,
        .proc_buf2 = s_proc_buffer2,
        .work_bytes = s_runtime_work_bytes,
    };
    ret = i2s_manager_init(&s_audio_config, &i2s_bufs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reconfigure I2S via i2s_manager: %d", ret);
        play_manager_deinit();
        return ret;
    }

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

    safe_memcpy(config, sizeof(*config), &s_audio_config, sizeof(audio_config_t));
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

    safe_memcpy(stats, sizeof(*stats), &s_audio_stats, sizeof(audio_stats_t));
    return ESP_OK;
}

esp_err_t audio_processor_acquire_chunk(audio_chunk_t *out_chunk, TickType_t wait_ticks)
{
    return audio_processor_acquire_chunk_internal(out_chunk, wait_ticks);
}

esp_err_t audio_processor_release_chunk(const audio_chunk_t *chunk)
{
    if (chunk == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (chunk->data != NULL) {
        audio_chunk_release_block(chunk->data);
    }
    return ESP_OK;
}


/*******************************
 * Internal functions
 *******************************/

/**
 * @brief Configure capture path via i2s_manager using shared buffers.
 */
static esp_err_t configure_i2s(const audio_config_t* config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    i2s_manager_deinit();

    i2s_manager_buffers_t bufs = {
        .raw_buf = s_capture_buffer,
        .raw_buf_bytes = s_runtime_work_bytes,
        .proc_buf = s_proc_buffer,
        .proc_buf2 = s_proc_buffer2,
        .work_bytes = s_runtime_work_bytes,
    };

    return i2s_manager_init(config, &bufs);
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

esp_err_t audio_processor_drain_audio_queue(void)
{
    AUDIO_PROC_LOG_ONCE();
    if (!s_is_initialized) return ESP_ERR_INVALID_STATE;

    audio_chunk_clear();
    audio_source_tag_reset_buffer();
    audio_processor_beep_reset();
    wav_playback_abort(__func__);
    ESP_LOGI(TAG, "audio_processor_drain_audio_queue: cleared audio_queue and beep state");
    return ESP_OK;
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
     * synthesize the same beep tone so the snapshot is representative of
     * an actual audio tone rather than a memset test pattern. */
#if defined(CONFIG_AUDIO_USE_SYNTH_SOURCE)
    generated = synth_manager_generate_audio(s_proc_buffer, target, &s_audio_config, &s_force_synth, &s_beep_lock);
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
     * the same fade duration as the runtime beep so diagnostics and live
     * playback match. */
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
 * @brief Play a WAV file by reading PCM frames, converting/resampling as needed
 * and enqueueing into the shared audio_queue.
 */
esp_err_t audio_processor_play_wav(const char* path)
{
    AUDIO_PROC_LOG_ONCE();
    /* Diagnostic: record initialization/running state to help trace
     * unexpected INVALID_STATE returns observed in unit tests. Use both
     * ESP_LOG and a plain printf so the test monitor captures the output
     * regardless of log configuration. */
        size_t queue_free = audio_processor_queue_free_bytes();
        ESP_LOGD(TAG, "audio_processor_play_wav: entry (s_is_initialized=%d, s_is_running=%d, queue_free=%zu)",
              (int)s_is_initialized, (int)s_is_running, queue_free);
        printf("DIAG-APLAY-STATE: init=%d run=%d queue_free=%zu path=%s\n",
            (int)s_is_initialized, (int)s_is_running, queue_free, path ? path : "(null)");
    if (!s_is_initialized) return ESP_ERR_INVALID_STATE;
    if (!path) return ESP_ERR_INVALID_ARG;

    /* Drop any synthetic keepalive so WAV output is audible immediately. */
    if (s_force_synth) {
        audio_processor_flush_priority_queues("play_wav");
        s_last_source_was_synth = false;
    }

    /* Reject PLAY while A2DP is disconnected to avoid queueing audio that
     * cannot be consumed. */
    if (!audio_processor_is_a2dp_connected()) {
        ESP_LOGW(TAG, "audio_processor_play_wav: A2DP not connected; rejecting PLAY");
        return ESP_ERR_INVALID_STATE;
    }

    /* I2S capture has priority over PLAY; reject when capture is active. */
    if (i2s_manager_is_running()) {
        ESP_LOGW(TAG, "audio_processor_play_wav: I2S running; rejecting PLAY");
        return ESP_ERR_INVALID_STATE;
    }

    /* Reject WAV playback while a beep is active or queued to keep the
     * paths isolated and avoid audible contention. */
    bool beep_active = (beep_manager_get_state() == BEEP_STATE_PLAYING);
    portENTER_CRITICAL(&s_beep_lock);
    if (!beep_active && s_beep_remaining_bytes > 0) {
        beep_active = true;
    }
    portEXIT_CRITICAL(&s_beep_lock);
    if (beep_active) {
        ESP_LOGW(TAG, "audio_processor_play_wav: busy (beep active)");
        return ESP_ERR_INVALID_STATE;
    }

    s_trace_next_read_call = true;
    ESP_LOGI(TAG, "audio_processor_play_wav: armed one-shot trace for next audio_processor_read call");

    esp_err_t status = ESP_OK;

    /* Clear queues/beep state before enqueuing WAV data. */
    (void)audio_processor_drain_audio_queue();
    audio_processor_beep_reset();

    wav_playback_begin();

    status = play_manager_play_wav(path);
    if (status != ESP_OK) {
        wav_playback_abort(__func__);
        goto cleanup;
    }

    portENTER_CRITICAL(&s_wav_lock);
    s_wav_pending_bytes = play_manager_pending_bytes();
    portEXIT_CRITICAL(&s_wav_lock);

    if (!play_manager_is_active() && s_wav_pending_bytes == 0) {
        wav_playback_complete_if_idle();
    }

    ESP_LOGI(TAG, "audio_processor_play_wav: playback streaming for %s", path);
    status = ESP_OK;

cleanup:
    if (status == ESP_OK) {
        /* Only arm the synth keepalive once real playback has succeeded. */
        s_keepalive_armed = true;
        /* Disable synth once real playback is active. */
        s_force_synth = false;
    }

    return status;
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

    if (s_audio_config.bit_depth == bit_depth) {
        return ESP_OK; // Nothing to change
    }

    bool was_running = s_is_running;

    if (was_running) {
        esp_err_t ret = audio_processor_stop();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to stop audio processor: %d", ret);
            return ret;
        }
    }

    s_audio_config.bit_depth = bit_depth;

    play_manager_deinit();
    play_manager_buffers_t pm_bufs = {
        .work_bytes = s_runtime_work_bytes,
    };
    esp_err_t ret = play_manager_init(&s_audio_config, &pm_bufs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reconfigure play_manager: %d", ret);
        return ret;
    }

    i2s_manager_deinit();
    i2s_manager_buffers_t i2s_bufs = {
        .raw_buf = s_capture_buffer,
        .raw_buf_bytes = s_runtime_work_bytes,
        .proc_buf = s_proc_buffer,
        .proc_buf2 = s_proc_buffer2,
        .work_bytes = s_runtime_work_bytes,
    };
    ret = i2s_manager_init(&s_audio_config, &i2s_bufs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reconfigure I2S via i2s_manager: %d", ret);
        play_manager_deinit();
        return ret;
    }

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

bool audio_processor_is_i2s_active(void)
{
    AUDIO_PROC_LOG_ONCE();
    return s_is_running;
}

bool audio_processor_is_wav_active(void)
{
    AUDIO_PROC_LOG_ONCE();
    return wav_playback_is_active();
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

bool audio_processor_is_synth_mode_enabled(void)
{
    AUDIO_PROC_LOG_ONCE();
    return s_force_synth;
}

void audio_processor_set_synth_mode(bool enable)
{
    AUDIO_PROC_LOG_ONCE();
    s_force_synth = enable;
    ESP_LOGI(TAG, "audio_processor: synth mode %s", enable ? "ENABLED" : "DISABLED");
}

uint32_t audio_processor_test_get_tag_miss_count(void)
{
    return __atomic_load_n(&s_tag_miss_count, __ATOMIC_RELAXED);
}

void audio_processor_test_reset_tag_miss_count(void)
{
    __atomic_store_n(&s_tag_miss_count, 0U, __ATOMIC_RELAXED);
}

void audio_processor_test_reset_tag_recover_window(void)
{
    s_tag_recover_mute_until = 0;
}

size_t audio_processor_test_get_audio_free_bytes(void)
{
    AUDIO_PROC_LOG_ONCE();
    return audio_processor_queue_free_bytes();
}

#ifdef CONFIG_BT_MOCK_TESTING
void audio_processor_test_idle_i2s_failures(int failures, bool synth_enabled, size_t beep_remaining, bool *synth_after, int *failures_after)
{
    AUDIO_PROC_LOG_ONCE();
    s_i2s_consecutive_failures = failures;
    s_force_synth = synth_enabled;
    s_beep_remaining_bytes = beep_remaining;
    s_keepalive_armed = true;
    s_wav_playback_active = false;
    s_last_i2s_failure_log = -I2S_FAILURE_LOG_THROTTLE;
    if (s_i2s_consecutive_failures >= I2S_FAILURE_THRESHOLD &&
        (s_i2s_consecutive_failures - s_last_i2s_failure_log) >= I2S_FAILURE_LOG_THROTTLE &&
        s_keepalive_armed && s_beep_remaining_bytes == 0 && !s_force_synth) {
        s_last_i2s_failure_log = s_i2s_consecutive_failures;
        s_force_synth = true;
        s_i2s_consecutive_failures = 0;
    }
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
#endif /* CONFIG_BT_MOCK_TESTING */

#if defined(CONFIG_BT_MOCK_TESTING) || defined(UNIT_TEST)
void audio_processor_test_set_queue_block_override(size_t max_item_bytes)
{
    s_test_queue_block_override = max_item_bytes;
}
#endif /* CONFIG_BT_MOCK_TESTING || UNIT_TEST */

#ifdef CONFIG_BT_MOCK_TESTING
/* Return number of queued audio descriptors (each carries its source tag).
 * Callers can use this to validate producer/consumer balance during tests. */
size_t audio_processor_test_get_tag_used(void)
{
    return audio_descriptor_used();
}
#endif
