// Simple host-side task stubs
#include "include/freertos/task.h"
#include <stdint.h>

typedef struct {
    BaseType_t create_result;
    unsigned create_count;
    unsigned delete_count;
    TaskHandle_t last_handle;
    const char *last_name;
    void (*last_fn)(void *);
    void *last_param;
    unsigned last_stack_depth;
    unsigned last_priority;
} mock_task_state_t;

static mock_task_state_t s_state = {.create_result = pdPASS};
/* Fail on Nth task creation (1-based). 0 means no fail-on-n. */
static unsigned s_fail_on_nth_create = 0;
static uint32_t s_mock_tick = 0;

static TaskHandle_t make_handle(unsigned ordinal)
{
    return (TaskHandle_t)(uintptr_t)(ordinal + 1U);
}

BaseType_t xTaskCreate(void (*task)(void *), const char *name, unsigned stackDepth, void *params, unsigned uxPriority, TaskHandle_t *outHandle)
{
    s_state.create_count++;
    s_state.last_name = name;
    s_state.last_fn = task;
    s_state.last_param = params;
    s_state.last_stack_depth = stackDepth;
    s_state.last_priority = uxPriority;

    /* Check fail-on-Nth hook first, then global result. */
    BaseType_t result = s_state.create_result;
    if (s_fail_on_nth_create && s_state.create_count == s_fail_on_nth_create) {
        result = pdFAIL;
    }

    if (result == pdPASS && outHandle) {
        s_state.last_handle = make_handle(s_state.create_count);
        *outHandle = s_state.last_handle;
    } else {
        s_state.last_handle = NULL;
        if (outHandle) {
            *outHandle = NULL;
        }
    }

    return result;
}

void vTaskDelete(TaskHandle_t task)
{
    if (task) {
        s_state.delete_count++;
    }
}

const char *pcTaskGetName(TaskHandle_t xTaskToQuery)
{
    (void)xTaskToQuery;
    return "host_task";
}

void taskYIELD(void) { /* no-op */ }

void mock_task_set_tick(uint32_t ticks)
{
    s_mock_tick = ticks;
}

uint32_t xTaskGetTickCount(void)
{
    return s_mock_tick;
}

void mock_task_reset(void)
{
    s_state.create_result = pdPASS;
    s_state.create_count = 0;
    s_state.delete_count = 0;
    s_state.last_handle = NULL;
    s_state.last_name = NULL;
    s_state.last_fn = NULL;
    s_state.last_param = NULL;
    s_state.last_stack_depth = 0;
    s_state.last_priority = 0;
    s_fail_on_nth_create = 0;
    s_mock_tick = 0;
}

unsigned mock_task_create_count(void)
{
    return s_state.create_count;
}

unsigned mock_task_delete_count(void)
{
    return s_state.delete_count;
}

TaskHandle_t mock_task_last_handle(void)
{
    return s_state.last_handle;
}

const char *mock_task_last_name(void)
{
    return s_state.last_name;
}

BaseType_t mock_task_last_result(void)
{
    return s_state.create_result;
}

void mock_task_set_create_result(BaseType_t result)
{
    s_state.create_result = result;
}

/* Fail on Nth task creation (1-based). 0 disables. */
void mock_task_set_fail_on_nth(unsigned n)
{
    s_fail_on_nth_create = n;
}

// Host build stub for critical section helpers used by audio_processor
void vTaskSuspendAll(void) { /* no-op for host */ }

BaseType_t xTaskResumeAll(void) { return pdTRUE; }
