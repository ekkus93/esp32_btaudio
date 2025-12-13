// Minimal host-side stub of the audio_processor implementation used only for
// host unit tests. This file provides small, deterministic implementations
// of the functions referenced by the tests so that the host CMake which
// references ../../components/audio_processor/audio_processor.c can configure
// and build successfully.

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "../../main/include/audio_processor.h"
#include "esp_err.h"

/* For host/unit tests provide a small, test-only stub for NVS persistence.
 * Guarded so production builds don't pick this up.
 */
#ifdef TEST_HOST
void nvs_storage_set_volume(uint8_t volume)
{
    (void)volume;
}
#endif

#define AUDIO_PROC_HOST_LOG_ONCE()                                                       \
    do {                                                                                \
        static bool _logged = false;                                                    \
        if (!_logged) {                                                                 \
            printf("audio_processor (host_stub) entered %s\n", __func__);            \
            _logged = true;                                                             \
        }                                                                               \
    } while (0)

static audio_config_t s_config = {
    .sample_rate = AUDIO_SAMPLE_RATE_44K,
    .bit_depth = AUDIO_BIT_DEPTH_16,
    .channels = AUDIO_CHANNEL_STEREO,
    .volume = 100,
    .mute = false,
    .i2s_port = -1,
    .i2s_bclk_pin = -1,
    .i2s_ws_pin = -1,
    .i2s_din_pin = -1,
    .i2s_dout_pin = -1
};

static audio_status_t s_status = {
    .initialized = false,
    .running = false,
    .volume = 100,
    .mute = false,
    .sample_rate = AUDIO_SAMPLE_RATE_44K,
    .bit_depth = AUDIO_BIT_DEPTH_16,
    .channels = AUDIO_CHANNEL_STEREO
};

static bool s_beep_active = false;
static uint32_t s_last_beep_duration_ms = 0;
static double s_last_beep_freq_hz = 0.0;

// Test buffer for host testing - simulates ring buffer behavior
#define TEST_BUFFER_SIZE 4096
static uint8_t s_test_buffer[TEST_BUFFER_SIZE];
static size_t s_test_buffer_write_pos = 0;
static size_t s_test_buffer_read_pos = 0;
static size_t s_test_buffer_count = 0;

esp_err_t audio_processor_init(const audio_config_t* config)
{
    AUDIO_PROC_HOST_LOG_ONCE();
    if (!config) return ESP_ERR_INVALID_ARG;
    s_config = *config;
    s_status.initialized = true;
    s_status.volume = s_config.volume;
    s_status.sample_rate = s_config.sample_rate;
    s_status.bit_depth = s_config.bit_depth;
    s_status.channels = s_config.channels;
    
    // Initialize test buffer for host testing
    s_test_buffer_write_pos = 0;
    s_test_buffer_read_pos = 0;
    s_test_buffer_count = 0;
    
    return ESP_OK;
}

esp_err_t audio_processor_deinit(void)
{
    AUDIO_PROC_HOST_LOG_ONCE();
    s_status.initialized = false;
    s_status.running = false;
    return ESP_OK;
}

esp_err_t audio_processor_start(void)
{
    AUDIO_PROC_HOST_LOG_ONCE();
    if (!s_status.initialized) return ESP_ERR_INVALID_STATE;
    s_status.running = true;
    return ESP_OK;
}

esp_err_t audio_processor_stop(void)
{
    AUDIO_PROC_HOST_LOG_ONCE();
    s_status.running = false;
    return ESP_OK;
}

esp_err_t audio_processor_set_sample_rate(audio_sample_rate_t sample_rate)
{
    AUDIO_PROC_HOST_LOG_ONCE();
    s_config.sample_rate = sample_rate;
    s_status.sample_rate = sample_rate;
    return ESP_OK;
}

esp_err_t audio_processor_set_bit_depth(audio_bit_depth_t bit_depth)
{
    AUDIO_PROC_HOST_LOG_ONCE();
    s_config.bit_depth = bit_depth;
    s_status.bit_depth = bit_depth;
    return ESP_OK;
}

