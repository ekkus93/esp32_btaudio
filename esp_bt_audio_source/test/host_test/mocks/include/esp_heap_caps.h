// Host mock of esp_heap_caps.h (placed in mocks include path)
#ifndef MOCK_ESP_HEAP_CAPS_H
#define MOCK_ESP_HEAP_CAPS_H

#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>

/* Minimal MALLOC_CAP_* flags used by the code under test.
 * Values match typical ESP-IDF conventions and are treated as bitflags.
 */
#define MALLOC_CAP_DEFAULT  (0x1)
#define MALLOC_CAP_SPIRAM   (0x2)
#define MALLOC_CAP_8BIT     (0x4)

void* heap_caps_malloc(size_t size, unsigned caps);
void heap_caps_free(void* ptr);
size_t heap_caps_get_free_size(unsigned caps);

void esp_heap_caps_mock_set_psram_available(bool available);
/* Query helpers added for tests */
bool esp_heap_caps_mock_was_allocated_from_spiram(void* ptr);
size_t esp_heap_caps_mock_count_allocations_spiram(void);
size_t esp_heap_caps_mock_count_allocations_dram(void);
void esp_heap_caps_mock_reset_allocations(void);

/* Convenience helpers that mirror ESP-IDF heap_caps API variants used in
 * production code. Implemented in the mock C file so allocations are
 * recorded for tests.
 */
void *heap_caps_malloc_prefer(size_t size, int preferred, int caps1, int caps2);
void *heap_caps_calloc_prefer(size_t nmemb, size_t size, int preferred, int caps1, int caps2);

#endif // MOCK_ESP_HEAP_CAPS_H
