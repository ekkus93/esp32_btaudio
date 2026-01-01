#include "audio_processor.h"
#include "esp_err.h"
#include "esp_log.h"
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

#define AUDIO_PROC_STUB_LOG_ONCE()                                                       \
    do {                                                                                \
        static bool _logged = false;                                                    \
        if (!_logged) {                                                                 \
            ESP_LOGI(TAG, "audio_processor (test_app) entered %s", __func__);          \
            _logged = true;                                                             \
        }                                                                               \
    } while (0)

static const char *TAG = "AUDIO_PROC_STUB";

/*
 * Lightweight stub of the audio processor interface for the Unity test_app
 * firmware. The real application provides a full implementation that drives
 * the I2S peripheral; the unit tests only require deterministic behaviour so
 * the command interface links and reports sensible state.
 */

typedef struct {
    audio_status_t status;
    audio_config_t config;
    audio_stats_t stats;
    bool diag_enabled;
} audio_stub_state_t;

static audio_stub_state_t s_audio_stub = {
    .status = {
        .initialized = false,
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
        .i2s_bclk_pin = GPIO_NUM_NC,
        .i2s_ws_pin = GPIO_NUM_NC,
        .i2s_din_pin = GPIO_NUM_NC,
        .i2s_dout_pin = GPIO_NUM_NC,
    },
    .stats = {
        .samples_processed = 0,
        .buffer_overruns = 0,
        .buffer_underruns = 0,
        .conversion_errors = 0,
        .cpu_load = 0.0f,
        .current_buffer_level = 0,
        .peak_buffer_level = 0,
    },
    .diag_enabled = false,
};

static void clamp_volume(uint8_t *volume)
{
    if (!volume) {
        return;
    }
    if (*volume > 100) {
        *volume = 100;
    }
}

esp_err_t audio_processor_init(const audio_config_t *config)
{
    AUDIO_PROC_STUB_LOG_ONCE();
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    s_audio_stub.config = *config;
    clamp_volume(&s_audio_stub.config.volume);

    s_audio_stub.status.initialized = true;
    s_audio_stub.status.running = false;
    s_audio_stub.status.volume = s_audio_stub.config.volume;
    s_audio_stub.status.mute = s_audio_stub.config.mute;
    s_audio_stub.status.sample_rate = s_audio_stub.config.sample_rate;
    s_audio_stub.status.bit_depth = s_audio_stub.config.bit_depth;
    s_audio_stub.status.channels = s_audio_stub.config.channels;

    return ESP_OK;
}

esp_err_t audio_processor_deinit(void)
{
    AUDIO_PROC_STUB_LOG_ONCE();
    s_audio_stub.status.initialized = false;
    s_audio_stub.status.running = false;
    return ESP_OK;
}

