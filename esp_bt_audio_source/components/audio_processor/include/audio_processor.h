#ifndef AUDIO_PROCESSOR_H_
#define AUDIO_PROCESSOR_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#endif

/* When building against ESP-IDF the platform headers provide i2s types.
 * Host/unit tests (and desktop builds) don't have those headers, so provide
 * lightweight fallbacks to allow compiling the public header for tests.
 */
#ifdef ESP_PLATFORM
#include "driver/i2s_std.h"  // Use the current standard I2S driver instead of deprecated one
#else
/* Minimal host-side substitutes used only for building unit tests */
typedef int i2s_port_t;
#ifndef GPIO_NUM_NC
#define GPIO_NUM_NC (-1)
#endif
#endif

#ifndef AUDIO_CHUNK_BLOCK_BYTES
#define AUDIO_CHUNK_BLOCK_BYTES 1024
#endif

// Audio bit depths
typedef enum {
    AUDIO_BIT_DEPTH_16 = 16,
    AUDIO_BIT_DEPTH_24 = 24,
    AUDIO_BIT_DEPTH_32 = 32
} audio_bit_depth_t;

// Audio sample rates
typedef enum {
    AUDIO_SAMPLE_RATE_8K = 8000,
    AUDIO_SAMPLE_RATE_16K = 16000,
    AUDIO_SAMPLE_RATE_22K = 22050,
    AUDIO_SAMPLE_RATE_32K = 32000,
    AUDIO_SAMPLE_RATE_44K = 44100,
    AUDIO_SAMPLE_RATE_48K = 48000,
    AUDIO_SAMPLE_RATE_96K = 96000
} audio_sample_rate_t;

// Audio channel modes
typedef enum {
    AUDIO_CHANNEL_MONO = 1,
    AUDIO_CHANNEL_STEREO = 2
} audio_channel_t;

// Audio configuration
typedef struct {
    audio_sample_rate_t sample_rate;
    audio_bit_depth_t bit_depth;
    audio_channel_t channels;
    uint8_t volume;        // 0-100%
    bool mute;
    i2s_port_t i2s_port;  // I2S port number
    /* Optional I2S pin configuration (GPIO numbers). Use GPIO_NUM_NC for unused pins. */
    int i2s_bclk_pin;
    int i2s_ws_pin;
    int i2s_din_pin;
    int i2s_dout_pin;
} audio_config_t;

// Audio statistics
typedef struct {
    uint32_t samples_processed;
    uint32_t buffer_overruns;
    uint32_t buffer_underruns;
    uint64_t underrun_bytes;      // Total bytes zero-filled on underrun (CODE_REVIEW6)
    uint64_t bytes_read;          // Total bytes read from ring/queue (CODE_REVIEW6)
    uint32_t conversion_errors;
    float cpu_load;               // Percentage 0-1
    uint32_t current_buffer_level;
    uint32_t peak_buffer_level;
    
    /* Audio engine stats (CODE_REVIEW6 Phase 4, Task 4.2) */
    uint64_t bytes_by_source[3];  // Per-source byte counts: [I2S, SYNTH, SILENCE]
    uint32_t source_switch_count; // Number of times active source changed
    uint32_t beep_overlay_count;  // Number of times beep was overlaid
    uint64_t beep_overlay_bytes;  // Total bytes mixed with beep
    size_t   ring_peak_used;      // Peak ring buffer occupancy (bytes)
    uint32_t engine_write_calls;  // Number of audio_rb_write() calls
    uint64_t engine_write_bytes;  // Total bytes written to ring buffer
    uint32_t engine_pause_count;  // Times engine paused due to watermark
} audio_stats_t;

