/* Host-only lightweight stubs for audio_processor symbols.
 * These implementations are intentionally minimal — they exist only to
 * satisfy linker references for native host tests and should not
 * attempt to exercise device-only functionality.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../../components/audio_processor/include/audio_processor.h"

/* Host-only stub for audio_processor used by native unit tests.
 * Provides a minimal, self-contained implementation of the public
 * audio_processor API so host tests can run without linking the
 * full production implementation.
 */

#include "../../components/audio_processor/include/audio_processor.h"
#include "esp_err.h"
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

static bool s_initialized = false;
static bool s_running = false;
static uint8_t s_volume = 50;
static bool s_mute = false;
static audio_sample_rate_t s_sample_rate = AUDIO_SAMPLE_RATE_44K;
static audio_bit_depth_t s_bit_depth = AUDIO_BIT_DEPTH_16;
static audio_channel_t s_channels = AUDIO_CHANNEL_STEREO;
static bool s_beep_active = false;
static bool s_synth_mode = true;
static uint32_t s_last_beep_duration_ms = 0;
static double s_last_beep_freq_hz = 0.0;
static size_t s_tag_used = 0;
static uint32_t s_tag_miss_count = 0;
static uint16_t s_tag_counter = 0;
static bool s_skip_scale_once = false;

/* Error injection for testing */
static esp_err_t s_read_error = ESP_OK;

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
    s_synth_mode = true;
    s_running = false;
    /* Reset transient playback state so host tests start from a clean slate. */
    s_beep_active = false;
    s_ring_len = 0;
    return ESP_OK;
}

esp_err_t audio_processor_deinit(void)
{
    s_initialized = false;
    s_running = false;
    s_synth_mode = true;
    s_beep_active = false;
    s_ring_len = 0;
    return ESP_OK;
}

