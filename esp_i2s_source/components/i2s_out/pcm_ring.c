/*
 * pcm_ring — lock-free SPSC byte ring (SIG-1b). See pcm_ring.h.
 *
 * C11 atomics (_Atomic size_t with acquire/release) provide the cross-core
 * visibility required for a valid SPSC ring. Plain volatile does not: the C
 * standard does not guarantee atomicity, ordering, or data-race freedom for
 * volatile accesses on shared memory across cores.
 */
#include "pcm_ring.h"

#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <stdint.h>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

struct pcm_ring {
    uint8_t *buf;
    size_t   size;      /* internal buffer size = capacity + 1 (wasted slot) */
    size_t   capacity;  /* usable bytes */
    _Atomic size_t head;  /* producer write index, [0, size) */
    _Atomic size_t tail;  /* consumer read index,  [0, size) */
    _Atomic size_t peak;  /* max used seen (producer-updated stat) */
};

static void *ring_alloc(size_t n, pcm_ring_memory_t memory)
{
#ifdef ESP_PLATFORM
    if (memory == PCM_RING_PSRAM_REQUIRED || memory == PCM_RING_PSRAM_PREFERRED) {
        void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (p != NULL || memory == PCM_RING_PSRAM_REQUIRED) {
            return p;   /* REQUIRED: NULL on PSRAM failure, never fall back */
        }
        /* PREFERRED: fall back to internal RAM if PSRAM is unavailable */
    }
    return heap_caps_malloc(n, MALLOC_CAP_8BIT);
#else
    (void)memory;
    return malloc(n);
#endif
}

pcm_ring_t *pcm_ring_create(size_t capacity, pcm_ring_memory_t memory)
{
    if (capacity == 0 || capacity == SIZE_MAX) return NULL;
    pcm_ring_t *r = calloc(1, sizeof(*r));
    if (!r) return NULL;
    r->size = capacity + 1;      /* one wasted slot for full/empty */
    r->capacity = capacity;
    r->buf = ring_alloc(r->size, memory);
    if (!r->buf) {
        free(r);
        return NULL;
    }
    atomic_init(&r->head, 0);
    atomic_init(&r->tail, 0);
    atomic_init(&r->peak, 0);
    return r;
}

void pcm_ring_destroy(pcm_ring_t *r)
{
    if (!r) return;
#ifdef ESP_PLATFORM
    heap_caps_free(r->buf);
#else
    free(r->buf);
#endif
    free(r);
}

static inline size_t used_of(size_t head, size_t tail, size_t size)
{
    return (head + size - tail) % size;
}

size_t pcm_ring_write(pcm_ring_t *r, const uint8_t *src, size_t len)
{
    if (!r || !src || len == 0) return 0;
    size_t head = atomic_load_explicit(&r->head, memory_order_relaxed);
    size_t tail = atomic_load_explicit(&r->tail, memory_order_acquire);
    size_t used = used_of(head, tail, r->size);
    size_t space = r->capacity - used;        /* usable free bytes */
    size_t n = (len < space) ? len : space;

    size_t first = r->size - head;            /* bytes to end of buffer */
    if (first > n) first = n;
    memcpy(r->buf + head, src, first);
    if (n > first) memcpy(r->buf, src + first, n - first);

    atomic_store_explicit(&r->head, (head + n) % r->size,
                          memory_order_release);

    /* Atomically update peak (CAS loop) */
    size_t new_used = used + n;
    size_t peak = atomic_load_explicit(&r->peak, memory_order_relaxed);
    while (new_used > peak &&
           !atomic_compare_exchange_weak_explicit(
               &r->peak, &peak, new_used,
               memory_order_relaxed, memory_order_relaxed));
    return n;
}

size_t pcm_ring_read(pcm_ring_t *r, uint8_t *dst, size_t len)
{
    if (!r || !dst || len == 0) return 0;
    size_t head = atomic_load_explicit(&r->head, memory_order_acquire);
    size_t tail = atomic_load_explicit(&r->tail, memory_order_relaxed);
    size_t used = used_of(head, tail, r->size);
    size_t n = (len < used) ? len : used;

    size_t first = r->size - tail;
    if (first > n) first = n;
    memcpy(dst, r->buf + tail, first);
    if (n > first) memcpy(dst + first, r->buf, n - first);

    atomic_store_explicit(&r->tail, (tail + n) % r->size,
                          memory_order_release);
    return n;
}

size_t pcm_ring_peek(const pcm_ring_t *r, uint8_t *dst, size_t len)
{
    if (!r || !dst || len == 0) return 0;
    size_t head = atomic_load_explicit(&r->head, memory_order_acquire);
    size_t tail = atomic_load_explicit(&r->tail, memory_order_relaxed);
    size_t used = used_of(head, tail, r->size);
    size_t n = (len < used) ? len : used;

    size_t first = r->size - tail;
    if (first > n) first = n;
    memcpy(dst, r->buf + tail, first);
    if (n > first) memcpy(dst + first, r->buf, n - first);
    return n;
}

size_t pcm_ring_consume(pcm_ring_t *r, size_t len)
{
    if (!r || len == 0) return 0;
    size_t head = atomic_load_explicit(&r->head, memory_order_acquire);
    size_t tail = atomic_load_explicit(&r->tail, memory_order_relaxed);
    size_t used = used_of(head, tail, r->size);
    size_t n = (len < used) ? len : used;

    atomic_store_explicit(&r->tail, (tail + n) % r->size,
                          memory_order_release);
    return n;
}

size_t pcm_ring_used(const pcm_ring_t *r)
{
    if (!r) return 0;
    size_t head = atomic_load_explicit(&r->head, memory_order_acquire);
    size_t tail = atomic_load_explicit(&r->tail, memory_order_acquire);
    return used_of(head, tail, r->size);
}

size_t pcm_ring_free(const pcm_ring_t *r)
{
    if (!r) return 0;
    return r->capacity - pcm_ring_used(r);
}

size_t pcm_ring_capacity(const pcm_ring_t *r) { return r ? r->capacity : 0; }

size_t pcm_ring_peak_used(const pcm_ring_t *r)
{
    if (!r) return 0;
    return atomic_load_explicit(&r->peak, memory_order_relaxed);
}

void pcm_ring_reset(pcm_ring_t *r)
{
    if (!r) return;
    /* Consumer-side reset: align tail to head, clear peak. */
    size_t head = atomic_load_explicit(&r->head, memory_order_acquire);
    atomic_store_explicit(&r->tail, head, memory_order_release);
    atomic_store_explicit(&r->peak, 0, memory_order_relaxed);
}
