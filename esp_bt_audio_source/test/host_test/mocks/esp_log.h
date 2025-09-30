// Minimal mock of esp_log.h for host unit tests
#ifndef _TEST_MOCK_ESP_LOG_H_
#define _TEST_MOCK_ESP_LOG_H_

#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <time.h>

// Basic ESP log level constants (values chosen to match common esp-idf headers)
#define ESP_LOG_NONE    0
#define ESP_LOG_ERROR   1
#define ESP_LOG_WARN    2
#define ESP_LOG_INFO    3
#define ESP_LOG_DEBUG   4
#define ESP_LOG_VERBOSE 5

// Provide LOG_FORMAT macro used by bt_common.h.
// The real esp-idf LOG_FORMAT prepends timestamp and tag placeholders. The
// BT code calls esp_log_write(..., LOG_FORMAT(...), esp_log_timestamp(), tag, ...)
// so the format must consume the timestamp and tag as the first two varargs.
// Keep it simple but safe for host tests by prepending "%u %s " to the
// user-provided format string.
#define LOG_FORMAT(LEVEL, FMT) ("%u %s " FMT)

// Timestamp helper used by BT macros (real returns ms since boot in ESP-IDF)
static inline unsigned int esp_log_timestamp(void)
{
    return (unsigned int)time(NULL);
}

// Simple esp_log_write implementation that prints to stdout/stderr.
static inline void esp_log_write(int level, const char* tag, const char* format, ...)
{
    va_list ap;
    va_start(ap, format);
    FILE* out = (level <= ESP_LOG_WARN) ? stderr : stdout;
    switch (level) {
        case ESP_LOG_ERROR: fprintf(out, "[E/%s] ", tag); break;
        case ESP_LOG_WARN:  fprintf(out, "[W/%s] ", tag); break;
        case ESP_LOG_INFO:  fprintf(out, "[I/%s] ", tag); break;
        case ESP_LOG_DEBUG: fprintf(out, "[D/%s] ", tag); break;
        case ESP_LOG_VERBOSE: fprintf(out, "[V/%s] ", tag); break;
        default: fprintf(out, "[?/%s] ", tag); break;
    }
    vfprintf(out, format, ap);
    fprintf(out, "\n");
    va_end(ap);
}

// Convenience macros used directly in some code paths
#define ESP_LOGE(fmt, ...) do { esp_log_write(ESP_LOG_ERROR, "", fmt, ##__VA_ARGS__); } while (0)
#define ESP_LOGW(fmt, ...) do { esp_log_write(ESP_LOG_WARN,  "", fmt, ##__VA_ARGS__); } while (0)
#define ESP_LOGI(fmt, ...) do { esp_log_write(ESP_LOG_INFO,  "", fmt, ##__VA_ARGS__); } while (0)
#define ESP_LOGD(fmt, ...) do { esp_log_write(ESP_LOG_DEBUG, "", fmt, ##__VA_ARGS__); } while (0)
#define ESP_LOGV(fmt, ...) do { esp_log_write(ESP_LOG_VERBOSE, "", fmt, ##__VA_ARGS__); } while (0)

// Provide a TAG-accepting hex-dump stub if code calls it directly
#define ESP_LOG_BUFFER_HEXDUMP(TAG, BUF, LEN, ARGS) do { \
    (void)TAG; (void)BUF; (void)LEN; (void)ARGS; \
} while(0)

#endif // _TEST_MOCK_ESP_LOG_H_