esp_err_t audio_processor_start(void)
{
    if (!s_initialized) {
        /* Host tests may call start without an explicit init; assume defaults. */
        s_initialized = true;
        s_synth_mode = true;
    }
    /* Starting the processor preempts any pending host-side playback so
     * tests that seed beep state observe a clean transition. */
    s_beep_active = false;
    s_ring_len = 0;
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

    /* Error injection for testing */
    if (s_read_error != ESP_OK) {
        return s_read_error;
    }

    /* If muted and no active beep, we should still consume data but return
     * zeros (tests expect sized reads even when muted). If a beep is active,
     * beep data was appended to the ring and will be returned normally.
     */
    size_t n = ring_pop(buffer, size);
    if (bytes_read) *bytes_read = n;

    /* Apply volume scaling for 16-bit samples unless a one-shot bypass is
     * requested (used by tag/reset tests that assert raw byte equality). */
    bool skip_scale = s_skip_scale_once;
    s_skip_scale_once = false;
    if (!skip_scale && n > 0 && s_bit_depth == AUDIO_BIT_DEPTH_16) {
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

    /* Consume one pending tag per read chunk; count misses when tags are absent */
    if (n > 0) {
        if (s_tag_used > 0) {
            s_tag_used -= 1;
        } else {
            s_tag_miss_count += 1;
        }
    }

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

esp_err_t audio_processor_beep_tone(uint32_t duration_ms, double freq_hz)
{
    if (!s_initialized) {
        /* Allow host commands to beep without prior init. */
        s_initialized = true;
        s_synth_mode = true;
    }
    /* Allow capture to continue because playback now routes exclusively through A2DP. */

    /* Beep should disable the synth keepalive so subsequent reads come from
     * the generated beep rather than the idle synth source. */
    s_synth_mode = false;

    /* Host stub: clear any pending ring data so beep insertion mirrors
     * production behavior where prior audio is dropped before beeps. */
    s_ring_len = 0;

    s_last_beep_duration_ms = duration_ms;
    s_last_beep_freq_hz = freq_hz;
    /* Generate a short burst of non-zero sample bytes and append to ring.
     * Use a simple deterministic pattern so tests can detect non-zero data.
     */
    const size_t beep_bytes = 256; /* small audible chunk for host tests */
    uint8_t buf[beep_bytes];
    for (size_t i = 0; i < beep_bytes; ++i) buf[i] = (uint8_t)((i * 31) & 0xFF);
    ring_append(buf, beep_bytes);
    if (s_tag_used < SIZE_MAX) {
        s_tag_used += 1;
    }
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

size_t audio_processor_test_get_beep_remaining_bytes(void)
{
    return s_ring_len;
}

void audio_processor_get_last_beep_request(uint32_t* duration_ms, double* freq_hz)
{
    if (duration_ms) {
        *duration_ms = s_last_beep_duration_ms;
    }
    if (freq_hz) {
        *freq_hz = s_last_beep_freq_hz;
    }
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

void audio_processor_enable_next_beep_diag(void)
{
    /* No-op for host stub */
}

esp_err_t audio_processor_emit_sync_worker_diag(void)
{
    /* No-op for host stub; return success so tests can proceed */
    return ESP_OK;
}

esp_err_t audio_processor_drain_ring(void)
{
    s_ring_len = 0;
    return ESP_OK;
}

void audio_processor_set_dram_only(bool enable)
{
    (void)enable; /* no-op in host tests */
}

bool audio_source_tag_test_init_buffer(size_t buf_size)
{
    (void)buf_size;
    s_tag_used = 0;
    s_tag_counter = 0;
    s_tag_miss_count = 0;
    return true;
}

void audio_source_tag_test_deinit_buffer(void)
{
    s_tag_used = 0;
    s_tag_counter = 0;
    s_tag_miss_count = 0;
}

uint16_t audio_source_tag_test_get_counter(void)
{
    return s_tag_counter;
}

void audio_source_tag_test_set_counter(uint16_t v)
{
    s_tag_counter = v;
}

bool audio_source_tag_test_push(int tag)
{
    (void)tag;
    const size_t cap = 8192; /* match device tag buffer capacity */
    if (s_tag_used >= cap) {
        return false;
    }
    s_tag_used += 1;
    return true;
}

bool audio_source_tag_test_take(int* tag_out, uint32_t wait_ticks)
{
    (void)wait_ticks;
    if (s_tag_used == 0) return false;
    s_tag_used -= 1;
    if (tag_out) {
        *tag_out = (int)(s_tag_counter++);
    }
    return true;
}

void audio_source_tag_test_drop_one(void)
{
    if (s_tag_used > 0) s_tag_used -= 1;
}

void audio_source_tag_test_reset_buffer(void)
{
    s_tag_used = 0;
    s_tag_counter = 0;
    /* Ensure subsequent reads see only freshly injected data */
    s_ring_len = 0;
    s_beep_active = false;
    s_skip_scale_once = true;
}

esp_err_t audio_processor_test_inject_audio_data(const uint8_t* data, size_t size)
{
    if (!data || size == 0) return ESP_ERR_INVALID_ARG;
    const size_t cap = 8192;
    if (s_tag_used >= cap) return ESP_ERR_NO_MEM;
    s_tag_used += 1;
    /* Append injected data to our ring buffer used by audio_processor_read */
    ring_append(data, size);
    return ESP_OK;
}

size_t audio_processor_test_get_tag_used(void)
{
    /* If no explicit tags are tracked, fall back to presence of audio data
     * so host tests still observe progress. This keeps native-only tests
     * aligned even when the lightweight stub omits full metadata handling. */
    if (s_tag_used == 0 && s_ring_len > 0) {
        return 1;
    }
    return s_tag_used;
}

uint32_t audio_processor_test_get_tag_miss_count(void)
{
    return s_tag_miss_count;
}

void audio_processor_test_reset_tag_miss_count(void)
{
    s_tag_miss_count = 0;
}

void audio_processor_test_idle_i2s_failures(int failures, bool synth_enabled, size_t beep_remaining, bool *synth_after, int *failures_after)
{
    /* Minimal host approximation of the production idle I2S failure handler.
     * If we see enough consecutive failures while no beep data is pending,
     * re-enable synth mode and reset the failure counter. Otherwise, leave
     * state unchanged so tests can assert the sticky path.
     */
    const int threshold = 20; /* matches production constant */
    bool synth = synth_enabled;
    int fail = failures;

    if (beep_remaining == 0 && failures >= threshold) {
        synth = true;
        fail = 0;
    }

    if (synth_after) {
        *synth_after = synth;
    }
    if (failures_after) {
        *failures_after = fail;
    }
}
esp_err_t audio_processor_dump_tag_queue(size_t max_items, size_t *captured_out)
{
    (void)max_items;
    if (captured_out) {
        *captured_out = 0;
    }
    return ESP_OK;
}

/* Test helper functions for error injection */
void audio_processor_test_set_read_error(esp_err_t error)
{
    s_read_error = error;
}

void audio_processor_test_clear_read_error(void)
{
    s_read_error = ESP_OK;
}

bool audio_processor_is_i2s_active(void)
{
    return s_running && !s_synth_mode;
}

/* ── uart_source stubs (UARTAUDIO) ───────────────────────────────────────
 * uart_audio.c (command_interface_host) queries the UART audio source for
 * STATUS/BUSY decisions; command-handler tests don't exercise the real
 * staging ring, so a minimal controllable stub suffices. */
#include "../../components/audio_processor/include/uart_source.h"

static bool s_stub_uart_source_active = false;

void uart_source_stub_set_active(bool active)
{
    s_stub_uart_source_active = active;
}

bool uart_source_is_active(void)
{
    return s_stub_uart_source_active;
}

void uart_source_get_stats(uart_source_stats_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->state = s_stub_uart_source_active ? UART_SOURCE_STATE_ACTIVE
                                           : UART_SOURCE_STATE_INACTIVE;
}

/* A2DP pull-rate stub (UARTAUDIO diagnostics) — command-handler tests
 * only need the call to succeed with quiescent values. */
esp_err_t audio_processor_get_read_rate(audio_read_rate_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    return ESP_OK;
}
