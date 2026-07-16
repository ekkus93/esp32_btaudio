/*
 * web_ui_internal.h — non-public glue shared between the web_ui translation
 * units (web_ui.c registrar + web_ui_{wifi,audio,radio,bt}.c feature handlers).
 * Not part of the web_ui public API — do not include outside this component.
 */
#pragma once

#include "esp_http_server.h"
#include "cJSON.h"

/* Read the full request body into buf (NUL-terminated). Defined in web_ui.c.
 * Deprecated — use web_read_json() instead. */
esp_err_t recv_body(httpd_req_t *req, char *buf, size_t buf_sz);

/* Strict JSON-body helper (10.2). Parses the HTTP body into a cJSON tree.
 * max_bytes is the upper bound on the body length. Caller must free via
 * web_json_free(). */
typedef struct {
    char *body;
    cJSON *root;
} web_json_body_t;

esp_err_t web_read_json(httpd_req_t *req, size_t max_bytes, web_json_body_t *out);
void      web_json_free(web_json_body_t *body);

/* Centralised JSON error/success responses (10.3).
 * http_status is a string such as "400 Bad Request" or "500 Internal Server Error". */
esp_err_t web_send_error(httpd_req_t *req, const char *http_status,
                         const char *code, const char *message,
                         bool retryable);
esp_err_t web_send_ok(httpd_req_t *req, const char *extra_json);

/* ---- Feature handlers, registered by web_ui_start() ---- */

/* web_ui_wifi.c */
esp_err_t wifi_post(httpd_req_t *req);
esp_err_t apmode_post_h(httpd_req_t *req);

/* web_ui_audio.c */
esp_err_t tone_post(httpd_req_t *req);
esp_err_t tone_delete(httpd_req_t *req);
esp_err_t volume_post_h(httpd_req_t *req);
esp_err_t prebuffer_post_h(httpd_req_t *req);

/* web_ui_radio.c */
esp_err_t radio_post(httpd_req_t *req);
esp_err_t radio_delete(httpd_req_t *req);
esp_err_t stations_get_h(httpd_req_t *req);
esp_err_t stations_post_h(httpd_req_t *req);
esp_err_t stations_put_h(httpd_req_t *req);
esp_err_t stations_delete_h(httpd_req_t *req);

/* web_ui_bt.c */
esp_err_t scan_post_h(httpd_req_t *req);
esp_err_t btvolume_get_h(httpd_req_t *req);
esp_err_t btvolume_post_h(httpd_req_t *req);
esp_err_t ctrl_get_h(httpd_req_t *req);
esp_err_t ctrl_post_h(httpd_req_t *req);
esp_err_t bt_get_h(httpd_req_t *req);
esp_err_t bt_post_h(httpd_req_t *req);
esp_err_t console_post_h(httpd_req_t *req);

/* Initialise the BT state module: create the mutex, subscribe the bt_link
 * async-line handler, and prime the paired list. Call once from web_ui_start()
 * after httpd is up. Defined in web_ui_bt.c. */
void web_ui_bt_init(void);

/* web_ui_auth.c — bearer-token authentication */
esp_err_t web_ui_auth_init(void);
esp_err_t web_ui_auth_generate_token(void);
bool      web_ui_auth_check(httpd_req_t *req);
const char *web_ui_auth_get_token(void);