esp_err_t audio_processor_set_volume(uint8_t volume)
{
    AUDIO_PROC_HOST_LOG_ONCE();
    if (!s_status.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (volume > 100) {
        volume = 100;
    }

    s_config.volume = volume;
    s_status.volume = volume;

#ifdef TEST_HOST
    nvs_storage_set_volume(s_status.volume);
#endif

    return ESP_OK;
}

esp_err_t audio_processor_set_mute(bool mute)
{
    AUDIO_PROC_HOST_LOG_ONCE();
    s_config.mute = mute;
    s_status.mute = mute;
    return ESP_OK;
}

esp_err_t audio_processor_get_config(audio_config_t* config)
{
    AUDIO_PROC_HOST_LOG_ONCE();
    if (!config) return ESP_ERR_INVALID_ARG;
    *config = s_config;
    return ESP_OK;
}

esp_err_t audio_processor_get_stats(audio_stats_t* stats)
{
    AUDIO_PROC_HOST_LOG_ONCE();
    if (!stats) return ESP_ERR_INVALID_ARG;
    stats->samples_processed = 0;
    stats->buffer_overruns = 0;
    stats->buffer_underruns = 0;
    stats->conversion_errors = 0;
    stats->cpu_load = 0.0f;
    stats->current_buffer_level = 0;
    stats->peak_buffer_level = 0;
    return ESP_OK;
}

esp_err_t audio_processor_read(uint8_t* buffer, size_t size, size_t* bytes_read)
{
    AUDIO_PROC_HOST_LOG_ONCE();
    if (!buffer || !bytes_read) return ESP_ERR_INVALID_ARG;
    
    size_t available = s_test_buffer_count;
    size_t to_read = (size < available) ? size : available;
    
    if (to_read == 0) {
        *bytes_read = 0;
        return ESP_OK;
    }
    
    // Read from circular buffer
    for (size_t i = 0; i < to_read; i++) {
        buffer[i] = s_test_buffer[s_test_buffer_read_pos];
        s_test_buffer_read_pos = (s_test_buffer_read_pos + 1) % TEST_BUFFER_SIZE;
    }
    s_test_buffer_count -= to_read;
    
    // Apply volume scaling if not muted and not 100%
    if (!s_config.mute && s_config.volume != 100) {
        if (s_config.bit_depth == AUDIO_BIT_DEPTH_16) {
            int16_t* samples = (int16_t*)buffer;
            size_t num_samples = to_read / 2; // 16-bit samples
            
            for (size_t i = 0; i < num_samples; i++) {
                if (s_config.volume == 0) {
                    samples[i] = 0; // Mute
                } else {
                    // Apply volume scaling: multiply by volume/100
                    samples[i] = (int16_t)((int32_t)samples[i] * s_config.volume / 100);
                }
            }
        }
    } else if (s_config.mute) {
        // Mute: zero out the buffer
        memset(buffer, 0, to_read);
    }
    
    *bytes_read = to_read;
    return ESP_OK;
}

esp_err_t audio_processor_get_status(audio_status_t* status)
{
    AUDIO_PROC_HOST_LOG_ONCE();
    if (!status) return ESP_ERR_INVALID_ARG;
    *status = s_status;
    return ESP_OK;
}

esp_err_t audio_processor_set_i2s_pins(int bclk_pin, int ws_pin, int din_pin, int dout_pin)
{
    AUDIO_PROC_HOST_LOG_ONCE();
    s_config.i2s_bclk_pin = bclk_pin;
    s_config.i2s_ws_pin = ws_pin;
    s_config.i2s_din_pin = din_pin;
    s_config.i2s_dout_pin = dout_pin;
    return ESP_OK;
}

// Simple beep implementation used by host/unit tests. Production builds
// should provide a proper hardware-backed implementation.
esp_err_t audio_processor_beep_tone(uint32_t duration_ms, double freq_hz)
{
    AUDIO_PROC_HOST_LOG_ONCE();
    if (!s_status.initialized) return ESP_ERR_INVALID_STATE;

    s_last_beep_duration_ms = duration_ms;
    s_last_beep_freq_hz = freq_hz;

    /* Generate a deterministic pattern and append to the test buffer to
     * simulate audible data. */
    const size_t beep_bytes = 256;
    uint8_t buf[beep_bytes];
    for (size_t i = 0; i < beep_bytes; ++i) buf[i] = (uint8_t)((i * 31) & 0xFF);

    if (beep_bytes > TEST_BUFFER_SIZE - s_test_buffer_count) {
        return ESP_ERR_NO_MEM;
    }

    for (size_t i = 0; i < beep_bytes; ++i) {
        s_test_buffer[(s_test_buffer_write_pos + i) % TEST_BUFFER_SIZE] = buf[i];
    }
    s_test_buffer_write_pos = (s_test_buffer_write_pos + beep_bytes) % TEST_BUFFER_SIZE;
    s_test_buffer_count += beep_bytes;
    s_beep_active = true;
    return ESP_OK;
}

esp_err_t audio_processor_beep(uint32_t duration_ms)
{
    return audio_processor_beep_tone(duration_ms, 1000.0);
}

bool audio_processor_is_beep_active(void)
{
    return s_beep_active;
}

void audio_processor_get_last_beep_request(uint32_t* duration_ms, double* freq_hz)
{
    if (duration_ms) *duration_ms = s_last_beep_duration_ms;
    if (freq_hz) *freq_hz = s_last_beep_freq_hz;
}

#ifdef CONFIG_BT_MOCK_TESTING
// Test helper function to inject audio data directly into the ring buffer
// This bypasses the audio processing task for unit testing
esp_err_t audio_processor_test_inject_audio_data(const uint8_t* data, size_t size)
{
    AUDIO_PROC_HOST_LOG_ONCE();
    if (!data) return ESP_ERR_INVALID_ARG;
    
    // Check if there's enough space in the test buffer
    if (size > TEST_BUFFER_SIZE - s_test_buffer_count) {
        return ESP_ERR_NO_MEM; // Not enough space
    }
    
    // Write to circular buffer
    for (size_t i = 0; i < size; i++) {
        s_test_buffer[s_test_buffer_write_pos] = data[i];
        s_test_buffer_write_pos = (s_test_buffer_write_pos + 1) % TEST_BUFFER_SIZE;
    }
    s_test_buffer_count += size;
    
    return ESP_OK;
}
#endif
