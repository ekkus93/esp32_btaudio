/* Stub esp_log.h for host tests */
#ifndef STUB_ESP_LOG_H
#define STUB_ESP_LOG_H
/* (void)(tag) keeps callers' `static const char *TAG` referenced so -Wextra
 * strict builds don't flag it as unused just because logging is a no-op
 * on host. */
#define ESP_LOGI(tag, fmt, ...) do { (void)(tag); } while(0)
#define ESP_LOGW(tag, fmt, ...) do { (void)(tag); } while(0)
#define ESP_LOGE(tag, fmt, ...) do { (void)(tag); } while(0)
#define ESP_LOGD(tag, fmt, ...) do { (void)(tag); } while(0)
#endif
