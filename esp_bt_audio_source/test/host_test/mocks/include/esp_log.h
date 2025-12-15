// Minimal mock of esp_log.h for host unit tests
#ifndef _TEST_MOCK_ESP_LOG_H_
#define _TEST_MOCK_ESP_LOG_H_

#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <time.h>

// Basic ESP log level constants
#define ESP_LOG_NONE    0
#define ESP_LOG_ERROR   1
#define ESP_LOG_WARN    2
#define ESP_LOG_INFO    3
#define ESP_LOG_DEBUG   4
#define ESP_LOG_VERBOSE 5

#define LOG_FORMAT(LEVEL, FMT) ("%u %s " FMT)

static inline unsigned int esp_log_timestamp(void)
{
    return (unsigned int)time(NULL);
}

/* Host-visible log level control used by production code. Backed by a single
 * global so all translation units share the same state instead of each TU
 * keeping its own static copy. */
extern int g_mock_log_level;

#ifndef ESP_LOG_LEVEL_IMPL
static inline void esp_log_level_set(const char* tag, int level)
{
    (void)tag;
    g_mock_log_level = level;
}

static inline int esp_log_level_get(const char* tag)
{
    (void)tag;
    return g_mock_log_level;
}
#endif

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

#define ESP_LOGE(TAG, fmt, ...) do { esp_log_write(ESP_LOG_ERROR, (TAG), fmt, ##__VA_ARGS__); } while (0)
#define ESP_LOGW(TAG, fmt, ...) do { esp_log_write(ESP_LOG_WARN,  (TAG), fmt, ##__VA_ARGS__); } while (0)
#define ESP_LOGI(TAG, fmt, ...) do { esp_log_write(ESP_LOG_INFO,  (TAG), fmt, ##__VA_ARGS__); } while (0)
#define ESP_LOGD(TAG, fmt, ...) do { esp_log_write(ESP_LOG_DEBUG, (TAG), fmt, ##__VA_ARGS__); } while (0)
#define ESP_LOGV(TAG, fmt, ...) do { esp_log_write(ESP_LOG_VERBOSE, (TAG), fmt, ##__VA_ARGS__); } while (0)

#define ESP_LOG_BUFFER_HEXDUMP(TAG, BUF, LEN, ARGS) do { \
    (void)TAG; (void)BUF; (void)LEN; (void)ARGS; \
} while(0)

#endif // _TEST_MOCK_ESP_LOG_H_
