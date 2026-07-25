/*
 * wifi_mgr_nvs — credential NVS persistence for wifi_mgr (split out of
 * wifi_mgr.c to keep that file focused on esp_wifi/event glue + public API).
 * Transactional, exact-length; the pure invariant checks live in
 * wifi_creds_core.c. Device glue, not host-tested (verified via idf.py build
 * + hardware smoke test), same as wifi_mgr.c.
 */
#include "wifi_mgr_internal.h"
#include "wifi_creds_core.h"

#include <string.h>

#include "nvs.h"

/* 6.2: read+validate an NVS string in one step. Pure invariant checking is
 * wifi_creds_validate_stored_string(); the nvs_get_str() call is the only
 * device-specific part. */
static esp_err_t nvs_get_string_exact(nvs_handle_t h, const char *key,
                                       char *dst, size_t dst_capacity,
                                       size_t max_payload, size_t *out_payload_len)
{
    if (!key || !dst || dst_capacity == 0 || !out_payload_len) return ESP_ERR_INVALID_ARG;

    size_t stored_len = dst_capacity;
    esp_err_t err = nvs_get_str(h, key, dst, &stored_len);
    if (err != ESP_OK) return err;

    return wifi_creds_validate_stored_string(dst, stored_len, dst_capacity, max_payload, out_payload_len);
}

esp_err_t wifi_nvs_load_creds(wifi_creds_t *c)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(WIFI_MGR_NVS_NS, NVS_READONLY, &h);
    if (err != ESP_OK) return err;

    c->ssid[0] = '\0';
    c->pass[0] = '\0';
    c->ssid_len = 0;
    c->pass_len = 0;

    size_t ssid_payload = 0, pass_payload = 0;
    esp_err_t e1 = nvs_get_string_exact(h, WIFI_MGR_NVS_SSID, c->ssid, sizeof(c->ssid), WIFI_MGR_SSID_MAX, &ssid_payload);
    esp_err_t e2 = nvs_get_string_exact(h, WIFI_MGR_NVS_PASS, c->pass, sizeof(c->pass), WIFI_MGR_PASS_MAX, &pass_payload);
    nvs_close(h);

    if (e1 == ESP_ERR_NVS_NOT_FOUND) {
        /* No SSID key at all -> no credentials (legitimate first-boot state). */
        return ESP_ERR_NOT_FOUND;
    }
    if (e1 != ESP_OK) {
        /* SSID key present but corrupt (bad terminator/oversized payload). */
        return e1;
    }
    if (e2 == ESP_ERR_NVS_NOT_FOUND) {
        /* save always writes both keys (even "" for an open network), so a
         * present SSID with a genuinely missing PASS key is corruption, not
         * an intentionally-open network. */
        return ESP_ERR_INVALID_STATE;
    }
    if (e2 != ESP_OK) {
        return e2;
    }

    c->ssid_len = ssid_payload;
    c->pass_len = pass_payload;
    return ESP_OK;
}

