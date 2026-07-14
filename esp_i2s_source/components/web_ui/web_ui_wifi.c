/*
 * web_ui_wifi — WiFi provisioning + control-AP endpoints, split out of web_ui.c.
 * POST /api/wifi (STA creds) and POST /api/apmode (toggle/rename the SoftAP).
 */
#include "web_ui.h"
#include "web_ui_internal.h"
#include "wifi_mgr.h"

#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "web_ui";

/* Deferred provisioning: apply the creds ~400 ms after the HTTP response, so
 * the reply flushes over the (soon-to-drop) SoftAP link before AP->STA. */
static char s_prov_ssid[WIFI_MGR_SSID_MAX + 1];
static char s_prov_pass[WIFI_MGR_PASS_MAX + 1];

static void provision_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(400));
    esp_err_t e = wifi_mgr_set_creds(s_prov_ssid, s_prov_pass);
    ESP_LOGI(TAG, "deferred provision \"%s\": %s", s_prov_ssid, esp_err_to_name(e));
    vTaskDelete(NULL);
}

/* POST /api/wifi {ssid, pass} — validate, reply, then switch AP->STA. */
esp_err_t wifi_post(httpd_req_t *req)
{
    char body[256];
    if (recv_body(req, body, sizeof(body)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body");
        return ESP_OK;
    }
    cJSON *j = cJSON_Parse(body);
    const char *ssid = j ? cJSON_GetStringValue(cJSON_GetObjectItem(j, "ssid")) : NULL;
    const char *pass = j ? cJSON_GetStringValue(cJSON_GetObjectItem(j, "pass")) : NULL;

    size_t sl = ssid ? strlen(ssid) : 0;
    size_t pl = pass ? strlen(pass) : 0;
    if (sl == 0 || sl > WIFI_MGR_SSID_MAX || (pl != 0 && (pl < 8 || pl > WIFI_MGR_PASS_MAX))) {
        cJSON_Delete(j);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"ssid 1..32, pass \\\"\\\" or 8..63\"}");
        return ESP_OK;
    }

    strlcpy(s_prov_ssid, ssid, sizeof(s_prov_ssid));
    strlcpy(s_prov_pass, pass ? pass : "", sizeof(s_prov_pass));
    cJSON_Delete(j);

    /* Create the provisioning task BEFORE replying. The task delays 400ms
     * before applying creds, so the response flushes over the SoftAP link
     * before the STA connection tears it down. If task creation fails, the
     * reply is an error — provisioning never happens. */
    TaskHandle_t prov_task;
    if (xTaskCreate(provision_task, "provision", 4096, NULL,
                    tskIDLE_PRIORITY + 3, &prov_task) != pdPASS) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"task creation failed\"}");
        return ESP_OK;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true,\"host\":\"esp-i2s-source.local\"}");
    return ESP_OK;
}

/* ---- /api/apmode: control the concurrent AP — toggle (AP+STA vs STA-only)
 * and/or change the AP name/password. Accepts {enabled?} and/or {ssid, pass?}. */

esp_err_t apmode_post_h(httpd_req_t *req)
{
    char body[160];
    if (recv_body(req, body, sizeof(body)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body");
        return ESP_OK;
    }
    cJSON *j = cJSON_Parse(body);
    httpd_resp_set_type(req, "application/json");
    cJSON *en = j ? cJSON_GetObjectItem(j, "enabled") : NULL;
    const char *ssid = j ? cJSON_GetStringValue(cJSON_GetObjectItem(j, "ssid")) : NULL;
    cJSON *passj = j ? cJSON_GetObjectItem(j, "pass") : NULL;
    const char *pass = cJSON_IsString(passj) ? passj->valuestring : NULL;

    if (ssid) {   /* rename / re-key the control AP */
        esp_err_t e = wifi_mgr_set_ap_config(ssid, pass ? pass : "");
        if (e != ESP_OK) {
            cJSON_Delete(j);
            httpd_resp_set_status(req, "400 Bad Request");
            httpd_resp_sendstr(req,
                "{\"ok\":false,\"error\":\"SSID 1-32 chars; password blank or 8-64\"}");
            return ESP_OK;
        }
    }
    if (cJSON_IsBool(en)) {
        wifi_mgr_set_ap_enabled(cJSON_IsTrue(en));
    } else if (!ssid) {
        cJSON_Delete(j);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"missing enabled or ssid\"}");
        return ESP_OK;
    }
    cJSON_Delete(j);
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}
