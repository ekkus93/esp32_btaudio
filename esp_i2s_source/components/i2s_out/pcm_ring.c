/*
 * pcm_ring — lock-free SPSC byte ring (SIG-1b). See pcm_ring.h.
 */
#include "pcm_ring.h"

#include <stdlib.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

struct pcm_ring {
    uint8_t *buf;
    size_t   size;      /* internal buffer size = capacity + 1 (wasted slot) */
    size_t   capacity;  /* usable bytes */
    volatile size_t head;  /* producer write index, [0, size) */
    volatile size_t tail;  /* consumer read index,  [0, size) */
    size_t   peak;      /* max used seen (producer-updated stat) */
};

static void *ring_alloc(size_t n, bool use_psram)
{
#ifdef ESP_PLATFORM
    if (use_psram) {
        void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (p) return p;
        /* fall back to internal RAM if PSRAM is unavailable */
    }
    return heap_caps_malloc(n, MALLOC_CAP_8BIT);
#else
    (void)use_psram;
    return malloc(n);
#endif
}

pcm_ring_t *pcm_ring_create(size_t capacity, bool use_psram)
{
    if (capacity == 0) return NULL;
    pcm_ring_t *r = calloc(1, sizeof(*r));
    if (!r) return NULL;
    r->size = capacity + 1;      /* one wasted slot for full/empty */
    r->capacity = capacity;
    r->buf = ring_alloc(r->size, use_psram);
    if (!r->buf) {
        free(r);
        return NULL;
    }
    r->head = r->tail = 0;
    r->peak = 0;
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
    size_t head = r->head;
    size_t tail = r->tail;                    /* snapshot consumer index */
    size_t used = used_of(head, tail, r->size);
    size_t space = r->capacity - used;        /* usable free bytes */
    size_t n = (len < space) ? len : space;

    size_t first = r->size - head;            /* bytes to end of buffer */
    if (first > n) first = n;
    memcpy(r->buf + head, src, first);
    if (n > first) memcpy(r->buf, src + first, n - first);

    r->head = (head + n) % r->size;           /* publish */

    size_t new_used = used + n;
    if (new_used > r->peak) r->peak = new_used;
    return n;
}

size_t pcm_ring_read(pcm_ring_t *r, uint8_t *dst, size_t len)
{
    if (!r || !dst || len == 0) return 0;
    size_t head = r->head;                    /* snapshot producer index */
    size_t tail = r->tail;
    size_t used = used_of(head, tail, r->size);
    size_t n = (len < used) ? len : used;

    size_t first = r->size - tail;
    if (first > n) first = n;
    memcpy(dst, r->buf + tail, first);
    if (n > first) memcpy(dst + first, r->buf, n - first);

    r->tail = (tail + n) % r->size;           /* publish */
    return n;
}

size_t pcm_ring_used(const pcm_ring_t *r)
{
    if (!r) return 0;
    return used_of(r->head, r->tail, r->size);
}

size_t pcm_ring_free(const pcm_ring_t *r)
{
    if (!r) return 0;
    return r->capacity - used_of(r->head, r->tail, r->size);
}

size_t pcm_ring_capacity(const pcm_ring_t *r) { return r ? r->capacity : 0; }
size_t pcm_ring_peak_used(const pcm_ring_t *r) { return r ? r->peak : 0; }

void pcm_ring_reset(pcm_ring_t *r)
{
    if (!r) return;
    r->tail = r->head;
    r->peak = 0;
}
