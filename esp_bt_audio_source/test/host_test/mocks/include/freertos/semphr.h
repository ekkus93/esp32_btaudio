// Minimal FreeRTOS semphr.h stub for host tests
#ifndef MOCK_FREERTOS_SEMPRH_H
#define MOCK_FREERTOS_SEMPRH_H

#include <stddef.h>

typedef void* SemaphoreHandle_t;

/* Create a mutex (returns non-NULL handle) */
SemaphoreHandle_t xSemaphoreCreateMutex(void);
int xSemaphoreTake(SemaphoreHandle_t xSemaphore, unsigned xTicksToWait);
int xSemaphoreGive(SemaphoreHandle_t xSemaphore);

/* Critical section macros no-op in host tests */
#define portENTER_CRITICAL(x)
#define portEXIT_CRITICAL(x)

#endif // MOCK_FREERTOS_SEMPRH_H
