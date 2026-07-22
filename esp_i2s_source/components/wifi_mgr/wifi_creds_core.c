/* wifi_creds_core — see wifi_creds_core.h. */
#include "wifi_creds_core.h"

#include <string.h>
#include <ctype.h>

esp_err_t wifi_creds_bounded_length(const char *value, size_t max_payload,
                                     bool allow_empty, size_t *out_len)
{
    if (!value || !out_len) return ESP_ERR_INVALID_ARG;
    size_t len = strnlen(value, max_payload + 1u);
    if (len > max_payload) return ESP_ERR_INVALID_SIZE;
    if (!allow_empty && len == 0) return ESP_ERR_INVALID_SIZE;
    *out_len = len;
    return ESP_OK;
}

esp_err_t wifi_creds_validate_ssid(const char *ssid, size_t *out_len)
{
    return wifi_creds_bounded_length(ssid, WIFI_MGR_SSID_MAX, false, out_len);
}

esp_err_t wifi_creds_validate_sta_password(const char *pass, size_t *out_len)
{
    if (!pass || !out_len) return ESP_ERR_INVALID_ARG;

    /* Probe one past the hex-PSK length (64) so a too-long value is
     * distinguishable from one that exactly fills it. */
    size_t len = strnlen(pass, WIFI_MGR_PASS_MAX + 1u);
    if (len > WIFI_MGR_PASS_MAX) return ESP_ERR_INVALID_SIZE;

    if (len == 0) {
        *out_len = 0;
        return ESP_OK;
    }
    if (len >= 8 && len <= 63) {
        *out_len = len;
        return ESP_OK;
    }
#ifdef CONFIG_ESP_I2S_SOURCE_STA_HEX_PSK
    if (len == 64) {
        for (size_t i = 0; i < 64; ++i) {
            if (!isxdigit((unsigned char)pass[i])) {
                return ESP_ERR_INVALID_ARG;
            }
        }
        *out_len = 64;
        return ESP_OK;
    }
#endif
    return ESP_ERR_INVALID_ARG;
}

esp_err_t wifi_creds_validate_ap_password(const char *pass, size_t *out_len)
{
    size_t len = 0;
    esp_err_t err = wifi_creds_bounded_length(pass ? pass : "", 63, true, &len);
    if (err != ESP_OK) return err;
    if (len > 0 && len < 8) return ESP_ERR_INVALID_ARG;
    *out_len = len;
    return ESP_OK;
}

esp_err_t wifi_creds_validate_stored_string(const char *dst, size_t stored_len,
                                             size_t dst_capacity, size_t max_payload,
                                             size_t *out_payload_len)
{
    if (!dst || !out_payload_len || dst_capacity == 0) return ESP_ERR_INVALID_ARG;
    if (stored_len == 0 || stored_len > dst_capacity) return ESP_ERR_INVALID_SIZE;
    if (dst[stored_len - 1u] != '\0') return ESP_ERR_INVALID_CRC;

    size_t payload_len = stored_len - 1u;
    if (payload_len > max_payload) return ESP_ERR_INVALID_SIZE;

    *out_payload_len = payload_len;
    return ESP_OK;
}
