// Minimal host-side ringbuffer implementation for unit tests.
#include "include/freertos/ringbuf.h"
#include "esp_heap_caps.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    size_t capacity;
    size_t used;
    uint8_t* data;
} mock_ringbuf_t;

RingbufHandle_t xRingbufferCreateWithCaps(size_t xBufferSize, RingbufferType_t xBufferType, unsigned uxMemoryCaps)
{
    (void)xBufferType;
    /* Allocate ringbuffer struct with default allocator (small) and
     * allocate the backing data buffer with the requested memory caps so
     * tests can observe whether PSRAM was requested via the heap_caps
     * mock. */
    mock_ringbuf_t* r = (mock_ringbuf_t*)malloc(sizeof(mock_ringbuf_t));
    if (!r) return NULL;
    r->capacity = xBufferSize;
    r->used = 0;
    /* Use heap_caps_malloc so PSRAM-capable allocations are recorded by the
     * esp_heap_caps_mock implementation used in host tests. If heap_caps
     * isn't available, fall back to malloc (heap_caps_malloc is provided
     * by the mocks in the test environment). */
    r->data = (uint8_t*)heap_caps_malloc(xBufferSize, uxMemoryCaps ? uxMemoryCaps : MALLOC_CAP_DEFAULT);
    if (!r->data) { free(r); return NULL; }
    return (RingbufHandle_t)r;
}

RingbufHandle_t xRingbufferCreate(size_t xBufferSize, RingbufferType_t xBufferType)
{
    return xRingbufferCreateWithCaps(xBufferSize, xBufferType, 0);
}

void vRingbufferDelete(RingbufHandle_t handle)
{
    if (!handle) return;
    mock_ringbuf_t* r = (mock_ringbuf_t*)handle;
    free(r->data);
    free(r);
}

size_t xRingbufferGetMaxItemSize(RingbufHandle_t handle)
{
    if (!handle) return 0;
    mock_ringbuf_t* r = (mock_ringbuf_t*)handle;
    return r->capacity;
}

size_t xRingbufferGetCurFreeSize(RingbufHandle_t handle)
{
    if (!handle) return 0;
    mock_ringbuf_t* r = (mock_ringbuf_t*)handle;
    return r->capacity - r->used;
}

void* xRingbufferReceive(RingbufHandle_t handle, size_t* item_size, unsigned ticks_to_wait)
{
    (void)ticks_to_wait;
    if (!handle || !item_size) return NULL;
    mock_ringbuf_t* r = (mock_ringbuf_t*)handle;
    if (r->used == 0) { *item_size = 0; return NULL; }
    /* Return pointer to data (caller must return it) */
    /* Simulate consumption: return up to the full used size and reduce
     * the mock's used count to reflect that the item has been taken.
     * In the real FreeRTOS ringbuffer the caller must call
     * vRingbufferReturnItem() to free the memory; our lightweight mock
     * models this by adjusting used on receive so tests observing
     * free-size see the expected change. */
    *item_size = r->used;
    r->used = 0;
    return r->data;
}

void* xRingbufferReceiveUpTo(RingbufHandle_t handle, size_t* item_size, unsigned ticks_to_wait, size_t max_size)
{
    (void)ticks_to_wait;
    if (!handle || !item_size) return NULL;
    mock_ringbuf_t* r = (mock_ringbuf_t*)handle;
    if (r->used == 0) { *item_size = 0; return NULL; }
    size_t to_copy = r->used < max_size ? r->used : max_size;
    *item_size = to_copy;
    /* Reduce used to simulate that bytes have been removed from the
     * ring buffer. This mirrors the effective behavior tests expect
     * (free size increases after a receive/return cycle). */
    if (to_copy >= r->used) r->used = 0;
    else r->used -= to_copy;
    return r->data;
}

int xRingbufferSend(RingbufHandle_t handle, const void* item, size_t size, unsigned ticks_to_wait)
{
    (void)ticks_to_wait;
    if (!handle || !item) return 0; /* pdFALSE */
    mock_ringbuf_t* r = (mock_ringbuf_t*)handle;
    if (size > r->capacity) return 0;
    /* Store beginning of buffer and set used to size */
    memcpy(r->data, item, size);
    r->used = size;
    return 1; /* pdTRUE */
}

void vRingbufferReturnItem(RingbufHandle_t handle, void* item)
{
    (void)handle; (void)item;
    /* No-op: consumption is simulated on receive in this mock. */
}
