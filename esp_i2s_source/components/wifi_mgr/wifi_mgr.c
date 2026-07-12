/*
 * wifi_mgr device glue (WIFI-1b). Drives the pure wifi_sm with esp_wifi
 * events, persists creds in NVS, brings up SoftAP provisioning or STA, and
 * publishes mDNS on the LAN. See wifi_mgr.h.
 */
#include "wifi_mgr.h"
#include "wifi_sm.h"

#include <string.h>

#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "nvs.h"
#include "mdns.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "wifi_mgr";

#define NVS_NS          "wifi"
#define NVS_KEY_SSID    "ssid"
#define NVS_KEY_PASS    "pass"
#define NVS_KEY_AP_ON   "ap_on"
#define NVS_KEY_AP_SSID "ap_ssid"
#define NVS_KEY_AP_PASS "ap_pass"

static wifi_sm_t     s_sm;
static esp_netif_t  *s_sta_netif;
static esp_netif_t  *s_ap_netif;
static char          s_ssid[WIFI_MGR_SSID_MAX + 1];
static char          s_pass[WIFI_MGR_PASS_MAX + 1];
static char          s_ap_ssid[WIFI_MGR_SSID_MAX + 1] = WIFI_MGR_AP_SSID;
static char          s_ap_pass[WIFI_MGR_PASS_MAX + 1];
static bool          s_mdns_up;
static bool          s_sta_got_ip;   /* true between GOT_IP and DISCONNECTED */
static bool          s_wifi_started;  /* esp_wifi_start() called (any mode) */
static bool          s_ap_enabled = true; /* keep the control AP up alongside STA */

/* ---- credential persistence ---- */

static bool load_creds(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    size_t sl = sizeof(s_ssid), pl = sizeof(s_pass);
    esp_err_t e1 = nvs_get_str(h, NVS_KEY_SSID, s_ssid, &sl);
    esp_err_t e2 = nvs_get_str(h, NVS_KEY_PASS, s_pass, &pl);
    nvs_close(h);
    if (e1 != ESP_OK || s_ssid[0] == '\0') {
        s_ssid[0] = s_pass[0] = '\0';
        return false;
    }
    if (e2 != ESP_OK) s_pass[0] = '\0';   /* open network */
    return true;
}

static esp_err_t save_creds(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_str(h, NVS_KEY_SSID, ssid);
    if (err == ESP_OK) err = nvs_set_str(h, NVS_KEY_PASS, pass ? pass : "");
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

static void erase_creds(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, NVS_KEY_SSID);
        nvs_erase_key(h, NVS_KEY_PASS);   /* keep ap_on across a WIFI RESET */
        nvs_commit(h);
        nvs_close(h);
    }
    s_ssid[0] = s_pass[0] = '\0';
}

static void load_ap_enabled(void)
{
    s_ap_enabled = true;   /* default: keep the control AP up */
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    uint8_t v = 1;
    if (nvs_get_u8(h, NVS_KEY_AP_ON, &v) == ESP_OK) s_ap_enabled = (v != 0);
    nvs_close(h);
}

static esp_err_t save_ap_enabled(bool on)
{
    nvs_handle_t h;
    esp_err_t e = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (e != ESP_OK) return e;
    e = nvs_set_u8(h, NVS_KEY_AP_ON, on ? 1 : 0);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return e;
}

/* Load a user-customised control-AP SSID/password over the defaults (fixed
 * SSID + MAC-derived password already in s_ap_ssid/s_ap_pass). */
static void load_ap_creds(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    char ssid[WIFI_MGR_SSID_MAX + 1], pass[WIFI_MGR_PASS_MAX + 1];
    size_t sl = sizeof(ssid), pl = sizeof(pass);
    if (nvs_get_str(h, NVS_KEY_AP_SSID, ssid, &sl) == ESP_OK && ssid[0]) {
        strlcpy(s_ap_ssid, ssid, sizeof(s_ap_ssid));
    }
    if (nvs_get_str(h, NVS_KEY_AP_PASS, pass, &pl) == ESP_OK) {
        strlcpy(s_ap_pass, pass, sizeof(s_ap_pass));   /* "" = open AP */
    }
    nvs_close(h);
}

