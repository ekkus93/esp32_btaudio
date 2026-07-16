/*
 * Mock EventGroup for host tests (RH-S3-03 radio lifecycle tests).
 * Provides just enough state for session exit signalling.
 */
#include "freertos/event_groups.h"
#include "freertos/FreeRTOS.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    EventBits_t bits;
} mock_event_group_t;

static int s_event_group_create_null = 0;

EventGroupHandle_t xEventGroupCreate(void)
{
    if (s_event_group_create_null > 0) {
        s_event_group_create_null--;
        return NULL;
    }
    mock_event_group_t *eg = (mock_event_group_t *)malloc(sizeof(*eg));
    if (eg) memset(eg, 0, sizeof(*eg));
    return (EventGroupHandle_t)eg;
}

void vEventGroupDelete(EventGroupHandle_t eg)
{
    free(eg);
}

EventBits_t xEventGroupSetBits(EventGroupHandle_t eg, EventBits_t uxBits)
{
    if (!eg) return 0;
    mock_event_group_t *e = (mock_event_group_t *)eg;
    EventBits_t prev = e->bits;
    e->bits |= uxBits;
    return prev;
}

EventBits_t xEventGroupWaitBits(EventGroupHandle_t eg,
                                 EventBits_t xBitsToWaitFor,
                                 BaseType_t xClearBitsOnExit,
                                 BaseType_t xWaitForAllBits,
                                 TickType_t xTicksToWait)
{
    (void)xTicksToWait; /* immediate — host test controls when bits are set */
    (void)xWaitForAllBits;
    (void)xBitsToWaitFor; /* mock returns current bits regardless of mask */
    if (!eg) return 0;
    mock_event_group_t *e = (mock_event_group_t *)eg;
    EventBits_t current = e->bits;
    if (xClearBitsOnExit) e->bits = 0;
    return current;
}

EventBits_t xEventGroupGetBits(EventGroupHandle_t eg)
{
    if (!eg) return 0;
    mock_event_group_t *e = (mock_event_group_t *)eg;
    return e->bits;
}

void mock_event_group_set_create_null(int n)
{
    s_event_group_create_null = n;
}

void mock_event_group_reset(void)
{
    s_event_group_create_null = 0;
}
