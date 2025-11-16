// Simple host stubs for semaphore APIs
#include "include/freertos/semphr.h"
#include <stdlib.h>

SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    void* p = malloc(1);
    return (SemaphoreHandle_t)p;
}

int xSemaphoreTake(SemaphoreHandle_t xSemaphore, unsigned xTicksToWait)
{
    (void)xSemaphore; (void)xTicksToWait; return 1; /* pdTRUE */
}

int xSemaphoreGive(SemaphoreHandle_t xSemaphore)
{
    (void)xSemaphore; return 1; /* pdTRUE */
}
