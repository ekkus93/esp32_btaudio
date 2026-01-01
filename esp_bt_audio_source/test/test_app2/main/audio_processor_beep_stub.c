// Test-only stub to satisfy linker when building unit-test app2.
#include <stdint.h>
#include "esp_err.h"

esp_err_t audio_processor_beep(uint32_t duration_ms)
{
    (void)duration_ms;
    return ESP_OK;
}

esp_err_t audio_processor_beep_tone(uint32_t duration_ms, double freq_hz)
{
    (void)duration_ms;
    (void)freq_hz;
    return ESP_OK;
}

esp_err_t audio_processor_dump_tag_queue(size_t max_items, size_t *captured_out)
{
    (void)max_items;
    if (captured_out) {
        *captured_out = 0;
    }
    return ESP_OK;
}
