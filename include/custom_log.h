#ifndef CUSTOM_LOG_H
#define CUSTOM_LOG_H

#include "esp_log.h"

// Modify format string to use %lu for unsigned long
#define SAFE_LOG_FORMAT(letter, format)  LOG_COLOR_ ## letter #letter " (%lu) %s: " format LOG_RESET_COLOR "\n"

#define SAFE_ESP_LOG_LEVEL(level, tag, format, ...) do {                            \
    if (level==ESP_LOG_ERROR )          { esp_log_write(ESP_LOG_ERROR,      tag, SAFE_LOG_FORMAT(E, format), (unsigned long)esp_log_timestamp(), tag, ##__VA_ARGS__); } \
    else if (level==ESP_LOG_WARN )      { esp_log_write(ESP_LOG_WARN,       tag, SAFE_LOG_FORMAT(W, format), (unsigned long)esp_log_timestamp(), tag, ##__VA_ARGS__); } \
    else if (level==ESP_LOG_DEBUG )     { esp_log_write(ESP_LOG_DEBUG,      tag, SAFE_LOG_FORMAT(D, format), (unsigned long)esp_log_timestamp(), tag, ##__VA_ARGS__); } \
    else if (level==ESP_LOG_VERBOSE )   { esp_log_write(ESP_LOG_VERBOSE,    tag, SAFE_LOG_FORMAT(V, format), (unsigned long)esp_log_timestamp(), tag, ##__VA_ARGS__); } \
    else                                { esp_log_write(ESP_LOG_INFO,       tag, SAFE_LOG_FORMAT(I, format), (unsigned long)esp_log_timestamp(), tag, ##__VA_ARGS__); } \
} while(0)

#define SAFE_ESP_LOGE(tag, format, ...)  SAFE_ESP_LOG_LEVEL(ESP_LOG_ERROR,   tag, format, ##__VA_ARGS__)
#define SAFE_ESP_LOGW(tag, format, ...)  SAFE_ESP_LOG_LEVEL(ESP_LOG_WARN,    tag, format, ##__VA_ARGS__)
#define SAFE_ESP_LOGI(tag, format, ...)  SAFE_ESP_LOG_LEVEL(ESP_LOG_INFO,    tag, format, ##__VA_ARGS__)
#define SAFE_ESP_LOGD(tag, format, ...)  SAFE_ESP_LOG_LEVEL(ESP_LOG_DEBUG,   tag, format, ##__VA_ARGS__)
#define SAFE_ESP_LOGV(tag, format, ...)  SAFE_ESP_LOG_LEVEL(ESP_LOG_VERBOSE, tag, format, ##__VA_ARGS__)

#endif // CUSTOM_LOG_H
