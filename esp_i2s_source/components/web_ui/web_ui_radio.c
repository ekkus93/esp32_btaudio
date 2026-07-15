/*
 * web_ui_radio — radio playback (POST/DELETE /api/radio) and the station store
 * CRUD (/api/stations GET/POST/PUT/DELETE), split out of web_ui.c.
 */
#include "web_ui.h"
#include "web_ui_internal.h"
#include "radio.h"
#include "stations.h"
#include "station_store.h"
#include "ctrl.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "esp_http_server.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "web_ui_radio";

/* POST /api/radio {url} — resolve + play; DELETE /api/radio — stop.
 * Commands are queued to the radio module's command worker (RH-S3-09). */

esp_err_t radio_post(httpd_req_t *req)
{
    char body[RADIO_URL_MAX + 64];
    if (recv_body(req, body, sizeof(body)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body");
        return ESP_OK;
    }
    cJSON *j = cJSON_Parse(body);
    const char *url = j ? cJSON_GetStringValue(cJSON_GetObjectItem(j, "url")) : NULL;
    if (!url || !url[0] || strlen(url) >= RADIO_URL_MAX) {
        cJSON_Delete(j);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"missing/oversized url\"}");
        return ESP_OK;
    }
    /* Optional station index: record it so CTRL-1 autostart can resume it. */
    cJSON *id = cJSON_GetObjectItem(j, "id");
    esp_err_t note_err = ESP_OK;
    if (cJSON_IsNumber(id)) note_err = ctrl_note_station(id->valueint);
    cJSON_Delete(j);

    /* Queue the play command (RH-S3-09). */
    esp_err_t err = radio_play_async(url);
    httpd_resp_set_type(req, "application/json");
    if (note_err != ESP_OK) {
        ESP_LOGW(TAG, "failed to persist station note: %s", esp_err_to_name(note_err));
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"failed to persist station\"}");
        return ESP_OK;
    }
    if (err != ESP_OK) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"radio queue full\"}");
        return ESP_OK;
    }
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

esp_err_t radio_delete(httpd_req_t *req)
{
    /* Queue the stop command (RH-S3-09). */
    esp_err_t err = radio_stop_async();
    httpd_resp_set_type(req, "application/json");
    if (err != ESP_OK) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"radio queue full\"}");
        return ESP_OK;
    }
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

/* ---- /api/stations CRUD (RADIO-1c) ---- */

static int station_id_param(httpd_req_t *req)
{
    char q[48], v[12];
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK &&
        httpd_query_key_value(q, "id", v, sizeof(v)) == ESP_OK) {
        return atoi(v);
    }
    return -1;
}

esp_err_t stations_get_h(httpd_req_t *req)
{
    cJSON *arr = cJSON_CreateArray();
    int n = stations_count();
    for (int i = 0; i < n; i++) {
        char name[STATION_NAME_MAX], url[STATION_URL_MAX];
        uint32_t sid;
        if (!stations_get(i, name, sizeof(name), url, sizeof(url), &sid)) continue;
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "id", (int)sid);
        cJSON_AddStringToObject(o, "name", name);
        cJSON_AddStringToObject(o, "url", url);
        cJSON_AddItemToArray(arr, o);
    }
    char *body = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    if (!body) return httpd_resp_send_500(req);
    httpd_resp_set_type(req, "application/json");
    esp_err_t e = httpd_resp_sendstr(req, body);
    cJSON_free(body);
    return e;
}

/* Parse {name,url} from the body into caller buffers; returns url ptr or NULL. */
static bool station_body(httpd_req_t *req, char *name, size_t nsz, char *url, size_t usz)
{
    char b[STATION_URL_MAX + STATION_NAME_MAX + 64];
    if (recv_body(req, b, sizeof(b)) != ESP_OK) return false;
    cJSON *j = cJSON_Parse(b);
    const char *jn = j ? cJSON_GetStringValue(cJSON_GetObjectItem(j, "name")) : NULL;
    const char *ju = j ? cJSON_GetStringValue(cJSON_GetObjectItem(j, "url")) : NULL;
    name[0] = url[0] = '\0';
    if (jn) strlcpy(name, jn, nsz);
    if (ju) strlcpy(url, ju, usz);
    bool ok = ju && ju[0];
    cJSON_Delete(j);
    return ok;
}

static void station_reply(httpd_req_t *req, bool ok, int id)
{
    char r[48];
    httpd_resp_set_type(req, "application/json");
    if (ok) {
        snprintf(r, sizeof(r), "{\"ok\":true,\"id\":%d}", id);
    } else {
        httpd_resp_set_status(req, "400 Bad Request");
        strlcpy(r, "{\"ok\":false,\"error\":\"invalid/duplicate/full\"}", sizeof(r));
    }
    httpd_resp_sendstr(req, r);
}

static void station_reply_err(httpd_req_t *req, const char *err, int id)
{
    char r[64];
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, "500 Internal Server Error");
    snprintf(r, sizeof(r), "{\"ok\":false,\"error\":\"%s\",\"id\":%d}", err, id);
    httpd_resp_sendstr(req, r);
}

esp_err_t stations_post_h(httpd_req_t *req)
{
    char name[STATION_NAME_MAX], url[STATION_URL_MAX];
    if (!station_body(req, name, sizeof(name), url, sizeof(url))) {
        station_reply(req, false, -1);
        return ESP_OK;
    }
    int id = -1;
    esp_err_t err = stations_add(name, url, &id);
    if (err == ESP_ERR_NO_MEM || err == ESP_ERR_INVALID_ARG) {
        station_reply(req, false, id);
    } else if (err != ESP_OK) {
        station_reply_err(req, "failed to persist", id);
    } else {
        station_reply(req, true, id);
    }
    return ESP_OK;
}

esp_err_t stations_put_h(httpd_req_t *req)
{
    int id = station_id_param(req);
    /* Reorder shortcut: PUT /api/stations?id=X&move=up|down (no body). */
    char q[64], mv[8];
    if (id >= 0 &&
        httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK &&
        httpd_query_key_value(q, "move", mv, sizeof(mv)) == ESP_OK) {
        int delta = !strcmp(mv, "up") ? -1 : (!strcmp(mv, "down") ? 1 : 0);
        esp_err_t err = delta != 0 ? stations_move(id, delta) : ESP_ERR_INVALID_ARG;
        if (err == ESP_ERR_INVALID_ARG) {
            station_reply(req, false, id);
        } else if (err != ESP_OK) {
            station_reply_err(req, "failed to persist", id);
        } else {
            station_reply(req, true, id);
        }
        return ESP_OK;
    }
    char name[STATION_NAME_MAX], url[STATION_URL_MAX];
    if (id < 0 || !station_body(req, name, sizeof(name), url, sizeof(url))) {
        station_reply(req, false, id);
        return ESP_OK;
    }
    esp_err_t err = stations_update(id, name, url);
    if (err == ESP_ERR_INVALID_ARG) {
        station_reply(req, false, id);
    } else if (err != ESP_OK) {
        station_reply_err(req, "failed to persist", id);
    } else {
        station_reply(req, true, id);
    }
    return ESP_OK;
}

esp_err_t stations_delete_h(httpd_req_t *req)
{
    int id = station_id_param(req);
    esp_err_t err = id >= 0 ? stations_remove(id) : ESP_ERR_INVALID_ARG;
    if (err == ESP_ERR_INVALID_ARG) {
        station_reply(req, false, id);
    } else if (err != ESP_OK) {
        station_reply_err(req, "failed to persist", id);
    } else {
        station_reply(req, true, id);
    }
    return ESP_OK;
}
