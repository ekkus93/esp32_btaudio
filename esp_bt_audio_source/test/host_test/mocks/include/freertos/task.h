// Minimal task.h stub for host tests
#ifndef MOCK_FREERTOS_TASK_H
#define MOCK_FREERTOS_TASK_H

#include "FreeRTOS.h"

typedef void* TaskHandle_t;
typedef int BaseType_t;

BaseType_t xTaskCreate(void (*task)(void*), const char* name, unsigned stackDepth, void* params, unsigned uxPriority, TaskHandle_t* outHandle);
void vTaskDelete(TaskHandle_t task);
const char* pcTaskGetName(TaskHandle_t xTaskToQuery);
void taskYIELD(void);
TickType_t xTaskGetTickCount(void);
void mock_task_set_tick(uint32_t ticks);
void vTaskSuspendAll(void);
BaseType_t xTaskResumeAll(void);

#endif // MOCK_FREERTOS_TASK_H
#pragma once

#include <stdint.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Host-mode vTaskDelay replacement. We ignore the current task handle and
 * simply sleep for the requested duration to preserve relative timing.
 */
static inline void vTaskDelay(const TickType_t ticks)
{
    (void)ticks;
    /* Host-mode delay is a no-op to keep unit tests fast. */
}

#ifdef __cplusplus
}
#endif