esp_err_t wifi_nvs_save_creds(wifi_creds_t *c, const char *ssid, size_t ssid_len,
                              const char *pass, size_t pass_len)
{
    char tmp_ssid[WIFI_MGR_SSID_MAX + 1];
    char tmp_pass[WIFI_MGR_PASS_MAX + 1];

    memcpy(tmp_ssid, ssid, ssid_len);
    tmp_ssid[ssid_len] = '\0';
    memcpy(tmp_pass, pass ? pass : "", pass_len);
    tmp_pass[pass_len] = '\0';

    nvs_handle_t h;
    esp_err_t err = nvs_open(WIFI_MGR_NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    err = nvs_set_str(h, WIFI_MGR_NVS_SSID, tmp_ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(h, WIFI_MGR_NVS_PASS, tmp_pass);
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);

    if (err != ESP_OK) return err;

    memcpy(c->ssid, ssid, ssid_len);
    c->ssid[ssid_len] = '\0';
    c->ssid_len = ssid_len;
    if (pass && pass_len > 0) {
        memcpy(c->pass, pass, pass_len);
        c->pass[pass_len] = '\0';
        c->pass_len = pass_len;
    } else {
        c->pass[0] = '\0';
        c->pass_len = 0;
    }

    return ESP_OK;
}

esp_err_t wifi_nvs_erase_creds(wifi_creds_t *c)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(WIFI_MGR_NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    esp_err_t e1 = nvs_erase_key(h, WIFI_MGR_NVS_SSID);
    if (e1 != ESP_OK && e1 != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(h);
        return e1;
    }

    esp_err_t e2 = nvs_erase_key(h, WIFI_MGR_NVS_PASS);
    if (e2 != ESP_OK && e2 != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(h);
        return e2;
    }

    err = nvs_commit(h);
    nvs_close(h);

    if (err != ESP_OK) return err;

    c->ssid[0] = '\0';
    c->ssid_len = 0;
    c->pass[0] = '\0';
    c->pass_len = 0;

    return ESP_OK;
}

esp_err_t wifi_nvs_load_ap_enabled(wifi_creds_t *c)
{
    c->ap_enabled = true;   /* default: keep the control AP up */
    nvs_handle_t h;
    esp_err_t err = nvs_open(WIFI_MGR_NVS_NS, NVS_READONLY, &h);
    if (err != ESP_OK) return err;

    uint8_t v = 1;
    if (nvs_get_u8(h, WIFI_MGR_NVS_AP_ON, &v) == ESP_OK) {
        c->ap_enabled = (v != 0);
    }
    nvs_close(h);
    return ESP_OK;
}

esp_err_t wifi_nvs_save_ap_enabled(bool on)
{
    nvs_handle_t h;
    esp_err_t e = nvs_open(WIFI_MGR_NVS_NS, NVS_READWRITE, &h);
    if (e != ESP_OK) return e;
    e = nvs_set_u8(h, WIFI_MGR_NVS_AP_ON, on ? 1 : 0);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return e;
}

/* 6.3: documented AP defaults, applied before any override is loaded. */
void wifi_nvs_set_default_ap_creds(wifi_creds_t *c)
{
    memcpy(c->ap_ssid, WIFI_CREDS_DEFAULT_AP_SSID, sizeof(WIFI_CREDS_DEFAULT_AP_SSID));
    c->ap_ssid_len = sizeof(WIFI_CREDS_DEFAULT_AP_SSID) - 1u;
    memcpy(c->ap_pass, WIFI_CREDS_DEFAULT_AP_PASS, sizeof(WIFI_CREDS_DEFAULT_AP_PASS));
    c->ap_pass_len = sizeof(WIFI_CREDS_DEFAULT_AP_PASS) - 1u;
}

/* Load user-customised control-AP SSID/password over the defaults. Missing
 * override keys keep the default already in c for that field independently
 * (SSID/password overrides are two separate settings, not a paired invariant
 * like STA SSID/pass). A *corrupt* override key returns a visible error
 * without touching either field — no partial application of one valid
 * override alongside one corrupt one. */
esp_err_t wifi_nvs_load_ap_creds(wifi_creds_t *c)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(WIFI_MGR_NVS_NS, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;  /* no namespace yet -> keep defaults */
    if (err != ESP_OK) return err;

    char tmp_ssid[WIFI_MGR_SSID_MAX + 1];
    char tmp_pass[WIFI_MGR_PASS_MAX + 1];
    size_t ssid_payload = 0, pass_payload = 0;

    esp_err_t e1 = nvs_get_string_exact(h, WIFI_MGR_NVS_AP_SSID, tmp_ssid, sizeof(tmp_ssid), WIFI_MGR_SSID_MAX, &ssid_payload);
    esp_err_t e2 = nvs_get_string_exact(h, WIFI_MGR_NVS_AP_PASS, tmp_pass, sizeof(tmp_pass), WIFI_MGR_PASS_MAX, &pass_payload);
    nvs_close(h);

    if (e1 != ESP_OK && e1 != ESP_ERR_NVS_NOT_FOUND) return e1;
    if (e2 != ESP_OK && e2 != ESP_ERR_NVS_NOT_FOUND) return e2;

    if (e1 == ESP_OK) {
        memcpy(c->ap_ssid, tmp_ssid, ssid_payload);
        c->ap_ssid[ssid_payload] = '\0';
        c->ap_ssid_len = ssid_payload;
    }
    if (e2 == ESP_OK) {
        memcpy(c->ap_pass, tmp_pass, pass_payload);
        c->ap_pass[pass_payload] = '\0';
        c->ap_pass_len = pass_payload;
    }
    return ESP_OK;
}

esp_err_t wifi_nvs_write_ap_creds(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(WIFI_MGR_NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    err = nvs_set_str(h, WIFI_MGR_NVS_AP_SSID, ssid);
    if (err == ESP_OK) err = nvs_set_str(h, WIFI_MGR_NVS_AP_PASS, pass);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}
