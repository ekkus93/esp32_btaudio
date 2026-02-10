/**
 * @file platform_memory.h
 * @brief Platform abstraction for memory allocation with capability flags
 *
 * This header provides a unified interface for memory allocation across:
 * - ESP32: Maps to heap_caps_malloc/free with memory capability flags
 * - Host: Maps to standard malloc/calloc/free (ignores capability flags)
 *
 * Eliminates platform-specific #ifdefs from application code.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Memory capability flags (platform-agnostic)
 *
 * On ESP32: Maps directly to MALLOC_CAP_* flags from esp_heap_caps.h
 * On Host: Ignored (all allocations use standard heap)
 */
typedef enum {
    PLATFORM_MEM_CAP_DEFAULT = 0x01,   ///< Default internal RAM
    PLATFORM_MEM_CAP_8BIT    = 0x02,   ///< 8-bit accessible memory (most common)
    PLATFORM_MEM_CAP_DMA     = 0x04,   ///< DMA-capable memory (required for I2S)
    PLATFORM_MEM_CAP_SPIRAM  = 0x08,   ///< External PSRAM (large buffers)
} platform_mem_caps_t;

/**
 * @brief Allocate memory with platform-specific capability flags
 *
 * @param size Number of bytes to allocate
 * @param caps Memory capability flags (ORed together)
 *             - ESP32: Uses heap_caps_malloc() with MALLOC_CAP_* equivalents
 *             - Host: Uses standard malloc(), ignores caps
 *
 * @return Pointer to allocated memory, or NULL on failure
 *
 * @note Caller must free returned pointer with platform_free()
 *
 * Examples:
 *   - platform_malloc(1024, PLATFORM_MEM_CAP_8BIT)
 *   - platform_malloc(512, PLATFORM_MEM_CAP_DMA)
 *   - platform_malloc(8192, PLATFORM_MEM_CAP_SPIRAM | PLATFORM_MEM_CAP_8BIT)
 */
void *platform_malloc(size_t size, uint32_t caps);

/**
 * @brief Allocate and zero-initialize memory with platform-specific capability flags
 *
 * @param count Number of elements
 * @param size Size of each element in bytes
 * @param caps Memory capability flags (ORed together)
 *             - ESP32: Uses heap_caps_calloc() with MALLOC_CAP_* equivalents
 *             - Host: Uses standard calloc(), ignores caps
 *
 * @return Pointer to zero-initialized memory, or NULL on failure
 *
 * @note Caller must free returned pointer with platform_free()
 *
 * Examples:
 *   - platform_calloc(10, sizeof(uint32_t), PLATFORM_MEM_CAP_8BIT)
 *   - platform_calloc(1, 4096, PLATFORM_MEM_CAP_SPIRAM)
 */
void *platform_calloc(size_t count, size_t size, uint32_t caps);

/**
 * @brief Free memory allocated by platform_malloc() or platform_calloc()
 *
 * @param ptr Pointer to memory block (NULL is safe, will be ignored)
 *            - ESP32: Uses heap_caps_free()
 *            - Host: Uses standard free()
 *
 * @note Safe to call with NULL pointer (no-op)
 */
void platform_free(void *ptr);

#ifdef __cplusplus
}
#endif
