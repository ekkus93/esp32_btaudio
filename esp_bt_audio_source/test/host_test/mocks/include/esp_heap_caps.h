/* Minimal host-side stub of esp_heap_caps.h for unit tests */
#ifndef TEST_HOST_ESP_HEAP_CAPS_H
#define TEST_HOST_ESP_HEAP_CAPS_H

#include <stdlib.h>
#include <stddef.h>

/* capacity flags (values don't matter for the host stub) */
#define MALLOC_CAP_DEFAULT 0
#define MALLOC_CAP_SPIRAM  1
#define MALLOC_CAP_INTERNAL 2

static inline void *heap_caps_malloc_prefer(size_t size, int preferred, int caps1, int caps2)
{
    (void)preferred; (void)caps1; (void)caps2;
    return malloc(size);
}

static inline void *heap_caps_calloc_prefer(size_t nmemb, size_t size, int preferred, int caps1, int caps2)
{
    (void)preferred; (void)caps1; (void)caps2;
    return calloc(nmemb, size);
}

#endif /* TEST_HOST_ESP_HEAP_CAPS_H */
