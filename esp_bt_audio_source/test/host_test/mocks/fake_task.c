// Simple host-side task stubs
#include "include/freertos/task.h"
#include <stdlib.h>
#include <string.h>

BaseType_t xTaskCreate(void (*task)(void*), const char* name, unsigned stackDepth, void* params, unsigned uxPriority, TaskHandle_t* outHandle)
{
    (void)task; (void)name; (void)stackDepth; (void)params; (void)uxPriority;
    if (outHandle) {
        *outHandle = malloc(1);
    }
    return pdPASS;
}

void vTaskDelete(TaskHandle_t task)
{
    if (task) free(task);
}

const char* pcTaskGetName(TaskHandle_t xTaskToQuery)
{
    (void)xTaskToQuery;
    return "host_task";
}

void taskYIELD(void) { /* no-op */ }

uint32_t xTaskGetTickCount(void) { return 0; }

// Host build stub for critical section helpers used by audio_processor
void vTaskSuspendAll(void) { /* no-op for host */ }

BaseType_t xTaskResumeAll(void) { return pdTRUE; }
