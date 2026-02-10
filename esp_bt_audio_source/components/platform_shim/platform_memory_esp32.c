/**
 * @file platform_memory_esp32.c
 * @brief ESP32 implementation of platform memory abstraction
 *
 * Maps platform_malloc/calloc/free to ESP-IDF heap_caps_* functions.
 * Translates PLATFORM_MEM_CAP_* flags to MALLOC_CAP_* equivalents.
 */

#include "platform_memory.h"
#include "esp_heap_caps.h"

/**
 * @brief Convert platform memory capability flags to ESP-IDF MALLOC_CAP_* flags
 *
 * @param caps Platform capability flags (ORed PLATFORM_MEM_CAP_*)
 * @return ESP-IDF capability flags (ORed MALLOC_CAP_*)
 */
static uint32_t platform_to_esp32_caps(uint32_t caps) {
    uint32_t esp_caps = 0;
    
    if (caps & PLATFORM_MEM_CAP_DEFAULT) {
        esp_caps |= MALLOC_CAP_DEFAULT;
    }
    if (caps & PLATFORM_MEM_CAP_8BIT) {
        esp_caps |= MALLOC_CAP_8BIT;
    }
    if (caps & PLATFORM_MEM_CAP_DMA) {
        esp_caps |= MALLOC_CAP_DMA;
    }
    if (caps & PLATFORM_MEM_CAP_SPIRAM) {
        esp_caps |= MALLOC_CAP_SPIRAM;
    }
    
    // Fallback to DEFAULT if no caps specified
    if (esp_caps == 0) {
        esp_caps = MALLOC_CAP_DEFAULT;
    }
    
    return esp_caps;
}

void *platform_malloc(size_t size, uint32_t caps) {
    uint32_t esp_caps = platform_to_esp32_caps(caps);
    return heap_caps_malloc(size, esp_caps);
}

void *platform_calloc(size_t count, size_t size, uint32_t caps) {
    uint32_t esp_caps = platform_to_esp32_caps(caps);
    return heap_caps_calloc(count, size, esp_caps);
}

void platform_free(void *ptr) {
    heap_caps_free(ptr);  // NULL-safe
}
