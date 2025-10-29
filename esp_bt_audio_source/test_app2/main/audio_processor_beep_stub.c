// Test-only stub to satisfy linker when building unit-test app2.
#include <stdint.h>
#include "esp_err.h"

esp_err_t audio_processor_beep(uint32_t duration_ms)
{
    (void)duration_ms;
    return ESP_OK;
}
