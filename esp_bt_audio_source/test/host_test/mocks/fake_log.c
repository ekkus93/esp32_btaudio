#include <stdio.h>
#define ESP_LOG_LEVEL_IMPL
#include "include/esp_log.h"

int g_mock_log_level = ESP_LOG_INFO;

void esp_log_level_set(const char* tag, int level)
{
    (void)tag;
    g_mock_log_level = level;
}

int esp_log_level_get(const char* tag)
{
    (void)tag;
    return g_mock_log_level;
}
