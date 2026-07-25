/*
 * wifi_mgr internal glue — shared between wifi_mgr.c (esp_wifi/event/public
 * API) and wifi_mgr_nvs.c (credential NVS persistence). Not a public header;
 * device glue only, not host-tested (same as wifi_mgr.c).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "wifi_mgr.h"   /* WIFI_MGR_SSID_MAX / WIFI_MGR_PASS_MAX */

/* ---- NVS namespace + keys (shared by both glue files) ---- */
#define WIFI_MGR_NVS_NS       "wifi"
#define WIFI_MGR_NVS_SSID      "ssid"
#define WIFI_MGR_NVS_PASS      "pass"
#define WIFI_MGR_NVS_AP_ON     "ap_on"
#define WIFI_MGR_NVS_AP_SSID   "ap_ssid"
#define WIFI_MGR_NVS_AP_PASS   "ap_pass"

/* Persisted Wi-Fi configuration (exact-length, NUL-terminated buffers).
 * Owned by wifi_mgr.c; wifi_mgr_nvs.c reads/writes it through the functions
 * below rather than touching any shared global. */
typedef struct {
    char   ssid[WIFI_MGR_SSID_MAX + 1];
    size_t ssid_len;
    char   pass[WIFI_MGR_PASS_MAX + 1];
    size_t pass_len;

    char   ap_ssid[WIFI_MGR_SSID_MAX + 1];
    size_t ap_ssid_len;
    char   ap_pass[WIFI_MGR_PASS_MAX + 1];
    size_t ap_pass_len;

    bool   ap_enabled;   /* keep the control AP up alongside STA */
} wifi_creds_t;

/* ---- credential NVS persistence (wifi_mgr_nvs.c) ----
 * All transactional/exact-length; STA creds validation invariants live in the
 * pure wifi_creds_core.c. These operate on the caller's wifi_creds_t. */

/* Load STA creds into c->ssid/c->pass. ESP_ERR_NOT_FOUND = legitimate
 * first-boot (no SSID key); other non-OK = corruption. */
esp_err_t wifi_nvs_load_creds(wifi_creds_t *c);

/* Persist STA creds, then on success apply them to c->ssid/c->pass. */
esp_err_t wifi_nvs_save_creds(wifi_creds_t *c, const char *ssid, size_t ssid_len,
                              const char *pass, size_t pass_len);

/* Erase both STA keys (tolerating missing) and clear c->ssid/c->pass. */
esp_err_t wifi_nvs_erase_creds(wifi_creds_t *c);

/* Control-AP enable flag: defaults to true when the key is absent. */
esp_err_t wifi_nvs_load_ap_enabled(wifi_creds_t *c);
esp_err_t wifi_nvs_save_ap_enabled(bool on);

/* Set c->ap_ssid/c->ap_pass to the compile-time defaults (call before
 * loading any override). */
void wifi_nvs_set_default_ap_creds(wifi_creds_t *c);

/* Load user AP overrides over whatever defaults are already in c (each of
 * SSID/pass is independent; a corrupt key returns an error without touching
 * either field). */
esp_err_t wifi_nvs_load_ap_creds(wifi_creds_t *c);

/* Write both AP keys transactionally (persist only — used by set_ap_config for
 * its persist and its rollback; does not touch c). */
esp_err_t wifi_nvs_write_ap_creds(const char *ssid, const char *pass);
