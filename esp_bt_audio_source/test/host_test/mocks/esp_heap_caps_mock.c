// Host-side mock for heap_caps_* APIs used by unit tests
#include "esp_heap_caps.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static bool s_psram_available = true;

/* Track allocations so tests can assert whether memory came from PSRAM or DRAM */
typedef struct alloc_rec {
    void* ptr;
    size_t size;
    unsigned caps;
    bool from_spiram;
    struct alloc_rec* next;
} alloc_rec_t;

static alloc_rec_t* s_alloc_list = NULL;

static void record_alloc(void* ptr, size_t size, unsigned caps, bool from_spiram)
{
    alloc_rec_t* r = (alloc_rec_t*)malloc(sizeof(alloc_rec_t));
    if (!r) return;
    r->ptr = ptr; r->size = size; r->caps = caps; r->from_spiram = from_spiram;
    r->next = s_alloc_list; s_alloc_list = r;
}

static void remove_alloc(void* ptr)
{
    alloc_rec_t** cur = &s_alloc_list;
    while (*cur) {
        if ((*cur)->ptr == ptr) {
            alloc_rec_t* rem = *cur;
            *cur = rem->next;
            free(rem);
            return;
        }
        cur = &(*cur)->next;
    }
}

bool esp_heap_caps_mock_was_allocated_from_spiram(void* ptr)
{
    alloc_rec_t* it = s_alloc_list;
    while (it) {
        if (it->ptr == ptr) return it->from_spiram;
        it = it->next;
    }
    return false; /* unknown pointers are treated as non-PSRAM */
}

size_t esp_heap_caps_mock_count_allocations_spiram(void)
{
    size_t c = 0; alloc_rec_t* it = s_alloc_list;
    while (it) { if (it->from_spiram) c++; it = it->next; } return c;
}

size_t esp_heap_caps_mock_count_allocations_dram(void)
{
    size_t c = 0; alloc_rec_t* it = s_alloc_list;
    while (it) { if (!it->from_spiram) c++; it = it->next; } return c;
}

void esp_heap_caps_mock_reset_allocations(void)
{
    alloc_rec_t* it = s_alloc_list;
    while (it) { alloc_rec_t* n = it->next; free(it); it = n; }
    s_alloc_list = NULL;
}

void esp_heap_caps_mock_set_psram_available(bool available)
{
    s_psram_available = available;
}

/* Provide a host-side esp_psram_is_initialized() so production code that
 * queries runtime PSRAM state can be exercised in native unit tests. This
 * mirrors the real API and returns the mock-configured PSRAM availability.
 */
bool esp_psram_is_initialized(void)
{
    return s_psram_available;
}

void* heap_caps_malloc(size_t size, unsigned caps)
{
    (void)caps;
    /* If PSRAM requested but mock is configured as unavailable, fail */
    bool spiram_requested = (caps & MALLOC_CAP_SPIRAM) != 0;
    if (spiram_requested && !s_psram_available) {
        return NULL;
    }

    void* p = malloc(size);
    if (p == NULL) return NULL;
    bool allocated_from_spiram = spiram_requested && s_psram_available;
    record_alloc(p, size, caps, allocated_from_spiram);
    return p;
}

void heap_caps_free(void* ptr)
{
    remove_alloc(ptr);
    free(ptr);
}

void *heap_caps_malloc_prefer(size_t size, int preferred, int caps1, int caps2)
{
    (void)preferred;
    unsigned caps = MALLOC_CAP_DEFAULT;
    if ((caps1 & MALLOC_CAP_SPIRAM) || (caps2 & MALLOC_CAP_SPIRAM)) {
        caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    } else {
        caps = MALLOC_CAP_DEFAULT | MALLOC_CAP_8BIT;
    }
    /* Try preferred caps first (SPIRAM if requested), otherwise fallback
     * will be handled by callers that try alternative caps when NULL is
     * returned. */
    return heap_caps_malloc(size, caps);
}

void *heap_caps_calloc_prefer(size_t nmemb, size_t size, int preferred, int caps1, int caps2)
{
    size_t total = nmemb * size;
    void* p = heap_caps_malloc_prefer(total, preferred, caps1, caps2);
    if (p) memset(p, 0, total);
    return p;
}

size_t heap_caps_get_free_size(unsigned caps)
{
    (void)caps;
    /* Host can't easily report free heap per-capability; return a large
     * value so tests won't treat small frees as allocations failures. */
    return (size_t)1024 * 1024 * 1024; /* 1 GiB */
}
