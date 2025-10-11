// Minimal host-side stub of the audio_processor implementation used only for
// host unit tests. This file provides small, deterministic implementations
// of the functions referenced by the tests so that the host CMake which
// references ../../components/audio_processor/audio_processor.c can configure
// and build successfully.

#include <stdbool.h>
#include <stdio.h>
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

esp_err_t audio_processor_init(const audio_config_t* config)
{
    if (!config) return ESP_ERR_INVALID_ARG;
    s_config = *config;
    s_status.initialized = true;
    s_status.volume = s_config.volume;
    s_status.sample_rate = s_config.sample_rate;
    s_status.bit_depth = s_config.bit_depth;
    s_status.channels = s_config.channels;
    return ESP_OK;
}

esp_err_t audio_processor_deinit(void)
{
    s_status.initialized = false;
    s_status.running = false;
    return ESP_OK;
}

esp_err_t audio_processor_start(void)
{
    if (!s_status.initialized) return ESP_ERR_INVALID_STATE;
    s_status.running = true;
    return ESP_OK;
}

esp_err_t audio_processor_stop(void)
{
    s_status.running = false;
    return ESP_OK;
}

esp_err_t audio_processor_set_sample_rate(audio_sample_rate_t sample_rate)
{
    s_config.sample_rate = sample_rate;
    s_status.sample_rate = sample_rate;
    return ESP_OK;
}

esp_err_t audio_processor_set_bit_depth(audio_bit_depth_t bit_depth)
{
    s_config.bit_depth = bit_depth;
    s_status.bit_depth = bit_depth;
    return ESP_OK;
}

esp_err_t audio_processor_set_volume(uint8_t volume)
{
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
    s_config.mute = mute;
    s_status.mute = mute;
    return ESP_OK;
}

esp_err_t audio_processor_get_config(audio_config_t* config)
{
    if (!config) return ESP_ERR_INVALID_ARG;
    *config = s_config;
    return ESP_OK;
}

esp_err_t audio_processor_get_stats(audio_stats_t* stats)
{
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
    (void)buffer; (void)size;
    if (bytes_read) *bytes_read = 0;
    return ESP_OK;
}

esp_err_t audio_processor_get_status(audio_status_t* status)
{
    if (!status) return ESP_ERR_INVALID_ARG;
    *status = s_status;
    return ESP_OK;
}

esp_err_t audio_processor_set_i2s_pins(int bclk_pin, int ws_pin, int din_pin, int dout_pin)
{
    s_config.i2s_bclk_pin = bclk_pin;
    s_config.i2s_ws_pin = ws_pin;
    s_config.i2s_din_pin = din_pin;
    s_config.i2s_dout_pin = dout_pin;
    return ESP_OK;
}
