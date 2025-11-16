// Minimal queue.h stub for host tests
#ifndef MOCK_FREERTOS_QUEUE_H
#define MOCK_FREERTOS_QUEUE_H

#include "FreeRTOS.h"

typedef void* QueueHandle_t;

QueueHandle_t xQueueCreate(UBaseType_t uxQueueLength, unsigned uxItemSize);
int xQueueSend(QueueHandle_t xQueue, const void* pvItemToQueue, unsigned xTicksToWait);
int xQueueReceive(QueueHandle_t xQueue, void* pvBuffer, unsigned xTicksToWait);
void vQueueDelete(QueueHandle_t xQueue);
UBaseType_t uxQueueSpacesAvailable(QueueHandle_t xQueue);
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t xQueue);

#endif // MOCK_FREERTOS_QUEUE_H
#pragma once

#include <stdint.h>
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void * QueueHandle_t;

typedef struct {
    uint32_t dummy;
} StaticQueue_t;

#ifdef __cplusplus
}
#endif
