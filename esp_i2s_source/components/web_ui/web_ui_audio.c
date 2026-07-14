/*
 * web_ui_audio — local-audio endpoints, split out of web_ui.c: the S3 test
 * tone (POST/DELETE /api/tone), the pre-I2S software gain (POST /api/volume),
 * and the radio prebuffer depth (POST /api/prebuffer).
 */
#include "web_ui.h"
#include "web_ui_internal.h"
#include "tone.h"
#include "radio.h"
#include "i2s_out.h"

#include <string.h>
#include <stdio.h>

#include "esp_http_server.h"
#include "cJSON.h"

/* POST /api/tone {hz, amp?} — enable the test tone (amp = 0..100% amplitude,
 * optional); DELETE /api/tone — silence it. */
esp_err_t tone_post(httpd_req_t *req)
{
    char body[128];
    int hz = TONE_HZ_DEFAULT;
    if (recv_body(req, body, sizeof(body)) == ESP_OK) {
        cJSON *j = cJSON_Parse(body);
        cJSON *h = j ? cJSON_GetObjectItem(j, "hz") : NULL;
        if (cJSON_IsNumber(h)) hz = h->valueint;
        cJSON *a = j ? cJSON_GetObjectItem(j, "amp") : NULL;
        if (cJSON_IsNumber(a)) tone_set_amplitude(a->valueint);   /* clamps [0,100] */
        const char *voice = j ? cJSON_GetStringValue(cJSON_GetObjectItem(j, "voice")) : NULL;
        tone_set_voice(voice && !strcmp(voice, "piano") ? TONE_VOICE_PIANO : TONE_VOICE_SINE);
        cJSON_Delete(j);
    }
    tone_set(hz);
    bool on; int cur;
    tone_get(&on, &cur);
    char resp[64];
    snprintf(resp, sizeof(resp), "{\"ok\":true,\"on\":true,\"hz\":%d}", cur);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, resp);
}

esp_err_t tone_delete(httpd_req_t *req)
{
    tone_off();
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true,\"on\":false}");
}

/* ---- /api/volume: S3 pre-I2S software gain (0..100) ---- */

esp_err_t volume_post_h(httpd_req_t *req)
{
    char body[64];
    if (recv_body(req, body, sizeof(body)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body");
        return ESP_OK;
    }
    cJSON *j = cJSON_Parse(body);
    cJSON *pct = j ? cJSON_GetObjectItem(j, "pct") : NULL;
    httpd_resp_set_type(req, "application/json");
    if (!cJSON_IsNumber(pct)) {
        cJSON_Delete(j);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"missing pct\"}");
        return ESP_OK;
    }
    esp_err_t err = i2s_out_set_gain(pct->valueint);  /* clamps to [0,100] */
    cJSON_Delete(j);
    if (err != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"failed to persist gain\"}");
        return ESP_OK;
    }
    char out[48];
    snprintf(out, sizeof(out), "{\"ok\":true,\"pct\":%d}", i2s_out_get_gain());
    httpd_resp_sendstr(req, out);
    return ESP_OK;
}

/* ---- /api/prebuffer: radio jitter-cushion depth (ms), NVS-persisted ---- */

esp_err_t prebuffer_post_h(httpd_req_t *req)
{
    char body[48];
    if (recv_body(req, body, sizeof(body)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body");
        return ESP_OK;
    }
    cJSON *j = cJSON_Parse(body);
    cJSON *ms = j ? cJSON_GetObjectItem(j, "ms") : NULL;
    httpd_resp_set_type(req, "application/json");
    if (!cJSON_IsNumber(ms)) {
        cJSON_Delete(j);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"missing ms\"}");
        return ESP_OK;
    }
    esp_err_t err = radio_set_prebuffer_ms(ms->valueint);  /* clamps to a ring-safe range */
    cJSON_Delete(j);
    if (err != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"failed to persist prebuffer\"}");
        return ESP_OK;
    }
    char out[48];
    snprintf(out, sizeof(out), "{\"ok\":true,\"ms\":%d}", radio_get_prebuffer_ms());
    httpd_resp_sendstr(req, out);
    return ESP_OK;
}
