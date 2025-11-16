// Simple esp_timer stub implementations for host tests
#include "include/esp_timer.h"
#include <stdint.h>

uint64_t esp_timer_get_time(void)
{
    return 0; /* deterministic */
}