esp_err_t audio_processor_start(void)
{
    AUDIO_PROC_STUB_LOG_ONCE();
    if (!s_audio_stub.status.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    s_audio_stub.status.running = true;
    return ESP_OK;
}

esp_err_t audio_processor_stop(void)
{
    AUDIO_PROC_STUB_LOG_ONCE();
    s_audio_stub.status.running = false;
    return ESP_OK;
}

esp_err_t audio_processor_set_sample_rate(audio_sample_rate_t sample_rate)
{
    AUDIO_PROC_STUB_LOG_ONCE();
    s_audio_stub.config.sample_rate = sample_rate;
    s_audio_stub.status.sample_rate = sample_rate;
    return ESP_OK;
}

esp_err_t audio_processor_set_bit_depth(audio_bit_depth_t bit_depth)
{
    AUDIO_PROC_STUB_LOG_ONCE();
    s_audio_stub.config.bit_depth = bit_depth;
    s_audio_stub.status.bit_depth = bit_depth;
    return ESP_OK;
}

esp_err_t audio_processor_set_volume(uint8_t volume)
{
    AUDIO_PROC_STUB_LOG_ONCE();
    clamp_volume(&volume);
    s_audio_stub.config.volume = volume;
    s_audio_stub.status.volume = volume;
    return ESP_OK;
}

esp_err_t audio_processor_set_mute(bool mute)
{
    AUDIO_PROC_STUB_LOG_ONCE();
    s_audio_stub.config.mute = mute;
    s_audio_stub.status.mute = mute;
    return ESP_OK;
}

esp_err_t audio_processor_get_config(audio_config_t *config)
{
    AUDIO_PROC_STUB_LOG_ONCE();
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    *config = s_audio_stub.config;
    return ESP_OK;
}

esp_err_t audio_processor_get_stats(audio_stats_t *stats)
{
    AUDIO_PROC_STUB_LOG_ONCE();
    if (!stats) {
        return ESP_ERR_INVALID_ARG;
    }
    *stats = s_audio_stub.stats;
    return ESP_OK;
}

esp_err_t audio_processor_acquire_chunk(audio_chunk_t *out_chunk, TickType_t wait_ticks)
{
    (void)wait_ticks;
    AUDIO_PROC_STUB_LOG_ONCE();
    if (out_chunk == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    /* Test stub produces silence */
    uint8_t *buf = (uint8_t *)malloc(AUDIO_CHUNK_BLOCK_BYTES);
    if (buf == NULL) {
        return ESP_ERR_NO_MEM;
    }
    memset(buf, 0, AUDIO_CHUNK_BLOCK_BYTES);
    out_chunk->data = buf;
    out_chunk->len = AUDIO_CHUNK_BLOCK_BYTES;
    out_chunk->tag = AUDIO_SOURCE_TAG_CAPTURE;
    out_chunk->tag_id = 0;
    return ESP_OK;
}

esp_err_t audio_processor_release_chunk(const audio_chunk_t *chunk)

{
    AUDIO_PROC_STUB_LOG_ONCE();
    if (chunk == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (chunk->data != NULL) {
        free(chunk->data);
    }
    return ESP_OK;
}

esp_err_t audio_processor_read(uint8_t *buffer, size_t size, size_t *bytes_read)
{
    AUDIO_PROC_STUB_LOG_ONCE();
    if (!buffer || size == 0 || !bytes_read) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(buffer, 0, size);
    *bytes_read = size;
    s_audio_stub.stats.samples_processed += size;
    s_audio_stub.stats.current_buffer_level = 0;
    return ESP_OK;
}

esp_err_t audio_processor_get_status(audio_status_t *status)
{
    AUDIO_PROC_STUB_LOG_ONCE();
    if (!status) {
        return ESP_ERR_INVALID_ARG;
    }
    *status = s_audio_stub.status;
    return ESP_OK;
}

esp_err_t audio_processor_set_i2s_pins(int bclk_pin, int ws_pin, int din_pin, int dout_pin)
{
    AUDIO_PROC_STUB_LOG_ONCE();
    s_audio_stub.config.i2s_bclk_pin = bclk_pin;
    s_audio_stub.config.i2s_ws_pin = ws_pin;
    s_audio_stub.config.i2s_din_pin = din_pin;
    s_audio_stub.config.i2s_dout_pin = dout_pin;
    return ESP_OK;
}

/*
 * Additional lightweight stubs to satisfy symbols referenced by
 * `components/command_interface/commands.c` and other test-only code.
 * These intentionally perform no real audio work; they return success so
 * command handlers can exercise flow-control and responses.
 */

esp_err_t audio_processor_play_wav(const char *path)
{
    AUDIO_PROC_STUB_LOG_ONCE();
    (void)path;
    /* Treat WAV play requests as accepted in the test stub */
    return ESP_OK;
}

void audio_processor_enable_next_beep_diag(void)
{
    AUDIO_PROC_STUB_LOG_ONCE();
    /* Arm diagnostic flag in stub for visibility */
    s_audio_stub.diag_enabled = true;
}

void audio_processor_set_diag_enabled(bool enable)
{
    AUDIO_PROC_STUB_LOG_ONCE();
    s_audio_stub.diag_enabled = enable;
}

bool audio_processor_is_diag_enabled(void)
{
    return s_audio_stub.diag_enabled;
}

esp_err_t audio_processor_emit_sync_worker_diag(void)
{
    AUDIO_PROC_STUB_LOG_ONCE();
    /* Indicate diagnostic emitted successfully */
    return ESP_OK;
}

esp_err_t audio_processor_drain_audio_queue(void)
{
    AUDIO_PROC_STUB_LOG_ONCE();
    /* No-op drain; report success */
    return ESP_OK;
}

void audio_processor_set_dram_only(bool enable)
{
    AUDIO_PROC_STUB_LOG_ONCE();
    (void)enable;
    /* Test stub doesn't allocate differently; no-op */
}

/* Minimal diagnostic/probe stubs referenced by the command interface.
 * These are no-ops in the test firmware but must be available at link-time.
 */
esp_err_t audio_processor_emit_diag_summary(void)
{
    AUDIO_PROC_STUB_LOG_ONCE();
    return ESP_OK;
}

void audio_processor_arm_probe(size_t n_entries)
{
    AUDIO_PROC_STUB_LOG_ONCE();
    (void)n_entries;
}

esp_err_t audio_processor_emit_probe(void)
{
    AUDIO_PROC_STUB_LOG_ONCE();
    return ESP_OK;
}

