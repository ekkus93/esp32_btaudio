#include <stdio.h>
#include "include/esp_log.h"

static int current_level = 3; // default INFO

void esp_log_level_set(const char* tag, int level)
{
    (void)tag;
    current_level = level;
}

int esp_log_level_get(const char* tag)
{
    (void)tag;
    return current_level;
}
