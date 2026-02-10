/**
 * @file platform_timing_host.c
 * @brief Host/test implementation of timing and delay functions
 * 
 * Provides mock timing for single-threaded unit tests using standard C/POSIX functions.
 * Uses clock_gettime() for timestamps and usleep() for delays on POSIX systems.
 * Falls back to gettimeofday() if clock_gettime() is not available.
 * 
 * CODE_REVIEW8 P2.2 - Platform Shim Layer (Phase 2: Timing)
 * 
 * NOTE: This implementation is designed for single-threaded host tests.
 * For production embedded builds, use platform_timing_esp32.c.
 */

#include "platform_timing.h"
#include "esp_log.h"
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <stddef.h>

static const char* TAG = "PLAT_TIMING_HOST";

// Boot time reference (initialized on first call)
static uint64_t s_boot_time_us = 0;
static bool s_initialized = false;

/**
 * @brief Initialize boot time reference (called automatically on first use)
 */
static void init_boot_time(void) {
    if (s_initialized) {
        return;
    }
    
#ifdef CLOCK_MONOTONIC
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        s_boot_time_us = (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
    } else {
        // Fallback to gettimeofday if clock_gettime fails
        struct timeval tv;
        gettimeofday(&tv, NULL);
        s_boot_time_us = (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
    }
#else
    // No clock_gettime, use gettimeofday
    struct timeval tv;
    gettimeofday(&tv, NULL);
    s_boot_time_us = (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
#endif
    
    s_initialized = true;
    ESP_LOGD(TAG, "Initialized host timing (boot time: %llu us)", s_boot_time_us);
}

void platform_delay_ms(uint32_t ms) {
    if (ms == 0) {
        return;  // No delay for 0ms
    }
    
    // Convert milliseconds to microseconds for usleep()
    uint32_t us = ms * 1000;
    
    // usleep() may not work for very long delays (>1 second on some systems)
    // Split into smaller chunks if needed
    while (us > 1000000) {
        usleep(1000000);  // Sleep 1 second
        us -= 1000000;
    }
    
    if (us > 0) {
        usleep(us);
    }
    
    ESP_LOGV(TAG, "Host delayed %lu ms", (unsigned long)ms);
}

uint64_t platform_get_time_ms(void) {
    uint64_t current_us = platform_get_time_us();
    return current_us / 1000ULL;
}

uint64_t platform_get_time_us(void) {
    init_boot_time();  // Ensure boot time is initialized
    
    uint64_t current_us;
    
#ifdef CLOCK_MONOTONIC
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        current_us = (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
    } else {
        // Fallback to gettimeofday
        struct timeval tv;
        gettimeofday(&tv, NULL);
        current_us = (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
    }
#else
    // No clock_gettime, use gettimeofday
    struct timeval tv;
    gettimeofday(&tv, NULL);
    current_us = (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
#endif
    
    // Return time since "boot" (relative to first call)
    return current_us - s_boot_time_us;
}
