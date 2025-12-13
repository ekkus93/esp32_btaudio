#ifndef _AUDIO_PROCESSOR_H_
#define _AUDIO_PROCESSOR_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

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
    uint32_t conversion_errors;
    float cpu_load;               // Percentage 0-1
    uint32_t current_buffer_level;
    uint32_t peak_buffer_level;
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
 * @brief Read processed audio data
 * 
 * @param buffer Buffer to store audio data
 * @param size Size of the buffer
 * @param bytes_read Number of bytes read
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_processor_read(uint8_t* buffer, size_t size, size_t* bytes_read);

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

#ifdef UNIT_TEST
/**
 * @brief Retrieve the most recent beep request (for tests only).
 */
void audio_processor_get_last_beep_request(uint32_t* duration_ms, double* freq_hz);
#endif

/**
 * @brief Arm a one-shot diagnostic dump for the next beep invocation.
 */
void audio_processor_enable_next_beep_diag(void);
void audio_processor_set_diag_enabled(bool enable);
bool audio_processor_is_diag_enabled(void);

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
 * @brief Snapshot the metadata tag ringbuffer and log its contents.
 *
 * This is a diagnostic-only helper. It temporarily pauses task scheduling,
 * copies up to `max_items` tag entries (tag type + monotonic counter) from
 * the metadata ringbuffer, re-queues them unchanged to preserve runtime
 * state, and emits an ESP_LOGI line per entry. The ringbuffer ordering and
 * counters are preserved; producers/consumers see the same contents after
 * the snapshot completes.
 *
 * @param max_items Maximum entries to log (clamped to a small internal cap)
 * @param captured_out Optional pointer to receive the number of entries logged
 * @return esp_err_t ESP_OK on success, ESP_ERR_INVALID_STATE if ringbuffer is missing
 */
esp_err_t audio_processor_dump_tag_ringbuffer(size_t max_items, size_t *captured_out);

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
 * @brief Drain (clear) the internal audio ring buffer of any queued items.
 *
 * This is a debug/test helper to free any queued audio blocks so that
 * subsequent beeps will be enqueued into the ringbuffer instead of
 * triggering fallback due to saturation. Use sparingly and only from
 * diagnostic command paths.
 */
esp_err_t audio_processor_drain_ringbuffer(void);

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
/* Test-only helper: return number of bytes currently stored in the
 * metadata/tag ringbuffer. This is only available in mock/test builds
 * where CONFIG_BT_MOCK_TESTING is defined. The value equals the number
 * of tag bytes enqueued (each tag is one byte per audio chunk). */
size_t audio_processor_test_get_tag_used(void);
#endif

/**
 * @brief Play a WAV file from the filesystem and inject it into the audio pipeline
 *
 * The path may be absolute (for example "/spiffs/worker_long_norm.wav") or
 * relative (in which case callers should prefix with "/spiffs/" when invoking
 * from the command layer). The implementation supports PCM WAV files and
 * performs conversion/resampling to the configured output format when
 * necessary.
 */
esp_err_t audio_processor_play_wav(const char* path);

#endif /* _AUDIO_PROCESSOR_H_ */
