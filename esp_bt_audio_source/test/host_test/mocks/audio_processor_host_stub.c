/* Host-only stub for audio_processor used by native unit tests.
 * Provides a minimal, self-contained implementation of the public
 * audio_processor API so host tests can run without linking the
 * full production implementation.
 */

#include "../../main/include/audio_processor.h"
#include "esp_err.h"
#include <string.h>
#include <stdlib.h>

static bool s_initialized = false;
static bool s_running = false;
static uint8_t s_volume = 50;
static bool s_mute = false;
static audio_sample_rate_t s_sample_rate = AUDIO_SAMPLE_RATE_44K;
static audio_bit_depth_t s_bit_depth = AUDIO_BIT_DEPTH_16;
static audio_channel_t s_channels = AUDIO_CHANNEL_STEREO;
static bool s_beep_active = false;
static bool s_synth_mode = false;

/* Simple host-side FIFO for injected/produced audio data. */
static uint8_t* s_ring = NULL;
static size_t s_ring_capacity = 0;
static size_t s_ring_len = 0; /* bytes currently stored, read consumes front bytes */

static void ensure_capacity_for_append(size_t add)
{
    size_t need = s_ring_len + add;
    if (need <= s_ring_capacity) return;
    size_t newcap = s_ring_capacity ? s_ring_capacity * 2 : 1024;
    while (newcap < need) newcap *= 2;
    s_ring = (uint8_t*)realloc(s_ring, newcap);
    s_ring_capacity = newcap;
}

static void ring_append(const uint8_t* data, size_t len)
{
    if (len == 0) return;
    ensure_capacity_for_append(len);
    /* append at end */
    memcpy(s_ring + s_ring_len, data, len);
    s_ring_len += len;
}

/* Consume up to `len` bytes from the front of the ring buffer into out. */
static size_t ring_pop(uint8_t* out, size_t len)
{
    if (s_ring_len == 0) return 0;
    size_t n = (len < s_ring_len) ? len : s_ring_len;
    if (out && n > 0) memcpy(out, s_ring, n);
    /* shift remaining data to front */
    if (n < s_ring_len) memmove(s_ring, s_ring + n, s_ring_len - n);
    s_ring_len -= n;
    return n;
}

esp_err_t audio_processor_init(const audio_config_t* config)
{
    if (config) {
        s_sample_rate = config->sample_rate;
        s_bit_depth = config->bit_depth;
        s_channels = config->channels;
        s_volume = config->volume;
        s_mute = config->mute;
    }
    s_initialized = true;
    s_running = false;
    return ESP_OK;
}

esp_err_t audio_processor_deinit(void)
{
    s_initialized = false;
    s_running = false;
    return ESP_OK;
}

esp_err_t audio_processor_start(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    s_running = true;
    return ESP_OK;
}

esp_err_t audio_processor_stop(void)
{
    s_running = false;
    return ESP_OK;
}

esp_err_t audio_processor_set_sample_rate(audio_sample_rate_t sample_rate)
{
    s_sample_rate = sample_rate;
    return ESP_OK;
}

esp_err_t audio_processor_set_bit_depth(audio_bit_depth_t bit_depth)
{
    s_bit_depth = bit_depth;
    return ESP_OK;
}

esp_err_t audio_processor_set_volume(uint8_t volume)
{
    if (volume > 100) volume = 100;
    s_volume = volume;
    return ESP_OK;
}

esp_err_t audio_processor_set_mute(bool mute)
{
    s_mute = mute;
    return ESP_OK;
}

esp_err_t audio_processor_get_config(audio_config_t* config)
{
    if (!config) return ESP_ERR_INVALID_ARG;
    memset(config, 0, sizeof(*config));
    config->sample_rate = s_sample_rate;
    config->bit_depth = s_bit_depth;
    config->channels = s_channels;
    config->volume = s_volume;
    config->mute = s_mute;
    config->i2s_port = -1;
    config->i2s_bclk_pin = GPIO_NUM_NC;
    config->i2s_ws_pin = GPIO_NUM_NC;
    config->i2s_din_pin = GPIO_NUM_NC;
    config->i2s_dout_pin = GPIO_NUM_NC;
    return ESP_OK;
}

