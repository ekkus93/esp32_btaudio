// Minimal host-side stub of FreeRTOS ringbuf API used by audio_processor.c
#ifndef MOCK_FREERTOS_RINGBUF_H
#define MOCK_FREERTOS_RINGBUF_H

#include <stddef.h>
#include <stdint.h>

typedef void* RingbufHandle_t;
typedef enum { RINGBUF_TYPE_BYTEBUF = 0, RINGBUF_TYPE_NOSPLIT = 1 } RingbufferType_t;

RingbufHandle_t xRingbufferCreateWithCaps(size_t xBufferSize, RingbufferType_t xBufferType, unsigned uxMemoryCaps);
RingbufHandle_t xRingbufferCreate(size_t xBufferSize, RingbufferType_t xBufferType);
void vRingbufferDelete(RingbufHandle_t handle);
size_t xRingbufferGetMaxItemSize(RingbufHandle_t handle);
size_t xRingbufferGetCurFreeSize(RingbufHandle_t handle);
void* xRingbufferReceive(RingbufHandle_t handle, size_t* item_size, unsigned ticks_to_wait);
void* xRingbufferReceiveUpTo(RingbufHandle_t handle, size_t* item_size, unsigned ticks_to_wait, size_t max_size);
int xRingbufferSend(RingbufHandle_t handle, const void* item, size_t size, unsigned ticks_to_wait);
void vRingbufferReturnItem(RingbufHandle_t handle, void* item);

#endif // MOCK_FREERTOS_RINGBUF_H
