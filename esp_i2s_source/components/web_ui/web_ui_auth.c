/*
 * web_ui_auth — bearer-token authentication for the web UI.
 *
 * Token is generated once on first boot and printed to USB serial as
 *   AUTH|BOOTSTRAP_TOKEN|<token>
 * All POST/PUT/DELETE require "Authorization: Bearer <token>".
 */
#include "web_ui_internal.h"

#include <string.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_random.h"
#include "esp_http_server.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "web_ui_auth";

static const char *AUTH_NVS_KEY = "auth";
#define TOKEN_LEN 32

static char s_token[TOKEN_LEN + 1];
static bool s_token_ready;

/* Constant-time string comparison (resists timing side-channels). */
static int ct_strcmp(const char *a, const char *b)
{
    int result = 0;
    size_t i;
    for (i = 0; a[i] && b[i]; i++) {
        result |= a[i] ^ b[i];
    }
    for (; b[i]; i++) {
        result |= b[i];
    }
    return result;
}

esp_err_t web_ui_auth_init(void)
{
    esp_err_t err;
    nvs_handle nvs_handle;
    size_t required_len = 0;

    /* Try to load token from NVS. */
    err = nvs_open("web_auth", NVS_READONLY, &nvs_handle);
    if (err == ESP_OK) {
        err = nvs_get_str(nvs_handle, AUTH_NVS_KEY, s_token, &required_len);
        nvs_close(nvs_handle);
    }

    if (err == ESP_OK && required_len == TOKEN_LEN) {
        s_token_ready = true;
        ESP_LOGI(TAG, "loaded auth token from NVS");
    } else {
        /* Generate new token. */
        ESP_LOGW(TAG, "no valid token in NVS — generating new token");
        err = web_ui_auth_generate_token();
    }

    return err;
}

esp_err_t web_ui_auth_generate_token(void)
{
    nvs_handle nvs_handle;
    esp_err_t err;

    /* Generate random token. */
    esp_fill_random((uint8_t *)s_token, TOKEN_LEN);
    s_token[TOKEN_LEN] = '\0';
    s_token_ready = true;

    /* Print bootstrap token to USB serial. */
    printf("AUTH|BOOTSTRAP_TOKEN|%s\n", s_token);
    fflush(stdout);

    /* Persist to NVS. */
    err = nvs_open("web_auth", NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        err = nvs_set_str(nvs_handle, AUTH_NVS_KEY, s_token);
        if (err == ESP_OK) err = nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to persist token: %s", esp_err_to_name(err));
        /* Still usable in-memory. */
    }

    return ESP_OK;
}

bool web_ui_auth_check(httpd_req_t *req)
{
    char auth_hdr[128];
    size_t len;

    if (!s_token_ready) return false;

    len = httpd_req_get_hdr_value_len(req, "Authorization");
    if (len == 0 || len >= sizeof(auth_hdr)) {
        return false;
    }

    if (httpd_req_get_hdr_value_str(req, "Authorization", auth_hdr, sizeof(auth_hdr)) != ESP_OK) {
        return false;
    }

    if (strncmp(auth_hdr, "Bearer ", 7) != 0) {
        return false;
    }

    /* Constant-time compare to prevent timing side-channel. */
    return ct_strcmp(auth_hdr + 7, s_token) == 0;
}

const char *web_ui_auth_get_token(void)
{
    return s_token_ready ? s_token : NULL;
}
