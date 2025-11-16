// Minimal host-side queue implementation for unit tests
#include "include/freertos/queue.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    UBaseType_t capacity;
    unsigned item_size;
    unsigned used;
    void* storage;
} mock_queue_t;

QueueHandle_t xQueueCreate(UBaseType_t uxQueueLength, unsigned uxItemSize)
{
    mock_queue_t* q = (mock_queue_t*)malloc(sizeof(mock_queue_t));
    if (!q) return NULL;
    q->capacity = uxQueueLength;
    q->item_size = uxItemSize;
    q->used = 0;
    q->storage = malloc((size_t)uxQueueLength * uxItemSize);
    if (!q->storage) { free(q); return NULL; }
    return (QueueHandle_t)q;
}

int xQueueSend(QueueHandle_t xQueue, const void* pvItemToQueue, unsigned xTicksToWait)
{
    (void)xTicksToWait;
    if (!xQueue || !pvItemToQueue) return pdFALSE;
    mock_queue_t* q = (mock_queue_t*)xQueue;
    if (q->used >= q->capacity) return pdFALSE;
    void* dest = (uint8_t*)q->storage + ((size_t)q->used * q->item_size);
    memcpy(dest, pvItemToQueue, q->item_size);
    q->used++;
    return pdTRUE;
}

int xQueueReceive(QueueHandle_t xQueue, void* pvBuffer, unsigned xTicksToWait)
{
    (void)xTicksToWait;
    if (!xQueue || !pvBuffer) return pdFALSE;
    mock_queue_t* q = (mock_queue_t*)xQueue;
    if (q->used == 0) return pdFALSE;
    /* Pop first item */
    void* src = q->storage;
    memcpy(pvBuffer, src, q->item_size);
    /* Shift remaining */
    q->used--;
    if (q->used > 0) memmove(q->storage, (uint8_t*)q->storage + q->item_size, (size_t)q->used * q->item_size);
    return pdTRUE;
}

void vQueueDelete(QueueHandle_t xQueue)
{
    if (!xQueue) return;
    mock_queue_t* q = (mock_queue_t*)xQueue;
    free(q->storage);
    free(q);
}

UBaseType_t uxQueueSpacesAvailable(QueueHandle_t xQueue)
{
    if (!xQueue) return 0;
    mock_queue_t* q = (mock_queue_t*)xQueue;
    return q->capacity - q->used;
}

UBaseType_t uxQueueMessagesWaiting(QueueHandle_t xQueue)
{
    if (!xQueue) return 0;
    mock_queue_t* q = (mock_queue_t*)xQueue;
    return q->used;
}