/* ---- mode application ---- */

static void start_mdns(void)
{
    if (s_mdns_up) return;
    if (mdns_init() != ESP_OK) return;
    mdns_hostname_set(WIFI_MGR_HOSTNAME);
    mdns_instance_name_set("ESP32-S3 Audio Source");
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    s_mdns_up = true;
    ESP_LOGI(TAG, "mDNS up: %s.local", WIFI_MGR_HOSTNAME);
}

/* Set the SoftAP interface config (idempotent). */
static void ensure_ap_config(void)
{
    wifi_config_t cfg = {0};
    strlcpy((char *)cfg.ap.ssid, s_ap_ssid, sizeof(cfg.ap.ssid));
    cfg.ap.ssid_len = strlen(s_ap_ssid);
    strlcpy((char *)cfg.ap.password, s_ap_pass, sizeof(cfg.ap.password));
    cfg.ap.max_connection = 4;
    /* Empty password => open AP (WPA2 requires an 8-char key). */
    cfg.ap.authmode = (s_ap_pass[0] == '\0') ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    esp_wifi_set_config(WIFI_IF_AP, &cfg);
}

static void apply_sta(void)
{
    /* Concurrent AP+STA: keep the control AP up alongside STA when enabled, so
     * the UI stays reachable at 192.168.4.1. STA-only when the AP is disabled. */
    esp_wifi_set_mode(s_ap_enabled ? WIFI_MODE_APSTA : WIFI_MODE_STA);
    if (s_ap_enabled) ensure_ap_config();

    wifi_config_t cfg = {0};
    strlcpy((char *)cfg.sta.ssid, s_ssid, sizeof(cfg.sta.ssid));
    strlcpy((char *)cfg.sta.password, s_pass, sizeof(cfg.sta.password));
    esp_wifi_set_config(WIFI_IF_STA, &cfg);
    ESP_LOGI(TAG, "STA: associating with \"%s\" (control AP %s)",
             s_ssid, s_ap_enabled ? "up" : "off");
    /* First start: STA_START fires -> on_wifi_event calls esp_wifi_connect().
     * Already started (re-provision): holding an IP -> disconnect() first (the
     * DISCONNECTED event re-drives connect() with the new config); else
     * connect() directly. */
    if (!s_wifi_started) {
        esp_wifi_start();
        s_wifi_started = true;
    } else if (s_sta_got_ip) {
        esp_wifi_disconnect();
    } else {
        esp_wifi_connect();
    }
}

static void apply_ap(void)
{
    /* No creds (or STA gave up): the AP must be up for setup/access. Use APSTA
     * so a later START_STA doesn't need a disruptive mode flip. */
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    ensure_ap_config();
    if (!s_wifi_started) {
        esp_wifi_start();
        s_wifi_started = true;
    }
    ESP_LOGW(TAG, "control AP up: SSID=\"%s\" PASS=\"%s\"  (join and open http://192.168.4.1)",
             s_ap_ssid, s_ap_pass);
    printf("DIAG|WIFI|AP|ssid=%s,pass=%s\n", s_ap_ssid, s_ap_pass);
    fflush(stdout);
}

static void apply_action(wifi_sm_action_t act)
{
    switch (act) {
    case WIFI_SM_ACT_START_STA: apply_sta(); break;
    case WIFI_SM_ACT_START_AP:  apply_ap();  break;
    case WIFI_SM_ACT_NONE:      break;
    }
}

/* ---- event handlers ---- */

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base; (void)data;
    switch (id) {
    case WIFI_EVENT_STA_START:
        esp_wifi_connect();
        break;
    case WIFI_EVENT_STA_DISCONNECTED:
        ESP_LOGW(TAG, "STA disconnected");
        s_sta_got_ip = false;
        apply_action(wifi_sm_on_disconnected(&s_sm));
        break;
    case WIFI_EVENT_AP_START:
        ESP_LOGI(TAG, "SoftAP started");
        break;
    default:
        break;
    }
}

