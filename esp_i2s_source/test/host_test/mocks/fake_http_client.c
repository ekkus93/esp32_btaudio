/*
 * fake_http_client.c — see fake_http_client.h. Implements the
 * mocks/stubs/esp_http_client.h + esp_crt_bundle.h API surface that
 * radio_stream.c links against.
 */
#include "fake_http_client.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"

#include <stdbool.h>
#include <string.h>

#define FAKE_HTTP_MAX_HOPS    8
#define FAKE_HTTP_MAX_HEADERS 8

typedef struct {
    int status;
    fake_http_header_t headers[FAKE_HTTP_MAX_HEADERS];
    int header_count;
} fake_http_hop_t;

typedef struct {
    esp_err_t (*event_handler)(esp_http_client_event_t *);
    int hop_index;
} fake_http_handle_t;

static fake_http_hop_t   s_hops[FAKE_HTTP_MAX_HOPS];
static int               s_hop_count;
static int               s_hop_cursor;
static bool               s_force_init_null;
static esp_err_t          s_force_open_err = ESP_OK;
static fake_http_handle_t s_handle;

void fake_http_client_reset(void)
{
    memset(s_hops, 0, sizeof(s_hops));
    s_hop_count = 0;
    s_hop_cursor = 0;
    s_force_init_null = false;
    s_force_open_err = ESP_OK;
    memset(&s_handle, 0, sizeof(s_handle));
}

void fake_http_client_queue_hop(int status, const fake_http_header_t *headers,
                                int header_count)
{
    if (s_hop_count >= FAKE_HTTP_MAX_HOPS) return;
    fake_http_hop_t *h = &s_hops[s_hop_count++];
    h->status = status;
    h->header_count = (header_count > FAKE_HTTP_MAX_HEADERS) ? FAKE_HTTP_MAX_HEADERS : header_count;
    for (int i = 0; i < h->header_count; i++) {
        h->headers[i] = headers[i];
    }
}

int fake_http_client_hops_consumed(void)
{
    return s_hop_cursor;
}

void fake_http_client_force_init_null(void)
{
    s_force_init_null = true;
}

void fake_http_client_force_open_err(esp_err_t err)
{
    s_force_open_err = err;
}

esp_http_client_handle_t esp_http_client_init(void *cfgv)
{
    if (s_force_init_null) {
        s_force_init_null = false;
        return NULL;
    }
    esp_http_client_config_t *cfg = (esp_http_client_config_t *)cfgv;
    s_handle.event_handler = cfg ? cfg->event_handler : NULL;
    s_handle.hop_index = s_hop_cursor++;
    return (esp_http_client_handle_t)&s_handle;
}

esp_err_t esp_http_client_open(esp_http_client_handle_t h, int code)
{
    (void)h; (void)code;
    if (s_force_open_err != ESP_OK) {
        esp_err_t e = s_force_open_err;
        s_force_open_err = ESP_OK;
        return e;
    }
    return ESP_OK;
}

esp_err_t esp_http_client_fetch_headers(esp_http_client_handle_t hv)
{
    fake_http_handle_t *h = (fake_http_handle_t *)hv;
    if (!h || h->hop_index >= s_hop_count) return ESP_OK;
    fake_http_hop_t *hop = &s_hops[h->hop_index];
    if (h->event_handler) {
        for (int i = 0; i < hop->header_count; i++) {
            esp_http_client_event_t evt = {
                .event_id = HTTP_EVENT_ON_HEADER,
                .header_key = hop->headers[i].key,
                .header_value = hop->headers[i].value,
            };
            h->event_handler(&evt);
        }
    }
    return ESP_OK;
}

int esp_http_client_get_status_code(esp_http_client_handle_t hv)
{
    fake_http_handle_t *h = (fake_http_handle_t *)hv;
    if (!h || h->hop_index >= s_hop_count) return 0;
    return s_hops[h->hop_index].status;
}

int esp_http_client_read(esp_http_client_handle_t h, char *buf, int len)
{
    (void)h; (void)buf; (void)len;
    return 0;
}

esp_err_t esp_http_client_close(esp_http_client_handle_t h)
{
    (void)h;
    return ESP_OK;
}

void esp_http_client_cleanup(esp_http_client_handle_t h)
{
    (void)h;
}

esp_err_t esp_http_client_set_header(esp_http_client_handle_t h, const char *key, const char *val)
{
    (void)h; (void)key; (void)val;
    return ESP_OK;
}

esp_err_t esp_crt_bundle_attach(void *cfg)
{
    (void)cfg;
    return ESP_OK;
}
