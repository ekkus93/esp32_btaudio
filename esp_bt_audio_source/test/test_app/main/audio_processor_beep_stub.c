// Test-only stub to satisfy linker when building unit-test app.
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

static bool s_beep_active = false;
static uint32_t s_last_beep_duration_ms = 0;
static double s_last_beep_freq_hz = 0.0;

esp_err_t audio_processor_beep_tone(uint32_t duration_ms, double freq_hz)
{
    s_last_beep_duration_ms = duration_ms;
    s_last_beep_freq_hz = freq_hz;
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

// Tag-ringbuffer diagnostics are not exercised in test_app; provide a no-op
// stub so command_interface can link.
esp_err_t audio_processor_dump_tag_ringbuffer(size_t max_items, size_t *captured_out)
{
    (void)max_items;
    if (captured_out) {
        *captured_out = 0;
    }
    return ESP_OK;
}