static void on_ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base; (void)id;
    ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
    ESP_LOGI(TAG, "STA got IP: " IPSTR, IP2STR(&e->ip_info.ip));
    printf("DIAG|WIFI|STA|ssid=%s,ip=" IPSTR "\n", s_ssid, IP2STR(&e->ip_info.ip));
    fflush(stdout);
    s_sta_got_ip = true;
    wifi_sm_on_connected(&s_sm);
    start_mdns();
}

/* ---- MAC-derived AP password ---- */

static void derive_ap_password(void)
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    /* "audio-XXXXXX" — 12 chars, satisfies WPA2 8-char minimum. */
    snprintf(s_ap_pass, sizeof(s_ap_pass), "audio-%02x%02x%02x",
             mac[3], mac[4], mac[5]);
}

/* ---- public API ---- */

esp_err_t wifi_mgr_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    esp_err_t e = esp_event_loop_create_default();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return e;
    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    /* RADIO-2d: disable modem power-save. The IDF default (WIFI_PS_MIN_MODEM)
     * sleeps the radio between DTIM beacons, which throttles a sustained TCP
     * audio stream to ~100 kbps with heavy jitter — enough to starve the
     * decoder on a 128 kbps station (choppy playback). PS_NONE keeps the radio
     * awake for full throughput; this is a mains/USB-powered device so the
     * extra current draw is acceptable. */
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip_event, NULL, NULL));

    derive_ap_password();
    load_ap_creds();      /* user overrides of AP SSID/pass, if any */
    load_ap_enabled();
    bool has = load_creds();
    wifi_sm_init(&s_sm, has, WIFI_SM_DEFAULT_MAX_RETRIES);
    ESP_LOGI(TAG, "init: creds=%s -> %s", has ? "present" : "none",
             has ? "STA" : "AP provisioning");
    apply_action(wifi_sm_start(&s_sm));
    return ESP_OK;
}

