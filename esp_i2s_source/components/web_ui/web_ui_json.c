/*
 * web_ui_json — strict JSON-body reader and centralised error/success helpers.
 * Every endpoint uses this instead of ad-hoc recv_body() + cJSON_Parse.
 *
 * Envelope format for responses:
 *   {"ok":true,"data":...}          on success
 *   {"ok":false,"error":{"code":"...","message":"...","retryable":false}}  on error
 */
#include "web_ui_internal.h"

#include <string.h>
#include <stdio.h>

#include "esp_http_server.h"
#include "cJSON.h"

esp_err_t web_read_json(httpd_req_t *req, size_t max_bytes, web_json_body_t *out)
{
    if (req == NULL || out == NULL) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    if (req->content_len <= 0 || (size_t)req->content_len > max_bytes) {
        return ESP_ERR_INVALID_SIZE;
    }

    out->body = calloc((size_t)req->content_len + 1, 1);
    if (out->body == NULL) return ESP_ERR_NO_MEM;

    size_t offset = 0;
    while (offset < (size_t)req->content_len) {
        int n = httpd_req_recv(req, out->body + offset,
                               (size_t)req->content_len - offset);
        if (n <= 0) {
            free(out->body);
            memset(out, 0, sizeof(*out));
            return ESP_ERR_TIMEOUT;
        }
        offset += (size_t)n;
    }

    out->root = cJSON_ParseWithLength(out->body, offset);
    if (out->root == NULL) {
        free(out->body);
        memset(out, 0, sizeof(*out));
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

void web_json_free(web_json_body_t *body)
{
    if (body == NULL) return;
    cJSON_Delete(body->root);
    free(body->body);
    memset(body, 0, sizeof(*body));
}

/* ---- Centralised JSON error/success responses ---- */

static esp_err_t web_send_json(httpd_req_t *req, const char *status,
                               const char *payload)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, payload);
}

esp_err_t web_send_error(httpd_req_t *req, const char *http_status,
                         const char *code, const char *message,
                         bool retryable)
{
    char buf[256];
    /* Escape the message for JSON (simple backslash/newline handling). */
    int len = snprintf(buf, sizeof(buf),
        "{\"ok\":false,\"error\":{\"code\":\"%s\",\"message\":\"%s\",\"retryable\":%s}}",
        code ? code : "UNKNOWN",
        message ? message : "error",
        retryable ? "true" : "false");
    if (len < 0 || (size_t)len >= sizeof(buf) - 1) {
        /* Truncated — send what we can. */
        buf[sizeof(buf) - 1] = '\0';
    }
    return web_send_json(req, http_status, buf);
}

esp_err_t web_send_ok(httpd_req_t *req, const char *extra_json)
{
    char buf[128];
    int len;

    if (extra_json && extra_json[0]) {
        /* Merge: {"ok":true,<extra>} */
        len = snprintf(buf, sizeof(buf), "{\"ok\":true,%s}", extra_json);
    } else {
        len = snprintf(buf, sizeof(buf), "{\"ok\":true}");
    }

    if (len < 0 || (size_t)len >= sizeof(buf) - 1) {
        buf[sizeof(buf) - 1] = '\0';
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, buf);
}
