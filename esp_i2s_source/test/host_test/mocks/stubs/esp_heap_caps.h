/* Stub esp_heap_caps.h for host tests */
#ifndef STUB_ESP_HEAP_CAPS_H
#define STUB_ESP_HEAP_CAPS_H
#include <stddef.h>
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_DEFAULT 0
void *esp_heap_caps_malloc(size_t size, int caps);
void esp_heap_caps_free(void *ptr);
/* Alias used by radio.c */
#define heap_caps_malloc esp_heap_caps_malloc
#endif
