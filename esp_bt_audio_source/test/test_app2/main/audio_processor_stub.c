// Minimal audio_processor stub for test_app2 to satisfy linker
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "audio_processor.h"
#include "esp_err.h"
#include "esp_log.h"

#define AUDIO_PROC_STUB2_LOG_ONCE()                         \
    do {                                                    \
        static bool _logged = false;                        \
        if (!_logged) {                                     \
            ESP_LOGI(TAG, "audio_processor (test_app2) entered %s", __func__); \
            _logged = true;                                 \
        }                                                   \
    } while (0)

static const char *TAG = "AUDIO_PROC_STUB2";

/* Device test app stub for the audio processor API. The production build
 * provides a full implementation in `main/audio_processor.c`, but the unit
 * test firmware only needs minimal behavior so the command interface can
 * link and report deterministic state.
 */

typedef struct {
    audio_status_t status;
    audio_config_t config;
    bool diag_enabled;
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
    .diag_enabled = false,
};

esp_err_t __attribute__((weak)) audio_processor_get_status(audio_status_t *status)
{
    AUDIO_PROC_STUB2_LOG_ONCE();
    if (!status) {
        return ESP_ERR_INVALID_ARG;
    }
    *status = s_audio_stub.status;
    return ESP_OK;
}

esp_err_t __attribute__((weak)) audio_processor_set_mute(bool mute)
{
    AUDIO_PROC_STUB2_LOG_ONCE();
    s_audio_stub.status.mute = mute;
    s_audio_stub.config.mute = mute;
    return ESP_OK;
}

esp_err_t __attribute__((weak)) audio_processor_set_volume(uint8_t volume)
{
    AUDIO_PROC_STUB2_LOG_ONCE();
    if (volume > 100) {
        volume = 100;
    }
    s_audio_stub.status.volume = volume;
    s_audio_stub.config.volume = volume;
    return ESP_OK;
}

esp_err_t __attribute__((weak)) audio_processor_read(uint8_t* buffer, size_t size, size_t* bytes_read)
{
    AUDIO_PROC_STUB2_LOG_ONCE();
    (void)buffer;
    (void)size;
    if (bytes_read) {
        *bytes_read = 0;
    }
    return ESP_OK;
}

esp_err_t __attribute__((weak)) audio_processor_set_i2s_pins(int bclk_pin, int ws_pin, int din_pin, int dout_pin)
{
    AUDIO_PROC_STUB2_LOG_ONCE();
    s_audio_stub.config.i2s_bclk_pin = bclk_pin;
    s_audio_stub.config.i2s_ws_pin = ws_pin;
    s_audio_stub.config.i2s_din_pin = din_pin;
    s_audio_stub.config.i2s_dout_pin = dout_pin;
    return ESP_OK;
}

esp_err_t __attribute__((weak)) audio_processor_set_sample_rate(audio_sample_rate_t sample_rate)
{
    AUDIO_PROC_STUB2_LOG_ONCE();
    s_audio_stub.config.sample_rate = sample_rate;
    s_audio_stub.status.sample_rate = sample_rate;
    return ESP_OK;
}

// Additional lightweight stubs to satisfy linker when command_interface
// invokes higher-level audio_processor helpers that are not needed in
// the unit-test firmware.

esp_err_t __attribute__((weak)) audio_processor_play_wav(const char *path)
{
    AUDIO_PROC_STUB2_LOG_ONCE();
    (void)path;
    /* No-op in tests */
    return ESP_OK;
}

/* The public header declares this as `void audio_processor_enable_next_beep_diag(void)`
 * so provide the exact signature expected by production code. The test harness
 * doesn't need a return code here.
 */
void __attribute__((weak)) audio_processor_enable_next_beep_diag(void)
{
    AUDIO_PROC_STUB2_LOG_ONCE();
    s_audio_stub.diag_enabled = true;
}

void __attribute__((weak)) audio_processor_set_diag_enabled(bool enable)
{
    AUDIO_PROC_STUB2_LOG_ONCE();
    s_audio_stub.diag_enabled = enable;
}

bool __attribute__((weak)) audio_processor_is_diag_enabled(void)
{
    return s_audio_stub.diag_enabled;
}

bool __attribute__((weak)) audio_processor_is_synth_mode_enabled(void)
{
    AUDIO_PROC_STUB2_LOG_ONCE();
    return false;
}

esp_err_t __attribute__((weak)) audio_processor_emit_sync_worker_diag(void)
{
    AUDIO_PROC_STUB2_LOG_ONCE();
    return ESP_OK;
}

esp_err_t __attribute__((weak)) audio_processor_drain_ring(void)
{
    AUDIO_PROC_STUB2_LOG_ONCE();
    return ESP_OK;
}

/* Public header: `void audio_processor_set_dram_only(bool enable)` */
void __attribute__((weak)) audio_processor_set_dram_only(bool dram_only)
{
    AUDIO_PROC_STUB2_LOG_ONCE();
    (void)dram_only;
}

/* Diagnostic/probe stubs required by command interface */
esp_err_t __attribute__((weak)) audio_processor_emit_diag_summary(void)
{
    AUDIO_PROC_STUB2_LOG_ONCE();
    return ESP_OK;
}

void __attribute__((weak)) audio_processor_arm_probe(size_t n_entries)
{
    AUDIO_PROC_STUB2_LOG_ONCE();
    (void)n_entries;
}

esp_err_t __attribute__((weak)) audio_processor_emit_probe(void)
{
    AUDIO_PROC_STUB2_LOG_ONCE();
    return ESP_OK;
}

bool __attribute__((weak)) audio_processor_is_running(void)
{
    AUDIO_PROC_STUB2_LOG_ONCE();
    return s_audio_stub.status.running;
}

__attribute__((used, weak)) bool audio_processor_is_i2s_active(void)
{
    AUDIO_PROC_STUB2_LOG_ONCE();
    return false;
}

__attribute__((used, weak)) bool audio_processor_is_beep_active(void)
{
    AUDIO_PROC_STUB2_LOG_ONCE();
    return false;
}

__attribute__((used, weak)) bool audio_processor_is_wav_active(void)
{
    AUDIO_PROC_STUB2_LOG_ONCE();
    return false;
}

esp_err_t __attribute__((weak)) audio_processor_start(void)
{
    AUDIO_PROC_STUB2_LOG_ONCE();
    s_audio_stub.status.running = true;
    return ESP_OK;
}

esp_err_t __attribute__((weak)) audio_processor_stop(void)
{
    AUDIO_PROC_STUB2_LOG_ONCE();
    s_audio_stub.status.running = false;
    return ESP_OK;
}
