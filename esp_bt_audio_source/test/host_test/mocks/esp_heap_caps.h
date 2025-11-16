// Simple host-side heap_caps mock header for unit tests
#ifndef TEST_HOST_ESP_HEAP_CAPS_H
#define TEST_HOST_ESP_HEAP_CAPS_H

#include <stdlib.h>
#include <stddef.h>
#include <stdbool.h>

/* Minimal MALLOC_CAP_* flags used by the code under test */
#define MALLOC_CAP_DEFAULT  (0x1)
#define MALLOC_CAP_SPIRAM   (0x2)
#define MALLOC_CAP_8BIT     (0x4)

/* Allocate memory with the given capability flags. On the host this
 * is a simple wrapper around malloc which can be configured to pretend
 * PSRAM is available or not. */
void* heap_caps_malloc(size_t size, unsigned caps);
void heap_caps_free(void* ptr);
size_t heap_caps_get_free_size(unsigned caps);

/* Test helper: control whether PSRAM-capable allocations succeed. If
 * true, allocations requested with MALLOC_CAP_SPIRAM will succeed and
 * be treated differently by the mock (for diagnostics). If false,
 * allocations with MALLOC_CAP_SPIRAM will fail (return NULL) and the
 * caller should fall back to default allocation. */
void esp_heap_caps_mock_set_psram_available(bool available);

#endif // TEST_HOST_ESP_HEAP_CAPS_H
