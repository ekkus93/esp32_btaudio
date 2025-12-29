#include "freertos/semphr.h"

/* Provide missing semaphore deletion stub for host builds. */
void vSemaphoreDelete(SemaphoreHandle_t xSemaphore)
{
    (void)xSemaphore;
}