/** 
 * @brief Initialize the audio processor
 * 
 * @param config Audio configuration
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_processor_init(const audio_config_t* config);

/**
 * @brief Deinitialize the audio processor
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_processor_deinit(void);

/**
 * @brief Start audio processing
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_processor_start(void);

/**
 * @brief Stop audio processing
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_processor_stop(void);

/**
 * @brief Set the output sample rate
 * 
 * @param sample_rate New sample rate
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_processor_set_sample_rate(audio_sample_rate_t sample_rate);

/**
 * @brief Set the output bit depth
 * 
 * @param bit_depth New bit depth
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_processor_set_bit_depth(audio_bit_depth_t bit_depth);

/**
 * @brief Set the output channel count
 *
 * @param channels New channel mode (mono or stereo)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_processor_set_channels(audio_channel_t channels);

/**
 * @brief Set audio volume
 * 
 * @param volume Volume level (0-100%)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_processor_set_volume(uint8_t volume);

/**
 * @brief Mute or unmute audio
 * 
 * @param mute true to mute, false to unmute
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_processor_set_mute(bool mute);

/**
 * @brief Get current audio configuration
 * 
 * @param config Pointer to configuration structure to fill
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_processor_get_config(audio_config_t* config);

/**
 * @brief Get audio processing statistics
 * 
 * @param stats Pointer to statistics structure to fill
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_processor_get_stats(audio_stats_t* stats);

/**
 * @brief Read processed audio data into a caller-provided buffer.
 *
 * @param buffer Buffer to store audio data
 * @param size Size of the buffer
 * @param bytes_read Number of bytes read
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_processor_read(uint8_t* buffer, size_t size, size_t* bytes_read);

/**
 * @brief Drain any queued audio in the ring buffer and reset transient state.
 *
 * This discards pending audio, clears residual buffers, and stops any active
 * WAV/beep playback state so the pipeline resumes cleanly.
 */
esp_err_t audio_processor_drain_ring(void);

/**
 * @brief Simple audio runtime status
 */
typedef struct {
    bool initialized;
    bool running;
    uint8_t volume;    // 0-100
    bool mute;
    audio_sample_rate_t sample_rate;
    audio_bit_depth_t bit_depth;
    audio_channel_t channels;
} audio_status_t;

/**
 * @brief Get a compact runtime status of the audio processor
 */
esp_err_t audio_processor_get_status(audio_status_t* status);

/**
 * @brief Set I2S pins (bclk/ws/din[,dout]) at runtime.
 * If the audio processor is running it will be stopped and restarted to apply pins.
 */
esp_err_t audio_processor_set_i2s_pins(int bclk_pin, int ws_pin, int din_pin, int dout_pin);

/**
 * @brief Emit a short beep/tone via the audio pipeline (test-friendly API)
 *
 * @param duration_ms Duration of the beep in milliseconds (implementation may ignore)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_processor_beep(uint32_t duration_ms);

/**
 * @brief Emit a sine-wave beep with an explicit frequency.
 *
 * @param duration_ms Duration of the beep in milliseconds
 * @param freq_hz Frequency in Hz (e.g., 261.63 for middle C)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_processor_beep_tone(uint32_t duration_ms, double freq_hz);

/**
 * @brief Check if a beep is currently active (for testing)
 *
 * @return true if beep is active, false otherwise
 */
bool audio_processor_is_beep_active(void);

/**
 * @brief Query whether the processor is currently consuming live I2S input
 * (i.e., running with the real capture path instead of the synth source).
 */
bool audio_processor_is_i2s_active(void);

#ifdef UNIT_TEST
/**
 * @brief Retrieve the most recent beep request (for tests only).
 */
void audio_processor_get_last_beep_request(uint32_t* duration_ms, double* freq_hz);

/**
 * @brief Test hook: return active source ID from core audio source arbitration.
 * Values map to audio_stats_t bytes_by_source indexes: 0=I2S, 1=SYNTH, 2=SILENCE.
 */
int audio_processor_test_get_active_source_id(void);

/**
 * @brief Test hook: invoke core chunk production path from audio_processor.c.
 */
size_t audio_processor_test_produce_audio_chunk(uint8_t* dst, size_t dst_bytes);

/**
 * @brief Test hook: reset core logic transient state and stats used by 6.1 tests.
 */
void audio_processor_test_reset_core_logic_state(void);

/**
 * @brief Test hook: apply engine watermark hysteresis logic for one iteration.
 *
 * @param was_paused Previous paused state.
 * @param used_bytes Current ring used-bytes value.
 * @param pause_transition_out Optional out param incremented when transitioning to paused.
 * @return New paused state after applying hysteresis.
 */
bool audio_processor_test_compute_engine_paused(bool was_paused, size_t used_bytes, uint32_t* pause_transition_out);

/**
 * @brief Test hook: evaluate whether engine should produce a chunk this iteration.
 */
bool audio_processor_test_should_produce_chunk(bool paused, size_t free_bytes);

/**
 * @brief Test hook: invoke immediate volume commit behavior.
 */
esp_err_t audio_processor_test_commit_volume_now(void);

/**
 * @brief Test hook: force beep overlay application failure path in produce_audio_chunk().
 */
void audio_processor_test_set_force_beep_overlay_fail(bool enable);
#endif

/**
 * @brief Arm a one-shot diagnostic dump for the next beep invocation.
 */
