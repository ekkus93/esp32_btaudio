#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Host-mode tick configuration mirrors a 1 kHz RTOS tick for simplicity. */
#ifndef configTICK_RATE_HZ
#define configTICK_RATE_HZ 1000U
#endif

#ifndef portTICK_PERIOD_MS
#define portTICK_PERIOD_MS (1000U / configTICK_RATE_HZ)
#endif

typedef uint32_t TickType_t;

typedef int BaseType_t;
typedef unsigned int UBaseType_t;

#define pdMS_TO_TICKS(ms) ((TickType_t)((ms) / portTICK_PERIOD_MS))

#ifdef __cplusplus
}
#endif