esp_err_t wifi_mgr_set_creds(const char *ssid, const char *pass)
{
    if (!ssid || ssid[0] == '\0' || strlen(ssid) > WIFI_MGR_SSID_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t plen = pass ? strlen(pass) : 0;
    if (plen != 0 && (plen < 8 || plen > WIFI_MGR_PASS_MAX)) {
        return ESP_ERR_INVALID_ARG;   /* WPA2 needs 8..63; "" = open */
    }
    esp_err_t err = save_creds(ssid, pass);
    if (err != ESP_OK) return err;
    strlcpy(s_ssid, ssid, sizeof(s_ssid));
    strlcpy(s_pass, pass ? pass : "", sizeof(s_pass));
    apply_action(wifi_sm_on_set_creds(&s_sm));
    return ESP_OK;
}

esp_err_t wifi_mgr_reset(void)
{
    erase_creds();
    apply_action(wifi_sm_on_clear_creds(&s_sm));
    return ESP_OK;
}

void wifi_mgr_get_status(char *buf, size_t buf_sz)
{
    if (!buf || buf_sz == 0) return;
    wifi_sm_state_t st = wifi_sm_state(&s_sm);
    if (st == WIFI_SM_AP_MODE) {
        esp_netif_ip_info_t ip = {0};
        if (s_ap_netif) esp_netif_get_ip_info(s_ap_netif, &ip);
        wifi_sta_list_t clients = {0};
        esp_wifi_ap_get_sta_list(&clients);
        snprintf(buf, buf_sz, "MODE=AP,SSID=%s,IP=" IPSTR ",CLIENTS=%d",
                 s_ap_ssid, IP2STR(&ip.ip), clients.num);
        return;
    }
    const char *sname = (st == WIFI_SM_STA_CONNECTED) ? "CONNECTED" : "CONNECTING";
    esp_netif_ip_info_t ip = {0};
    if (s_sta_netif) esp_netif_get_ip_info(s_sta_netif, &ip);
    wifi_ap_record_t ap = {0};
    int rssi = (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) ? ap.rssi : 0;
    snprintf(buf, buf_sz, "MODE=STA,STATE=%s,SSID=%s,IP=" IPSTR ",RSSI=%d",
             sname, s_ssid, IP2STR(&ip.ip), rssi);
}

void wifi_mgr_get_info(wifi_mgr_info_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    wifi_sm_state_t st = wifi_sm_state(&s_sm);

    /* STA-side (the provisioning indicator the UI keys off). */
    if (st == WIFI_SM_AP_MODE) {
        strlcpy(out->mode, "AP", sizeof(out->mode));
        strlcpy(out->ssid, s_ap_ssid, sizeof(out->ssid));
    } else {
        esp_netif_ip_info_t ip = {0};
        if (s_sta_netif) esp_netif_get_ip_info(s_sta_netif, &ip);
        wifi_ap_record_t ap = {0};
        strlcpy(out->mode, "STA", sizeof(out->mode));
        strlcpy(out->state, (st == WIFI_SM_STA_CONNECTED) ? "CONNECTED" : "CONNECTING",
                sizeof(out->state));
        strlcpy(out->ssid, s_ssid, sizeof(out->ssid));
        snprintf(out->ip, sizeof(out->ip), IPSTR, IP2STR(&ip.ip));
        out->rssi = (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) ? ap.rssi : 0;
    }

    /* SoftAP block — reflects whatever the radio is actually running now. */
    wifi_mode_t mode = WIFI_MODE_NULL;
    esp_wifi_get_mode(&mode);
    out->ap_on = (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA);
    strlcpy(out->ap_ssid, s_ap_ssid, sizeof(out->ap_ssid));
    strlcpy(out->ap_pass, s_ap_pass, sizeof(out->ap_pass));
    if (out->ap_on) {
        esp_netif_ip_info_t apip = {0};
        if (s_ap_netif) esp_netif_get_ip_info(s_ap_netif, &apip);
        snprintf(out->ap_ip, sizeof(out->ap_ip), IPSTR, IP2STR(&apip.ip));
        wifi_sta_list_t clients = {0};
        if (esp_wifi_ap_get_sta_list(&clients) == ESP_OK) out->ap_clients = clients.num;
    }
}

bool wifi_mgr_ap_enabled(void)
{
    return s_ap_enabled;
}

esp_err_t wifi_mgr_set_ap_enabled(bool enabled)
{
    esp_err_t e = save_ap_enabled(enabled);
    s_ap_enabled = enabled;
    /* Flip the AP up/down WITHOUT disturbing an active STA link (just a mode
     * change). When there's no STA, force the AP up regardless of the flag. */
    wifi_sm_state_t st = wifi_sm_state(&s_sm);
    bool sta_active = (st == WIFI_SM_STA_CONNECTED || st == WIFI_SM_STA_CONNECTING);
    if (sta_active && !enabled) {
        esp_wifi_set_mode(WIFI_MODE_STA);
    } else {
        esp_wifi_set_mode(WIFI_MODE_APSTA);
        ensure_ap_config();
    }
    ESP_LOGI(TAG, "control AP %s", enabled ? "enabled" : "disabled");
    return e;
}

esp_err_t wifi_mgr_set_ap_config(const char *ssid, const char *pass)
{
    if (!ssid || ssid[0] == '\0' || strlen(ssid) > WIFI_MGR_SSID_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t plen = pass ? strlen(pass) : 0;
    if (plen != 0 && (plen < 8 || plen > WIFI_MGR_PASS_MAX)) {
        return ESP_ERR_INVALID_ARG;   /* WPA2 needs 8..64; "" = open AP */
    }
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_str(h, NVS_KEY_AP_SSID, ssid);
    if (err == ESP_OK) err = nvs_set_str(h, NVS_KEY_AP_PASS, pass ? pass : "");
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) return err;

    strlcpy(s_ap_ssid, ssid, sizeof(s_ap_ssid));
    strlcpy(s_ap_pass, pass ? pass : "", sizeof(s_ap_pass));
    /* Re-apply live if the AP is currently broadcasting — this bounces any
     * stations currently on the AP; they must rejoin with the new creds. */
    wifi_mode_t mode = WIFI_MODE_NULL;
    esp_wifi_get_mode(&mode);
    if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) ensure_ap_config();
    ESP_LOGI(TAG, "control AP creds updated: SSID=\"%s\"", s_ap_ssid);
    return ESP_OK;
}
