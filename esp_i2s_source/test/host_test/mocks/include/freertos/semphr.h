// Minimal FreeRTOS semphr.h stub for host tests
#ifndef MOCK_FREERTOS_SEMPRH_H
#define MOCK_FREERTOS_SEMPRH_H

#include <stddef.h>

typedef void* SemaphoreHandle_t;

/* Create a mutex (returns non-NULL handle) */
SemaphoreHandle_t xSemaphoreCreateMutex(void);

/* Create a binary semaphore (initially empty) */
SemaphoreHandle_t xSemaphoreCreateBinary(void);

int xSemaphoreTake(SemaphoreHandle_t xSemaphore, unsigned xTicksToWait);
int xSemaphoreGive(SemaphoreHandle_t xSemaphore);
void vSemaphoreDelete(SemaphoreHandle_t xSemaphore);

/* Mock control */
void mock_sem_set_binary_wait_result(int result);
void mock_sem_set_mutex_null(int n); /* return NULL for next N mutex creations */
void mock_sem_reset(void);

/* Critical section macros no-op in host tests */
#define portENTER_CRITICAL(x)
#define portEXIT_CRITICAL(x)

#endif // MOCK_FREERTOS_SEMPRH_H