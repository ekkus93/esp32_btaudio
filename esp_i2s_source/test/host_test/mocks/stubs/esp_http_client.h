/* Stub esp_http_client.h for host tests */
#ifndef STUB_ESP_HTTP_CLIENT_H
#define STUB_ESP_HTTP_CLIENT_H
#include "esp_err.h"

/* Handle type — matches ESP-IDF convention (void *) */
typedef void *esp_http_client_handle_t;

/* Event types */
#define HTTP_EVENT_ON_HEADER 0

/* Event structure */
typedef struct {
    int event_id;
    const char *header_key;
    const char *header_value;
} esp_http_client_event_t;

/* Config structure */
typedef struct {
    const char *url;
    int timeout_ms;
    esp_err_t (*event_handler)(esp_http_client_event_t *);
    esp_err_t (*crt_bundle_attach)(void *conf);
    const char *user_agent;
    int buffer_size;
} esp_http_client_config_t;

esp_http_client_handle_t esp_http_client_init(void *cfg);
esp_err_t esp_http_client_open(esp_http_client_handle_t h, int code);
esp_err_t esp_http_client_fetch_headers(esp_http_client_handle_t h);
int esp_http_client_read(esp_http_client_handle_t h, char *buf, int len);
esp_err_t esp_http_client_close(esp_http_client_handle_t h);
void esp_http_client_cleanup(esp_http_client_handle_t h);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t h, const char *key, const char *val);
int esp_http_client_get_status_code(esp_http_client_handle_t h);

#endif
