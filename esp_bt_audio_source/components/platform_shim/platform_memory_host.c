/**
 * @file platform_memory_host.c
 * @brief Host (POSIX) implementation of platform memory abstraction
 *
 * Maps platform_malloc/calloc/free to standard C library functions.
 * Ignores ESP32-specific memory capability flags (all memory is equal on host).
 */

#include "platform_memory.h"
#include <stdlib.h>

void *platform_malloc(size_t size, uint32_t caps) {
    (void)caps;  // Ignore capability flags on host (all memory is equal)
    return malloc(size);
}

void *platform_calloc(size_t count, size_t size, uint32_t caps) {
    (void)caps;  // Ignore capability flags on host
    return calloc(count, size);
}

void platform_free(void *ptr) {
    free(ptr);  // NULL-safe as per C standard
}
