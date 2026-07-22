// Minimal FreeRTOS.h stub for host tests
#ifndef MOCK_FREERTOS_H
#define MOCK_FREERTOS_H

#include <stdint.h>

/* Basic tick and base types - choose uint32_t to match common ESP-IDF
 * FreeRTOS config and avoid conflicts with other headers. */
typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef unsigned int UBaseType_t;

/* Port/RTOS macros used by the production code */
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdFAIL 0
#define portMAX_DELAY ((TickType_t)0xffffffffU)

/* BIT macros for event groups */
#define BIT0 ((uint32_t)1)
#define BIT1 ((uint32_t)2)
#define BIT(x) ((uint32_t)1 << (x))

/* Critical sections are no-ops on host: single-threaded test execution,
 * no real interrupts to disable. */
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define taskENTER_CRITICAL(mux) ((void)(mux))
#define taskEXIT_CRITICAL(mux) ((void)(mux))

#include <assert.h>
#define configASSERT(x) assert(x)

/* Task priority */
#define tskIDLE_PRIORITY 0
#define configMAX_PRIORITIES 10

/* Helper to convert milliseconds to ticks for host tests */
#ifndef configTICK_RATE_HZ
#define configTICK_RATE_HZ 1000U
#endif
#define portTICK_PERIOD_MS (1000U / configTICK_RATE_HZ)
#define pdMS_TO_TICKS(ms) ((TickType_t)((uint32_t)(ms) / portTICK_PERIOD_MS))

/* Forward declarations for event group types (used by radio.c).
 * These are normally in freertos/event_groups.h but radio.c relies on
 * ESP-IDF's implicit includes. Provide them here for host tests. */
typedef void *EventGroupHandle_t;
typedef uint32_t EventBits_t;

#endif // MOCK_FREERTOS_H
