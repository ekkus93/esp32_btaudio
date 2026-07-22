/* Stub esp_heap_caps.h for host tests */
#ifndef STUB_ESP_HEAP_CAPS_H
#define STUB_ESP_HEAP_CAPS_H
#include <stddef.h>
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_DEFAULT 0
#define MALLOC_CAP_8BIT 0
void *esp_heap_caps_malloc(size_t size, int caps);
void esp_heap_caps_free(void *ptr);
/* Aliases used by radio.c / pcm_ring.c */
#define heap_caps_malloc esp_heap_caps_malloc
#define heap_caps_free esp_heap_caps_free
#endif