void audio_processor_enable_next_beep_diag(void);
void audio_processor_set_diag_enabled(bool enable);
bool audio_processor_is_diag_enabled(void);
esp_err_t audio_processor_emit_diag_summary(void);

/**
 * @brief Arm a one-shot high-resolution I2S probe for the next N part-reads.
 *
 * The probe captures per-chunk timestamps and read results. Use
 * `audio_processor_emit_probe()` to retrieve and print the captured entries.
 *
 * @param n_entries Number of chunk-reads to capture (clamped internally)
 */
void audio_processor_arm_probe(size_t n_entries);

/**
 * @brief Emit the captured probe entries (if any) to the log/console.
 *
 * This prints a concise header followed by up to the captured number of
 * probe entries. Returns ESP_OK even if no entries were captured.
 */
esp_err_t audio_processor_emit_probe(void);

/**
 * @brief Emit a synchronous worker-like diagnostic snapshot.
 *
 * This helper performs a small, bounded synchronous conversion/resample of
 * a generated audio block (using the same synth/mock path the worker
 * would use) and emits a `DIAG:worker-out` hex dump for offline
 * inspection. Intended for deterministic captures from the serial console.
 */
esp_err_t audio_processor_emit_sync_worker_diag(void);


/**
 * @brief Convenience query for whether the audio processor is currently running
 *
 * This is a small, non-invasive runtime check intended for diagnostics and
 * telemetry consumers. It does not change processor state.
 *
 * @return true if the audio processor is running (I2S enabled), false otherwise
 */
bool audio_processor_is_running(void);

/**
 * @brief Force the audio processor to use the synthetic source at runtime.
 *
 * This allows the system to switch to the synthetic generator when I2S
 * hardware is absent or misbehaving. The setting is not persisted across
 * reboots.
 */
void audio_processor_set_synth_mode(bool enable);

/**
 * @brief Query whether synthetic source mode is currently enabled.
 */
bool audio_processor_is_synth_mode_enabled(void);


/**
 * @brief Force audio allocations to use DRAM only (disable PSRAM usage)
 *
 * When enabled the audio processor will avoid placing large, persistent
 * buffers in PSRAM and will instead allocate them from the default
 * allocator (DRAM). This is intended as a runtime diagnostic/workaround
 * for boards where PSRAM artifacts (noise/static) are observed. The
 * setting is not persisted across reboots and should be called before
 * `audio_processor_init()` to take effect for the initial allocations.
 */
void audio_processor_set_dram_only(bool enable);

/**
 * @brief Query the byte size of the runtime audio work buffers.
 *
 * Returns the per-buffer size selected during init. When the processor is not
 * initialized this returns 0.
 */
size_t audio_processor_get_work_buffer_bytes(void);


/**
 * @brief Inject audio data directly into the ring buffer (for testing only)
 *
 * This function is only available when CONFIG_BT_MOCK_TESTING is defined.
 * It allows unit tests to inject audio data directly into the processing pipeline
 * without requiring the audio processing task to be running.
 *
 * @param data Pointer to audio data to inject
 * @param size Size of the data in bytes
 * @return esp_err_t ESP_OK on success
 */
#ifdef CONFIG_BT_MOCK_TESTING
esp_err_t audio_processor_test_inject_audio_data(const uint8_t* data, size_t size);
void audio_processor_test_wav_reset_state(void);
void audio_processor_test_wav_begin(void);
void audio_processor_test_wav_add_pending(size_t bytes);
bool audio_processor_test_wav_consume(size_t bytes);
void audio_processor_test_wav_abort(void);
void audio_processor_test_wav_complete_if_idle(void);
bool audio_processor_test_wav_is_active(void);
size_t audio_processor_test_wav_pending_bytes(void);
size_t audio_processor_test_get_beep_remaining_bytes(void);
size_t audio_processor_test_get_audio_free_bytes(void);
size_t audio_processor_test_get_ring_used_bytes(void);
void audio_processor_test_idle_i2s_failures(int failures, bool synth_enabled, size_t beep_remaining, bool *synth_after, int *failures_after);
#endif

/* Diagnostic counter: number of times audio bytes were observed without a
 * matching metadata tag. Exposed for tests/telemetry; counter is always
 * available regardless of build config. */
uint32_t audio_processor_test_get_tag_miss_count(void);
void audio_processor_test_reset_tag_miss_count(void);
void audio_processor_test_reset_tag_recover_window(void);

#endif /* AUDIO_PROCESSOR_H_ */
