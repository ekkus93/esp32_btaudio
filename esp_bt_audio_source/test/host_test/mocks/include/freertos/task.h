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
