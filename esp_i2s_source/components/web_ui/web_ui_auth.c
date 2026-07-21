/*
 * web_ui_auth — bearer-token authentication for the web UI (FIX3 §5).
 *
 * Token is a 64-character lowercase-hex encoding of 32 random bytes,
 * generated once on first boot and persisted before it is ever published
 * or printed. Printed to USB serial as:
 *   AUTH|BOOTSTRAP_TOKEN|<token>
 * All POST/PUT/DELETE routes require "Authorization: Bearer <token>".
 * Pure encoding/validation/compare logic lives in web_ui_auth_core.c so it
 * can be host-tested without NVS/httpd/RNG.
 */
#include "web_ui_internal.h"
#include "web_ui_auth_core.h"

#include <string.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_random.h"
#include "esp_http_server.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "web_ui_auth";
static const char *AUTH_NVS_NAMESPACE = "web_auth";
static const char *AUTH_NVS_KEY = "token";

static char s_token[AUTH_TOKEN_BUF_LEN];
static bool s_token_ready;
static SemaphoreHandle_t s_token_mtx;

/* Load the persisted token into out (AUTH_TOKEN_BUF_LEN bytes). Returns:
 *   ESP_OK                  — out holds a syntactically valid token
 *   ESP_ERR_NVS_NOT_FOUND    — no token has ever been persisted (first boot)
 *   ESP_ERR_INVALID_CRC      — a token exists but is malformed/corrupt
 *   (other esp_err_t)        — NVS I/O error
 */
static esp_err_t load_token(char out[AUTH_TOKEN_BUF_LEN])
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(AUTH_NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) {
        return err; /* includes ESP_ERR_NVS_NOT_FOUND for a missing namespace */
    }

    size_t len = AUTH_TOKEN_BUF_LEN;
    err = nvs_get_str(h, AUTH_NVS_KEY, out, &len);
    nvs_close(h);
    if (err != ESP_OK) {
        return err; /* includes ESP_ERR_NVS_NOT_FOUND for a missing key */
    }

    if (len != AUTH_TOKEN_BUF_LEN || out[AUTH_TOKEN_BUF_LEN - 1] != '\0' ||
        !auth_token_is_valid(out)) {
        return ESP_ERR_INVALID_CRC;
    }
    return ESP_OK;
}

static esp_err_t persist_token(const char token[AUTH_TOKEN_BUF_LEN])
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(AUTH_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(h, AUTH_NVS_KEY, token);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

/* Generate 32 random bytes, hex-encode, and persist before returning it.
 * On any failure, out/random material is wiped and nothing is published. */
static esp_err_t generate_and_persist_candidate(char out[AUTH_TOKEN_BUF_LEN])
{
    uint8_t random_bytes[AUTH_TOKEN_BYTES];
    char candidate[AUTH_TOKEN_BUF_LEN];

    esp_fill_random(random_bytes, sizeof(random_bytes));
    auth_hex_encode_lower(random_bytes, sizeof(random_bytes), candidate, sizeof(candidate));
    memset(random_bytes, 0, sizeof(random_bytes));

    esp_err_t err = persist_token(candidate);
    if (err != ESP_OK) {
        memset(candidate, 0, sizeof(candidate));
        return err;
    }

    memcpy(out, candidate, AUTH_TOKEN_BUF_LEN);
    memset(candidate, 0, sizeof(candidate));
    return ESP_OK;
}

esp_err_t web_ui_auth_generate_token(void)
{
    char candidate[AUTH_TOKEN_BUF_LEN];
    esp_err_t err = generate_and_persist_candidate(candidate);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "token generation/persist failed: %s", esp_err_to_name(err));
        xSemaphoreTake(s_token_mtx, portMAX_DELAY);
        s_token_ready = false;
        xSemaphoreGive(s_token_mtx);
        return err;
    }

    xSemaphoreTake(s_token_mtx, portMAX_DELAY);
    memcpy(s_token, candidate, sizeof(s_token));
    s_token_ready = true;
    xSemaphoreGive(s_token_mtx);
    memset(candidate, 0, sizeof(candidate));

    printf("AUTH|BOOTSTRAP_TOKEN|%s\n", s_token);
    fflush(stdout);
    return ESP_OK;
}

esp_err_t web_ui_auth_rotate(void)
{
    if (!s_token_mtx) {
        return ESP_ERR_INVALID_STATE;
    }

    char candidate[AUTH_TOKEN_BUF_LEN];
    esp_err_t err = generate_and_persist_candidate(candidate);
    if (err != ESP_OK) {
        /* Old token remains active and untouched; nothing is printed. */
        ESP_LOGE(TAG, "token rotation failed: %s", esp_err_to_name(err));
        return err;
    }

    xSemaphoreTake(s_token_mtx, portMAX_DELAY);
    memcpy(s_token, candidate, sizeof(s_token)); /* old token invalidated here */
    s_token_ready = true;
    xSemaphoreGive(s_token_mtx);
    memset(candidate, 0, sizeof(candidate));

    printf("AUTH|BOOTSTRAP_TOKEN|%s\n", s_token);
    fflush(stdout);
    printf("AUTH|TOKEN_ROTATED\n");
    fflush(stdout);
    return ESP_OK;
}

esp_err_t web_ui_auth_init(void)
{
    if (!s_token_mtx) {
        s_token_mtx = xSemaphoreCreateMutex();
        if (!s_token_mtx) {
            return ESP_ERR_NO_MEM;
        }
    }

    char candidate[AUTH_TOKEN_BUF_LEN];
    esp_err_t err = load_token(candidate);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        /* First boot: no token has ever been persisted. */
        return web_ui_auth_generate_token();
    }
    if (err != ESP_OK) {
        /* Includes ESP_ERR_INVALID_CRC (malformed stored token) — do not
         * silently replace a corrupt token with a fresh one. */
        ESP_LOGE(TAG, "auth token load failed: %s", esp_err_to_name(err));
        xSemaphoreTake(s_token_mtx, portMAX_DELAY);
        s_token_ready = false;
        xSemaphoreGive(s_token_mtx);
        return err;
    }

    xSemaphoreTake(s_token_mtx, portMAX_DELAY);
    memcpy(s_token, candidate, sizeof(s_token));
    s_token_ready = true;
    xSemaphoreGive(s_token_mtx);
    memset(candidate, 0, sizeof(candidate));
    ESP_LOGI(TAG, "loaded auth token from NVS");
    printf("DIAG|AUTH|READY|source=loaded\n");
    fflush(stdout);
    return ESP_OK;
}

bool web_ui_auth_check(httpd_req_t *req)
{
    if (!s_token_mtx) {
        return false;
    }

    size_t len = httpd_req_get_hdr_value_len(req, "Authorization");
    if (len != AUTH_HEADER_EXACT_LEN) {
        return false;
    }

    char hdr[AUTH_HEADER_EXACT_LEN + 1];
    if (httpd_req_get_hdr_value_str(req, "Authorization", hdr, sizeof(hdr)) != ESP_OK) {
        return false;
    }

    char candidate[AUTH_TOKEN_BUF_LEN];
    if (!auth_header_extract_bearer(hdr, len, candidate)) {
        return false;
    }

    xSemaphoreTake(s_token_mtx, portMAX_DELAY);
    bool ok = s_token_ready && auth_token_equal_exact(candidate, s_token);
    xSemaphoreGive(s_token_mtx);
    return ok;
}
