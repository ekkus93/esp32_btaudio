/*
 * fake_http_client.h — controllable fake esp_http_client for radio_stream.c
 * host tests (I2S-1, docs/UNIT_TESTS2_TODO.md).
 *
 * Unlike the trivial single-response stub in test_radio_lifecycle.c, this
 * fake models a SEQUENCE of HTTP responses ("hops") so a test can script an
 * entire redirect chain: each call to esp_http_client_init() consumes the
 * next queued hop. esp_http_client_fetch_headers() replays that hop's
 * queued headers through the real event_handler callback captured at
 * esp_http_client_init() time — exactly mirroring how the real esp_http_client
 * drives radio_stream.c's http_evt() to populate its static header state
 * (icy-metaint, content-type, icy-br, icy-name, location).
 */
#ifndef FAKE_HTTP_CLIENT_H
#define FAKE_HTTP_CLIENT_H

#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *key;
    const char *value;
} fake_http_header_t;

/* Clear all queued hops and forced-failure flags. Call in setUp(). */
void fake_http_client_reset(void);

/* Queue the next hop's response: HTTP status code + header_count headers
 * (each replayed as one HTTP_EVENT_ON_HEADER event during fetch_headers()).
 * Hops are consumed in FIFO order, one per esp_http_client_init() call. */
void fake_http_client_queue_hop(int status, const fake_http_header_t *headers,
                                int header_count);

/* Number of esp_http_client_init() calls made since the last reset — i.e.
 * how many hops have been (or are being) consumed. */
int fake_http_client_hops_consumed(void);

/* Force the NEXT esp_http_client_init() call to return NULL (alloc failure),
 * then auto-clears. */
void fake_http_client_force_init_null(void);

/* Force the NEXT esp_http_client_open() call to return `err` instead of
 * ESP_OK, then auto-clears. */
void fake_http_client_force_open_err(esp_err_t err);

#ifdef __cplusplus
}
#endif

#endif /* FAKE_HTTP_CLIENT_H */
