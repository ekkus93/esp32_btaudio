/*
 * Minimal event_groups.h stub for host tests.
 */
#ifndef MOCK_EVENT_GROUPS_H
#define MOCK_EVENT_GROUPS_H

#include "FreeRTOS.h"

#include <stdint.h>
#include <stddef.h>

typedef void *EventGroupHandle_t;

/* BIT macros — already defined in FreeRTOS.h, but guard for standalone use */
#ifndef BIT0
#define BIT0 ((EventBits_t)1)
#endif
#ifndef BIT1
#define BIT1 ((EventBits_t)2)
#endif

EventGroupHandle_t xEventGroupCreate(void);
void vEventGroupDelete(EventGroupHandle_t eg);
EventBits_t xEventGroupSetBits(EventGroupHandle_t eg, EventBits_t uxBits);
EventBits_t xEventGroupWaitBits(EventGroupHandle_t eg,
                                EventBits_t xBitsToWaitFor,
                                BaseType_t xClearBitsOnExit,
                                BaseType_t xWaitForAllBits,
                                TickType_t xTicksToWait);
EventBits_t xEventGroupGetBits(EventGroupHandle_t eg);

#endif
