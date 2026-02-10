/**
 * @file platform_timing.h
 * @brief Platform-independent timing and delay functions
 * 
 * This header provides a unified interface for timing operations across different platforms.
 * The implementation is selected at build time based on the target platform.
 * 
 * CODE_REVIEW8 P2.2 - Platform Shim Layer (Phase 2: Timing)
 * 
 * USAGE:
 *   - ESP32 builds: Use esp_timer and FreeRTOS vTaskDelay
 *   - Host builds: Use system clock and sleep functions
 * 
 * THREAD SAFETY:
 *   - All functions are thread-safe on ESP32 (leveraging esp_timer and FreeRTOS)
 *   - Host implementation is single-threaded mock (sufficient for unit tests)
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Delay execution for specified milliseconds
 * 
 * Blocks the calling task/thread for the specified duration.
 * 
 * ESP32: Uses vTaskDelay() - yields to other tasks during delay
 * Host: Uses usleep() or busy-wait - simple delay for tests
 * 
 * @param ms Delay duration in milliseconds
 * 
 * @note On ESP32, actual delay may be slightly longer due to tick resolution
 *       (configTICK_RATE_HZ, typically 100Hz = 10ms resolution)
 * @note Minimum delay on ESP32 is 1 tick (~10ms at 100Hz tick rate)
 * 
 * Example:
 * @code
 * platform_delay_ms(100);  // Wait 100ms
 * @endcode
 */
void platform_delay_ms(uint32_t ms);

/**
 * @brief Get current time in milliseconds since boot
 * 
 * Returns a monotonic timestamp in milliseconds since system boot.
 * Suitable for measuring elapsed time and timeouts.
 * 
 * ESP32: Uses esp_timer_get_time() / 1000 (microsecond precision)
 * Host: Uses clock_gettime() or gettimeofday() (system time)
 * 
 * @return Current time in milliseconds
 * 
 * @note Timestamp wraps after ~49.7 days (2^32 milliseconds)
 * @note For ESP32, based on esp_timer which is paused during deep sleep
 * @note For host tests, uses system monotonic clock if available
 * 
 * Example:
 * @code
 * uint64_t start_time = platform_get_time_ms();
 * do_work();
 * uint64_t elapsed = platform_get_time_ms() - start_time;
 * ESP_LOGI(TAG, "Work took %llu ms", elapsed);
 * @endcode
 */
uint64_t platform_get_time_ms(void);

/**
 * @brief Get current time in microseconds since boot (high precision)
 * 
 * Returns a monotonic timestamp in microseconds since system boot.
 * Higher precision than platform_get_time_ms() for timing-critical code.
 * 
 * ESP32: Uses esp_timer_get_time() directly (1us precision)
 * Host: Uses clock_gettime() with nanosecond resolution if available
 * 
 * @return Current time in microseconds
 * 
 * @note Timestamp wraps after ~49.7 days (2^32 milliseconds)
 * @note Use for measuring short intervals (< 1ms) accurately
 * 
 * Example:
 * @code
 * uint64_t start_us = platform_get_time_us();
 * fast_operation();
 * uint64_t elapsed_us = platform_get_time_us() - start_us;
 * ESP_LOGD(TAG, "Fast operation took %llu us", elapsed_us);
 * @endcode
 */
uint64_t platform_get_time_us(void);

#ifdef __cplusplus
}
#endif
