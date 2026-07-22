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
#include "runtime_capabilities.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "esp_http_server.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "web_ui_radio";

/* FIX3 10.3: centralized capability guards — a degraded boot (radio_init()
 * or stations_init() failed) must return 503, never silently operate on an
 * uninitialized module. Mirrors web_ui_bt.c's require_bt(). */
static esp_err_t require_radio(httpd_req_t *req)
{
    runtime_capabilities_t caps;
    runtime_capabilities_get(&caps);
    if (caps.radio) return ESP_OK;
    return web_send_error(req, "503 Service Unavailable", "RADIO_UNAVAILABLE",
                          "radio is unavailable", true);
}

static esp_err_t require_stations(httpd_req_t *req)
{
    runtime_capabilities_t caps;
    runtime_capabilities_get(&caps);
    if (caps.stations) return ESP_OK;
    return web_send_error(req, "503 Service Unavailable", "STATIONS_UNAVAILABLE",
                          "station storage is unavailable", true);
}

/* POST /api/radio {url} — resolve + play; DELETE /api/radio — stop.
 * Commands are queued to the radio module's command worker (RH-S3-09). */

esp_err_t radio_post(httpd_req_t *req)
{
    esp_err_t guard = require_radio(req);
    if (guard != ESP_OK) return guard;

    web_json_body_t jbody;
    if (web_read_json(req, RADIO_URL_MAX + 128, &jbody) != ESP_OK) {
        return web_send_error(req, "400 Bad Request", "BAD_BODY", "invalid JSON body", false);
    }

   /* 10.1 — Copy URL into a fixed buffer BEFORE cJSON_Delete.
     * The cJSON string pointer dangles after web_json_free(). */
    char url_copy[RADIO_URL_MAX];
    url_copy[0] = '\0';
    const char *url = cJSON_GetStringValue(cJSON_GetObjectItem(jbody.root, "url"));
    if (url && url[0]) {
        size_t len = strnlen(url, sizeof(url_copy));
        if (len == 0 || len >= sizeof(url_copy)) {
            web_json_free(&jbody);
            return web_send_error(req, "400 Bad Request", "INVALID_URL", "url empty or too long", false);
        }
        memcpy(url_copy, url, len + 1);
    }

    /* Optional station ID: record it so CTRL-1 autostart can resume it. */
    cJSON *id = cJSON_GetObjectItem(jbody.root, "id");
    esp_err_t note_err = ESP_OK;
    if (cJSON_IsNumber(id)) note_err = ctrl_note_station(id->valueint);

    web_json_free(&jbody);

    if (!url_copy[0]) {
        return web_send_error(req, "400 Bad Request", "INVALID_URL", "missing url", false);
    }

    /* FIX3 §8.6/URL-001: direct play must pass the same syntax + literal-IP
     * SSRF policy as stored stations — this was previously bypassed
     * entirely (radio_play_async() was called with no validation at all). */
    if (station_validate_url(url_copy) != STATION_OK) {
        return web_send_error(req, "400 Bad Request", "INVALID_URL",
                              "url rejected by destination policy", false);
    }

    /* Queue the play command (RH-S3-09). */
    esp_err_t err = radio_play_async(url_copy);
    if (note_err != ESP_OK) {
        ESP_LOGW(TAG, "failed to persist station note: %s", esp_err_to_name(note_err));
        return web_send_error(req, "500 Internal Server Error", "PERSIST_ERR", "failed to persist station", false);
    }
    if (err != ESP_OK) {
        return web_send_error(req, "503 Service Unavailable", "QUEUE_FULL", "radio queue full", true);
    }
    return web_send_ok(req, NULL);
}

esp_err_t radio_delete(httpd_req_t *req)
{
    esp_err_t guard = require_radio(req);
    if (guard != ESP_OK) return guard;

    /* Queue the stop command (RH-S3-09). */
    esp_err_t err = radio_stop_async();
    if (err != ESP_OK) {
        return web_send_error(req, "503 Service Unavailable", "QUEUE_FULL", "radio queue full", true);
    }
    return web_send_ok(req, NULL);
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
    esp_err_t guard = require_stations(req);
    if (guard != ESP_OK) return guard;

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

/* Parse {name,url} from the body into caller buffers; returns true on success. */
static bool station_body(httpd_req_t *req, char *name, size_t nsz, char *url, size_t usz)
{
    web_json_body_t jbody;
    if (web_read_json(req, STATION_URL_MAX + STATION_NAME_MAX + 64, &jbody) != ESP_OK) {
        return false;
    }

    const char *jn = cJSON_GetStringValue(cJSON_GetObjectItem(jbody.root, "name"));
    const char *ju = cJSON_GetStringValue(cJSON_GetObjectItem(jbody.root, "url"));
    name[0] = url[0] = '\0';
    if (jn) strlcpy(name, jn, nsz);
    if (ju) strlcpy(url, ju, usz);
    bool ok = ju && ju[0];
    web_json_free(&jbody);
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
    esp_err_t guard = require_stations(req);
    if (guard != ESP_OK) return guard;

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
    esp_err_t guard = require_stations(req);
    if (guard != ESP_OK) return guard;

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
    esp_err_t guard = require_stations(req);
    if (guard != ESP_OK) return guard;

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
