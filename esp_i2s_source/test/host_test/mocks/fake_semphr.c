/* FreeRTOS semaphore mock for host tests.
 * Supports mutex and binary semaphores with configurable behavior.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdlib.h>
#include <stdio.h>

typedef struct {
    int  type; /* 0 = mutex, 1 = binary */
    int  count; /* for binary: 0 = empty, 1 = signaled */
} mock_sem_t;

/* Configurable behavior for binary semaphore waits */
static int s_binary_wait_result = pdFALSE; /* default: timeout */

SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    mock_sem_t *s = (mock_sem_t *)malloc(sizeof(mock_sem_t));
    if (s) {
        s->type = 0; /* mutex */
        s->count = 1; /* mutex initially taken=false (count=1 means available) */
    }
    return (SemaphoreHandle_t)s;
}

SemaphoreHandle_t xSemaphoreCreateBinary(void)
{
    mock_sem_t *s = (mock_sem_t *)malloc(sizeof(mock_sem_t));
    if (s) {
        s->type = 1; /* binary */
        s->count = 0; /* binary initially empty */
    }
    return (SemaphoreHandle_t)s;
}

int xSemaphoreTake(SemaphoreHandle_t xSemaphore, unsigned xTicksToWait)
{
    mock_sem_t *s = (mock_sem_t *)xSemaphore;
    if (!s) return pdFALSE;

    if (s->type == 0) {
        /* Mutex: always succeed */
        return pdTRUE;
    }

    /* Binary semaphore: configurable behavior */
    if (s_binary_wait_result == pdTRUE) {
        /* Simulate success: return immediately */
        return pdTRUE;
    } else {
        /* Simulate timeout: return pdFALSE */
        return pdFALSE;
    }
}

int xSemaphoreGive(SemaphoreHandle_t xSemaphore)
{
    mock_sem_t *s = (mock_sem_t *)xSemaphore;
    if (!s) return pdFAIL;
    return pdTRUE;
}

void vSemaphoreDelete(SemaphoreHandle_t xSemaphore)
{
    free(xSemaphore);
}

void mock_sem_set_binary_wait_result(int result)
{
    s_binary_wait_result = result;
}

void mock_sem_reset(void)
{
    s_binary_wait_result = pdFALSE;
}