#include "audio_processor_internal.h"
#include <math.h>

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
    AUDIO_PROC_LOG_ONCE();  // NOLINT(bugprone-branch-clone)
    if (!s_is_initialized) {
    	return ESP_ERR_INVALID_STATE;
    }

    size_t target = audio_get_runtime_work_bytes();
    if (target == 0) {
    	return ESP_ERR_INVALID_ARG;
    }

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

    if (generated == 0) {
    	return ESP_ERR_INVALID_SIZE;
    }

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
    if (conv_size == 0) {
    	return ESP_ERR_INVALID_SIZE;
    }

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
    if (res_size == 0) {
    	return ESP_ERR_INVALID_SIZE;
    }

    size_t dump = res_size < DIAG_DUMP_BYTES ? res_size : DIAG_DUMP_BYTES;
    diag_dump_bytes(s_proc_buffer2, dump, "DIAG:worker-out");
    return ESP_OK;
}
