/* esp_timer mock for host tests. Provides esp_timer_get_time(). */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Static inline mock — returns a monotonically increasing time in microseconds. */
static inline int64_t esp_timer_get_time(void)
{
    static int64_t s_mock_time = 0;
    s_mock_time += 500000; /* advance ~500 ms per call (CTRL_LOOP_MS + drift) */
    return s_mock_time;
}

#ifdef __cplusplus
}
#endif
