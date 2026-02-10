/**
 * @file platform_timing_esp32.c
 * @brief ESP32 implementation of timing and delay functions
 * 
 * Uses ESP-IDF esp_timer for timestamps and FreeRTOS v TaskDelay for delays.
 * Provides microsecond-precision timing and tick-based task delays.
 * 
 * CODE_REVIEW8 P2.2 - Platform Shim Layer (Phase 2: Timing)
 */

#include "platform_timing.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char* TAG = "PLAT_TIMING_ESP32";

void platform_delay_ms(uint32_t ms) {
    if (ms == 0) {
        // Allow immediate return for 0ms delay
        return;
    }
    
    // Convert milliseconds to FreeRTOS ticks
    // pdMS_TO_TICKS handles rounding and tick resolution
    TickType_t ticks = pdMS_TO_TICKS(ms);
    
    // Ensure minimum 1 tick delay (avoid busy-wait for sub-tick delays)
    if (ticks == 0) {
        ticks = 1;
    }
    
    vTaskDelay(ticks);
    
    ESP_LOGV(TAG, "Delayed %lu ms (%lu ticks)", (unsigned long)ms, (unsigned long)ticks);
}

uint64_t platform_get_time_ms(void) {
    // esp_timer_get_time() returns microseconds since boot
    // Divide by 1000 to get milliseconds
    return (uint64_t)(esp_timer_get_time() / 1000ULL);
}

uint64_t platform_get_time_us(void) {
    // esp_timer_get_time() provides microsecond precision directly
    return (uint64_t)esp_timer_get_time();
}
