/* FreeRTOS queue mock for host tests.
 * Implements a real FIFO queue with configurable depth.
 */
#include "freertos/queue.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct {
    unsigned   depth;
    unsigned   item_size;
    unsigned   head;
    unsigned   tail;
    char       *buf;
    unsigned   count;
} mock_queue_t;

static mock_queue_t *create_queue(unsigned depth, unsigned item_size)
{
    mock_queue_t *q = (mock_queue_t *)malloc(sizeof(mock_queue_t));
    if (!q) return NULL;
    q->depth = depth;
    q->item_size = item_size;
    q->buf = (char *)malloc(depth * item_size);
    if (!q->buf) {
        free(q);
        return NULL;
    }
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    return q;
}

QueueHandle_t xQueueCreate(unsigned depth, unsigned item_size)
{
    if (depth == 0 || item_size == 0) return NULL;
    return (QueueHandle_t)create_queue(depth, item_size);
}

BaseType_t xQueueSend(QueueHandle_t xQueue, const void *item, TickType_t xTicksToWait)
{
    mock_queue_t *q = (mock_queue_t *)xQueue;
    if (!q || !item) return pdFAIL;

    if (q->count >= q->depth) {
        (void)xTicksToWait;
        /* Queue is full — timeout means fail.
         * In real FreeRTOS, the task would block, but in host tests we simulate
         * by checking if the timeout is 0 (non-blocking) or any timeout —
         * for host tests, we always fail immediately if full. */
        return pdFAIL;
    }

    /* Copy item to buffer */
    unsigned pos = q->head;
    memcpy(q->buf + pos * q->item_size, item, q->item_size);
    q->head = (q->head + 1) % q->depth;
    q->count++;
    return pdPASS;
}

BaseType_t xQueueReceive(QueueHandle_t xQueue, void *item, TickType_t xTicksToWait)
{
    mock_queue_t *q = (mock_queue_t *)xQueue;
    if (!q || !item) return pdFAIL;

    if (q->count == 0) {
        (void)xTicksToWait;
        return pdFAIL; /* Queue empty */
    }

    /* Copy item from buffer */
    unsigned pos = q->tail;
    memcpy(item, q->buf + pos * q->item_size, q->item_size);
    q->tail = (q->tail + 1) % q->depth;
    q->count--;
    return pdPASS;
}

void vQueueDelete(QueueHandle_t xQueue)
{
    mock_queue_t *q = (mock_queue_t *)xQueue;
    if (q) {
        free(q->buf);
        free(q);
    }
}

unsigned uxQueueMessagesWaiting(QueueHandle_t xQueue)
{
    mock_queue_t *q = (mock_queue_t *)xQueue;
    if (!q) return 0;
    return q->count;
}

void mock_queue_reset(void)
{
    /* No global state to reset */
}