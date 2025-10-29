#pragma once

#include <stdint.h>
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void * QueueHandle_t;

typedef struct {
    uint32_t dummy;
} StaticQueue_t;

#ifdef __cplusplus
}
#endif
