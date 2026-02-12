/**
 * @file portmacro.h
 * @brief Mock FreeRTOS portmacro.h for host unit tests
 *
 * Provides minimal mocks for FreeRTOS port macros needed by audio_span_log.c
 * Note: portMUX_TYPE is already defined in FreeRTOS.h as int
 */

#ifndef PORTMACRO_H
#define PORTMACRO_H

#include <stdint.h>

/* portMUX_TYPE is already defined in FreeRTOS.h - don't redefine */

/* Mock portMUX initializer */
#ifndef portMUX_INITIALIZER_UNLOCKED
#define portMUX_INITIALIZER_UNLOCKED 0
#endif

/* Mock critical section macros - no-op for single-threaded tests */
#ifndef portENTER_CRITICAL
#define portENTER_CRITICAL(mux)  do { (void)(mux); } while(0)
#endif

#ifndef portEXIT_CRITICAL
#define portEXIT_CRITICAL(mux)   do { (void)(mux); } while(0)
#endif

#endif /* PORTMACRO_H */
