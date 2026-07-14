// Minimal FreeRTOS queue.h stub for host tests
#ifndef MOCK_FREERTOS_QUEUE_H
#define MOCK_FREERTOS_QUEUE_H

#include "FreeRTOS.h"

typedef void* QueueHandle_t;

/* Create a queue with `depth` items of `item_size` bytes each. */
QueueHandle_t xQueueCreate(unsigned depth, unsigned item_size);

/* Send an item to the queue. Returns pdPASS (1) on success, pdFAIL (0) if queue is full. */
BaseType_t xQueueSend(QueueHandle_t xQueue, const void *item, TickType_t xTicksToWait);

/* Receive an item from the queue. Returns pdPASS (1) on success, pdFAIL (0) if queue is empty. */
BaseType_t xQueueReceive(QueueHandle_t xQueue, void *item, TickType_t xTicksToWait);

/* Delete a queue. */
void vQueueDelete(QueueHandle_t xQueue);

/* Return the number of items in the queue. */
unsigned uxQueueMessagesWaiting(QueueHandle_t xQueue);

/* Mock control */
void mock_queue_set_create_null(int n); /* return NULL for next N queue creations */
void mock_queue_reset(void);

#endif /* MOCK_FREERTOS_QUEUE_H */