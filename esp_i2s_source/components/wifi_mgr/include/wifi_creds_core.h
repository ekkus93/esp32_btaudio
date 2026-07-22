/*
 * wifi_creds_core — pure Wi-Fi credential validation logic (FIX3 Phase 6,
 * WIFI-001/6.1/6.2). No ESP-IDF deps beyond esp_err_t; host-tested. NVS I/O
 * itself (nvs_get_str/nvs_set_str/nvs_erase_key) is device glue in
 * wifi_mgr.c, same split as station_store.c/stations_persist_core.c and
 * web_ui_auth_core.c/web_ui_auth.c elsewhere in this codebase.
 */
#pragma once

#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_MGR_SSID_MAX  32
#define WIFI_MGR_PASS_MAX  64

#define WIFI_CREDS_DEFAULT_AP_SSID "ESP32-S3-Audio"
#define WIFI_CREDS_DEFAULT_AP_PASS "password"

/* 6.1: strnlen probes max_payload+1 so a value at or past the limit is
 * distinguishable from one that merely fills the buffer exactly —
 * strnlen(value, max) followed by `len > max` can never observe an
 * overflow, since it silently truncates at max. */
esp_err_t wifi_creds_bounded_length(const char *value, size_t max_payload,
                                     bool allow_empty, size_t *out_len);

/* SSID: 1..32 bytes, binary-safe (no NUL requirement beyond bounded_length's
 * own strnlen probe). */
esp_err_t wifi_creds_validate_ssid(const char *ssid, size_t *out_len);

/* STA password: "" (open network), 8..63 (WPA2 passphrase), or — only when
 * compiled with CONFIG_ESP_I2S_SOURCE_STA_HEX_PSK — exactly 64 hex digits
 * (raw PSK). */
esp_err_t wifi_creds_validate_sta_password(const char *pass, size_t *out_len);

/* Control-AP password: "" (open) or 8..63 (WPA2 passphrase). No hex-PSK
 * form — the SoftAP always uses a passphrase. */
esp_err_t wifi_creds_validate_ap_password(const char *pass, size_t *out_len);

/* 6.2: validate a string just retrieved via nvs_get_str(): dst[0..stored_len)
 * with stored_len following the ESP-IDF convention of including the NUL
 * terminator. Pure — the nvs_get_str() call itself is device glue. */
esp_err_t wifi_creds_validate_stored_string(const char *dst, size_t stored_len,
                                             size_t dst_capacity, size_t max_payload,
                                             size_t *out_payload_len);

#ifdef __cplusplus
}
#endif
