// Minimal audio_processor stub for test_app2 to satisfy linker
#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

esp_err_t audio_processor_read(uint8_t* buffer, size_t size, size_t* bytes_read)
{
    (void)buffer;
    (void)size;
    if (bytes_read) *bytes_read = 0;
    return ESP_OK;
}
#include "audio_processor.h"
#include "esp_err.h"
#include <string.h>

/* Device test app stub for the audio processor API. The production build
 * provides a full implementation in `main/audio_processor.c`, but the unit
 * test firmware only needs minimal behavior so the command interface can
 * link and report deterministic state.
 */

typedef struct {
    audio_status_t status;
    audio_config_t config;
} audio_stub_state_t;

static audio_stub_state_t s_audio_stub = {
    .status = {
        .initialized = true,
        .running = false,
        .volume = 50,
        .mute = false,
        .sample_rate = AUDIO_SAMPLE_RATE_44K,
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = AUDIO_CHANNEL_STEREO,
    },
    .config = {
        .sample_rate = AUDIO_SAMPLE_RATE_44K,
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = AUDIO_CHANNEL_STEREO,
        .volume = 50,
        .mute = false,
        .i2s_port = -1,
        .i2s_bclk_pin = -1,
        .i2s_ws_pin = -1,
        .i2s_din_pin = -1,
        .i2s_dout_pin = -1,
    },
};

esp_err_t audio_processor_get_status(audio_status_t *status)
{
    if (!status) {
        return ESP_ERR_INVALID_ARG;
    }
    *status = s_audio_stub.status;
    return ESP_OK;
}

esp_err_t audio_processor_set_mute(bool mute)
{
    s_audio_stub.status.mute = mute;
    s_audio_stub.config.mute = mute;
    return ESP_OK;
}

esp_err_t audio_processor_set_volume(uint8_t volume)
{
    if (volume > 100) {
        volume = 100;
    }
    s_audio_stub.status.volume = volume;
    s_audio_stub.config.volume = volume;
    return ESP_OK;
}

esp_err_t audio_processor_set_i2s_pins(int bclk_pin, int ws_pin, int din_pin, int dout_pin)
{
    s_audio_stub.config.i2s_bclk_pin = bclk_pin;
    s_audio_stub.config.i2s_ws_pin = ws_pin;
    s_audio_stub.config.i2s_din_pin = din_pin;
    s_audio_stub.config.i2s_dout_pin = dout_pin;
    return ESP_OK;
}

esp_err_t audio_processor_set_sample_rate(audio_sample_rate_t sample_rate)
{
    s_audio_stub.config.sample_rate = sample_rate;
    s_audio_stub.status.sample_rate = sample_rate;
    return ESP_OK;
}
