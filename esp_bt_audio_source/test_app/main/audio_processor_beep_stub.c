// Test-only stub to satisfy linker when building unit-test app.
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

static bool s_beep_active = false;

esp_err_t audio_processor_beep(uint32_t duration_ms)
{
    (void)duration_ms;
    s_beep_active = true;
    return ESP_OK;
}

bool audio_processor_is_beep_active(void)
{
    return s_beep_active;
}
