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
void mock_task_reset(void);
unsigned mock_task_create_count(void);
unsigned mock_task_delete_count(void);
TaskHandle_t mock_task_last_handle(void);
const char* mock_task_last_name(void);
BaseType_t mock_task_last_result(void);
void mock_task_set_create_result(BaseType_t result);
void mock_task_set_fail_on_nth(unsigned n); /* fail on Nth creation (1-based), 0 disables */
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

/* Phase 7.5: Task notification stubs for host tests.
 * ulTaskNotifyTake clears and returns the task's notification value.
 * For host tests this returns 0 (no notification pending). */
static inline unsigned long ulTaskNotifyTake(BaseType_t xClearToZero, TickType_t xTimeout)
{
    (void)xClearToZero;
    (void)xTimeout;
    return 0UL;
}

/* xTaskNotifyGive sends a notification to a task.
 * For host tests this is a no-op. */
static inline BaseType_t xTaskNotifyGive(TaskHandle_t xTaskToNotify)
{
    (void)xTaskToNotify;
    return pdPASS;
}

#ifdef __cplusplus
}
#endif
