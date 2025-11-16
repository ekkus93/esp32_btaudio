// Simple stub for esp_backtrace_print
#include "include/esp_debug_helpers.h"
#include <stdio.h>

void esp_backtrace_print(int max_frames)
{
    (void)max_frames;
    /* No-op for host tests */
}