esp_err_t audio_processor_get_stats(audio_stats_t* stats)
{
    if (!stats) return ESP_ERR_INVALID_ARG;
    memset(stats, 0, sizeof(*stats));
    stats->cpu_load = 0.0f;
    return ESP_OK;
}

esp_err_t audio_processor_read(uint8_t* buffer, size_t size, size_t* bytes_read)
{
    if (bytes_read) *bytes_read = 0;
    if (!buffer || size == 0) return ESP_OK;

    /* If muted and no active beep, we should still consume data but return
     * zeros (tests expect sized reads even when muted). If a beep is active,
     * beep data was appended to the ring and will be returned normally.
     */
    size_t n = ring_pop(buffer, size);
    if (bytes_read) *bytes_read = n;

    /* Apply volume scaling for 16-bit samples if needed */
    if (n > 0 && s_bit_depth == AUDIO_BIT_DEPTH_16) {
        size_t samples = n / sizeof(int16_t);
        int16_t* s = (int16_t*)buffer;
        float scale = (float)s_volume / 100.0f;
        for (size_t i = 0; i < samples; ++i) {
            int32_t v = s[i];
            v = (int32_t)(v * scale);
            if (v > INT16_MAX) v = INT16_MAX;
            if (v < INT16_MIN) v = INT16_MIN;
            s[i] = (int16_t)v;
        }
    }

    /* If muted and no beep active, zero the returned data */
    if (s_mute && !s_beep_active) {
        if (n > 0) memset(buffer, 0, n);
    }

    /* If we drained all beep data, clear flag */
    if (s_beep_active && s_ring_len == 0) s_beep_active = false;

    return ESP_OK;
}

esp_err_t audio_processor_get_status(audio_status_t* status)
{
    if (!status) return ESP_ERR_INVALID_ARG;
    status->initialized = s_initialized;
    status->running = s_running;
    status->volume = s_volume;
    status->mute = s_mute;
    status->sample_rate = s_sample_rate;
    status->bit_depth = s_bit_depth;
    status->channels = s_channels;
    return ESP_OK;
}

esp_err_t audio_processor_set_i2s_pins(int bclk_pin, int ws_pin, int din_pin, int dout_pin)
{
    (void)bclk_pin; (void)ws_pin; (void)din_pin; (void)dout_pin;
    return ESP_OK;
}

esp_err_t audio_processor_beep(uint32_t duration_ms)
{
    (void)duration_ms;
    /* Generate a short burst of non-zero sample bytes and append to ring.
     * Use a simple deterministic pattern so tests can detect non-zero data.
     */
    const size_t beep_bytes = 256; /* small audible chunk for host tests */
    uint8_t buf[beep_bytes];
    for (size_t i = 0; i < beep_bytes; ++i) buf[i] = (uint8_t)((i * 31) & 0xFF);
    ring_append(buf, beep_bytes);
    s_beep_active = true;
    return ESP_OK;
}

bool audio_processor_is_beep_active(void)
{
    return s_beep_active;
}

bool audio_processor_is_running(void)
{
    return s_running;
}

void audio_processor_set_synth_mode(bool enable)
{
    s_synth_mode = enable;
}

bool audio_processor_is_synth_mode_enabled(void)
{
    return s_synth_mode;
}

#ifdef CONFIG_BT_MOCK_TESTING
esp_err_t audio_processor_test_inject_audio_data(const uint8_t* data, size_t size)
{
    if (!data || size == 0) return ESP_ERR_INVALID_ARG;
    /* Append injected data to our ring buffer used by audio_processor_read */
    ring_append(data, size);
    return ESP_OK;
}
#endif
