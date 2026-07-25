/*
 * wifi_mgr device glue (WIFI-1b). Drives the pure wifi_sm with esp_wifi
 * events, persists creds in NVS, brings up SoftAP provisioning or STA, and
 * publishes mDNS on the LAN. See wifi_mgr.h.
 *
 * FIX3 Phase 6: string-handling/default/lifecycle/error-propagation fixes
 * (WIFI-001..004). Credential validation and NVS-string-exactness invariants
 * are pure logic in wifi_creds_core.c (host-tested); this file is device
 * glue (NVS I/O, esp_wifi/esp_netif/esp_event/mdns) and is not host-tested,
 * same split as station_store.c/stations.c and web_ui_auth_core.c/
 * web_ui_auth.c elsewhere in this codebase — verified via idf.py build plus
 * a hardware smoke test.
 */
#include "wifi_mgr.h"
#include "wifi_mgr_internal.h"
#include "wifi_sm.h"
#include "wifi_creds_core.h"

#include <string.h>
#include <stdio.h>

#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "mdns.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "wifi_mgr";

/* NVS keys + the credential NVS persistence functions live in
 * wifi_mgr_internal.h / wifi_mgr_nvs.c. */

/* ---- Lifecycle (8.1) ---- */

typedef enum {
    WIFI_MGR_STATE_UNINITIALIZED = 0,
    WIFI_MGR_STATE_INITIALIZING,
    WIFI_MGR_STATE_RUNNING,
    WIFI_MGR_STATE_FAULTED,
} wifi_mgr_state_t;

static SemaphoreHandle_t s_mgr_mtx;
static wifi_mgr_state_t   s_state;
static esp_err_t          s_last_error = ESP_OK;

/* ---- Internal state ---- */

static wifi_sm_t     s_sm;
static esp_netif_t  *s_sta_netif;
static esp_netif_t  *s_ap_netif;

/* Persisted Wi-Fi config: STA creds, control-AP creds, and ap_enabled — all
 * exact-length NUL-terminated buffers. Read/written through wifi_nvs_*. */
static wifi_creds_t  s_creds;

static bool          s_mdns_up;         /* mdns_init() succeeded */
static bool          s_mdns_available;  /* mdns_up AND every subsequent call succeeded (6.10) */
static bool          s_sta_got_ip;   /* true between GOT_IP and DISCONNECTED */
static bool          s_wifi_started;  /* esp_wifi_start() called (any mode) */

/* Provisioning lock — only one set_creds/reset job at a time (8.9). */
static bool          s_provisioning_job;

/* Connection generation for stale-event rejection (8.8). */
static uint32_t      s_connection_generation;

/* Stored event handler instances for cleanup (8.2). */
static esp_event_handler_instance_t s_wifi_handler_inst;
static esp_event_handler_instance_t s_ip_handler_inst;

/* ---- diagnostics (6.9) ---- */

/* Non-fatal, asynchronous/event-driven failure: record + log + emit a
 * DIAG line, but do not change lifecycle state. Callers must not spin a
 * tight retry off of this — the bounded retry lives in wifi_sm. */
static void wifi_async_error(const char *operation, esp_err_t err)
{
    if (err == ESP_OK) return;
    s_last_error = err;
    ESP_LOGE(TAG, "%s failed: %s", operation, esp_err_to_name(err));
    printf("DIAG|WIFI|ERROR|op=%s,err=%s\n", operation, esp_err_to_name(err));
    fflush(stdout);
}

/* Fatal/irrecoverable mismatch (e.g. a persistence rollback itself failed):
 * enters FAULTED so callers stop trusting runtime state to match NVS. */
static void wifi_record_fault(const char *context, esp_err_t err)
{
    s_last_error = err;
    ESP_LOGE(TAG, "%s: entering FAULTED (%s)", context, esp_err_to_name(err));
    printf("DIAG|WIFI|FAULT|ctx=%s,err=%s\n", context, esp_err_to_name(err));
    fflush(stdout);
    if (s_mgr_mtx) {
        xSemaphoreTake(s_mgr_mtx, portMAX_DELAY);
        s_state = WIFI_MGR_STATE_FAULTED;
        xSemaphoreGive(s_mgr_mtx);
    }
}

/* 6.7: guard for every mutating public API — never take a NULL mutex, never
 * act on an uninitialized/faulted manager. */
static bool wifi_mgr_running(void)
{
    return s_mgr_mtx && s_state == WIFI_MGR_STATE_RUNNING;
}

/* 10.3: public capability check — callers (e.g. web_ui's request guards)
 * use this rather than assuming wifi_mgr_init() succeeded at boot. */
bool wifi_mgr_is_running(void)
{
    return wifi_mgr_running();
}

/* ---- mode application (6.4: propagate errors) ---- */

static esp_err_t start_mdns(void)
{
    if (s_mdns_up) return ESP_OK;

    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        wifi_async_error("mdns_init", err);
        return err;
    }
    s_mdns_up = true;

    /* 6.10: check every call. A failure here is a degraded subcapability —
     * Wi-Fi stays RUNNING, but availability must be visible in status. */
    esp_err_t e2 = mdns_hostname_set(WIFI_MGR_HOSTNAME);
    esp_err_t e3 = mdns_instance_name_set("ESP32-S3 Audio Source");
    esp_err_t e4 = mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    if (e2 != ESP_OK) wifi_async_error("mdns_hostname_set", e2);
    if (e3 != ESP_OK) wifi_async_error("mdns_instance_name_set", e3);
    if (e4 != ESP_OK) wifi_async_error("mdns_service_add", e4);

    s_mdns_available = (e2 == ESP_OK && e3 == ESP_OK && e4 == ESP_OK);
    ESP_LOGI(TAG, "mDNS %s: %s.local", s_mdns_available ? "up" : "degraded", WIFI_MGR_HOSTNAME);
    return ESP_OK;
}

/* Set the SoftAP interface config (idempotent). */
static esp_err_t ensure_ap_config(void)
{
    wifi_config_t cfg = {0};

    if (s_creds.ap_ssid_len == 0 || s_creds.ap_ssid_len > sizeof(cfg.ap.ssid)) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (s_creds.ap_pass_len > sizeof(cfg.ap.password)) {
        return ESP_ERR_INVALID_SIZE;
    }

    memcpy(cfg.ap.ssid, s_creds.ap_ssid, s_creds.ap_ssid_len);
    cfg.ap.ssid_len = (uint8_t)s_creds.ap_ssid_len;

    if (s_creds.ap_pass_len > 0) {
        memcpy(cfg.ap.password, s_creds.ap_pass, s_creds.ap_pass_len);
        cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        cfg.ap.authmode = WIFI_AUTH_OPEN;
    }
    cfg.ap.max_connection = 4;
    return esp_wifi_set_config(WIFI_IF_AP, &cfg);
}

static esp_err_t apply_sta(void)
{
    /* Concurrent AP+STA: keep the control AP up alongside STA when enabled, so
     * the UI stays reachable at 192.168.4.1. STA-only when the AP is disabled. */
    esp_err_t err = esp_wifi_set_mode(s_creds.ap_enabled ? WIFI_MODE_APSTA : WIFI_MODE_STA);
    if (err != ESP_OK) return err;

    if (s_creds.ap_enabled) {
        err = ensure_ap_config();
        if (err != ESP_OK) return err;
    }

    wifi_config_t cfg = {0};
    if (s_creds.ssid_len == 0 || s_creds.ssid_len > sizeof(cfg.sta.ssid)) return ESP_ERR_INVALID_SIZE;
    if (s_creds.pass_len > sizeof(cfg.sta.password)) return ESP_ERR_INVALID_SIZE;

    memcpy(cfg.sta.ssid, s_creds.ssid, s_creds.ssid_len);
    if (s_creds.pass_len > 0) {
        memcpy(cfg.sta.password, s_creds.pass, s_creds.pass_len);
    }

    err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (err != ESP_OK) return err;

    ESP_LOGI(TAG, "STA: associating with SSID len=%zu (control AP %s)",
             s_creds.ssid_len, s_creds.ap_enabled ? "up" : "off");

    /* First start: STA_START fires -> on_wifi_event calls esp_wifi_connect().
     * Already started (re-provision): holding an IP -> disconnect() first (the
     * DISCONNECTED event re-drives connect() with the new config); else
     * connect() directly. */
    if (!s_wifi_started) {
        err = esp_wifi_start();
        if (err != ESP_OK) return err;
        s_wifi_started = true;
        return ESP_OK;
    }
    return s_sta_got_ip ? esp_wifi_disconnect() : esp_wifi_connect();
}

static esp_err_t apply_ap(void)
{
    /* No creds (or STA gave up): the AP must be up for setup/access. Use APSTA
     * so a later START_STA doesn't need a disruptive mode flip. */
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err != ESP_OK) return err;

    err = ensure_ap_config();
    if (err != ESP_OK) return err;

    if (!s_wifi_started) {
        err = esp_wifi_start();
        if (err != ESP_OK) return err;
        s_wifi_started = true;
    }
    ESP_LOGW(TAG, "control AP up: SSID=\"%s\"  (join and open http://192.168.4.1)",
             s_creds.ap_ssid);
    printf("DIAG|WIFI|AP|ssid=%s,pass=<redacted>\n", s_creds.ap_ssid);
    fflush(stdout);
    return ESP_OK;
}

/* 6.9: single point that makes every action failure visible — event-driven,
 * so this never spins a tight loop; the bounded retry is wifi_sm's job. */
static esp_err_t apply_action(wifi_sm_action_t act)
{
    esp_err_t err;
    const char *what;
    switch (act) {
    case WIFI_SM_ACT_START_STA: err = apply_sta(); what = "apply_sta"; break;
    case WIFI_SM_ACT_START_AP:  err = apply_ap();  what = "apply_ap";  break;
    case WIFI_SM_ACT_NONE:      err = ESP_OK;      what = NULL;        break;
    default:                    err = ESP_OK;      what = NULL;        break;
    }
    if (err != ESP_OK && what) {
        wifi_async_error(what, err);
    }
    return err;
}

/* ---- event handlers (8.8: stale event rejection) ---- */

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base; (void)data;
    switch (id) {
    case WIFI_EVENT_STA_START: {
        /* Max out TX power (84 = 20 dBm, the ceiling) for the best link
         * margin on a weak/congested AP. The STA's own uplink paces the L2
         * and TCP ACKs that gate DOWNLOAD throughput, so a stronger uplink
         * directly helps a marginal audio stream from starving the decoder.
         * Same mains/USB-powered rationale as WIFI_PS_NONE above; non-fatal. */
        esp_err_t tx = esp_wifi_set_max_tx_power(84);
        if (tx != ESP_OK) {
            ESP_LOGW(TAG, "set_max_tx_power failed: %s", esp_err_to_name(tx));
        }
        esp_err_t err = esp_wifi_connect();
        if (err != ESP_OK) wifi_async_error("esp_wifi_connect(STA_START)", err);
        break;
    }
    case WIFI_EVENT_STA_DISCONNECTED:
        ESP_LOGW(TAG, "STA disconnected");
        s_sta_got_ip = false;
        ++s_connection_generation;
        apply_action(wifi_sm_on_disconnected(&s_sm));
        break;
    case WIFI_EVENT_AP_START:
        ESP_LOGI(TAG, "SoftAP started");
        break;
    default:
        break;
    }
}

/* 8.8: Reject stale Wi-Fi events — only accept GOT_IP when the state machine
 * expects it (STA_CONNECTING or STA_CONNECTED). */
static void on_ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base; (void)id;

    wifi_sm_state_t st = wifi_sm_state(&s_sm);
    if (st != WIFI_SM_STA_CONNECTING && st != WIFI_SM_STA_CONNECTED) {
        ESP_LOGW(TAG, "ignoring stale GOT_IP event (state=%d)", (int)st);
        return;
    }

    ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
    ESP_LOGI(TAG, "STA got IP: " IPSTR, IP2STR(&e->ip_info.ip));
    printf("DIAG|WIFI|STA|ssid=%s,ip=" IPSTR "\n", s_creds.ssid, IP2STR(&e->ip_info.ip));
    fflush(stdout);
    s_sta_got_ip = true;
    wifi_sm_on_connected(&s_sm);
    start_mdns();
}

/* ---- AP enabled/config transactional updates (6.8) ---- */

static esp_err_t apply_ap_enabled_live(bool enabled)
{
    wifi_sm_state_t st = wifi_sm_state(&s_sm);
    bool sta_active = (st == WIFI_SM_STA_CONNECTED || st == WIFI_SM_STA_CONNECTING);
    if (sta_active && !enabled) {
        return esp_wifi_set_mode(WIFI_MODE_STA);
    }
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err != ESP_OK) return err;
    return ensure_ap_config();
}

/* ---- public API ---- */

esp_err_t wifi_mgr_init(void)
{
    /* 8.1: Lifecycle mutex + state check */
    if (!s_mgr_mtx) {
        s_mgr_mtx = xSemaphoreCreateMutex();
        if (!s_mgr_mtx) return ESP_ERR_NO_MEM;
    }

    xSemaphoreTake(s_mgr_mtx, portMAX_DELAY);
    if (s_state == WIFI_MGR_STATE_RUNNING) {
        /* Idempotent — already initialized */
        xSemaphoreGive(s_mgr_mtx);
        return ESP_OK;
    }
    if (s_state != WIFI_MGR_STATE_UNINITIALIZED) {
        xSemaphoreGive(s_mgr_mtx);
        return ESP_ERR_INVALID_STATE;
    }
    s_state = WIFI_MGR_STATE_INITIALIZING;
    xSemaphoreGive(s_mgr_mtx);

    /* 6.6: track exactly what THIS attempt created/owns, for a precise
     * reverse-order unwind on failure. */
    bool sta_netif_created = false;
    bool ap_netif_created = false;
    bool wifi_driver_inited = false;
    bool wifi_handler_registered = false;
    bool ip_handler_registered = false;

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) goto fail;

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) goto fail;

    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (!s_sta_netif) {
        err = ESP_ERR_NO_MEM;
        goto fail;
    }
    sta_netif_created = true;

    s_ap_netif = esp_netif_create_default_wifi_ap();
    if (!s_ap_netif) {
        err = ESP_ERR_NO_MEM;
        goto fail;
    }
    ap_netif_created = true;

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) goto fail;
    wifi_driver_inited = true;

    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) goto fail;

    /* RADIO-2d: disable modem power-save. The IDF default (WIFI_PS_MIN_MODEM)
     * sleeps the radio between DTIM beacons, which throttles a sustained TCP
     * audio stream to ~100 kbps with heavy jitter — enough to starve the
     * decoder on a 128 kbps station (choppy playback). PS_NONE keeps the
     * radio awake for full throughput; this is a mains/USB-powered device so
     * the extra current draw is acceptable. */
    err = esp_wifi_set_ps(WIFI_PS_NONE);
    if (err != ESP_OK) goto fail;

    s_wifi_handler_inst = 0;
    s_ip_handler_inst = 0;

    err = esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL,
        &s_wifi_handler_inst);
    if (err != ESP_OK) goto fail;
    wifi_handler_registered = true;

    err = esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip_event, NULL,
        &s_ip_handler_inst);
    if (err != ESP_OK) goto fail;
    ip_handler_registered = true;

    /* 6.3: documented defaults first, then optional overrides. */
    wifi_nvs_set_default_ap_creds(&s_creds);
    esp_err_t ap_creds_err = wifi_nvs_load_ap_creds(&s_creds);
    if (ap_creds_err != ESP_OK) {
        wifi_async_error("load_ap_creds", ap_creds_err);
        ESP_LOGW(TAG, "control-AP override corrupt (%s) -> using defaults",
                 esp_err_to_name(ap_creds_err));
    }
    wifi_nvs_load_ap_enabled(&s_creds);

    esp_err_t creds_err = wifi_nvs_load_creds(&s_creds);
    bool has = (creds_err == ESP_OK);
    if (creds_err != ESP_OK && creds_err != ESP_ERR_NOT_FOUND) {
        wifi_async_error("load_creds", creds_err);
        ESP_LOGW(TAG, "stored STA credentials unusable (%s) -> AP provisioning",
                 esp_err_to_name(creds_err));
    }

    s_connection_generation = 0;
    s_provisioning_job = false;

    wifi_sm_init(&s_sm, has, WIFI_SM_DEFAULT_MAX_RETRIES);
    ESP_LOGI(TAG, "init: creds=%s -> %s", has ? "present" : "none",
             has ? "STA" : "AP provisioning");

    /* 6.5: do not publish RUNNING until the initial action actually succeeds. */
    err = apply_action(wifi_sm_start(&s_sm));
    if (err != ESP_OK) goto fail;

    xSemaphoreTake(s_mgr_mtx, portMAX_DELAY);
    s_state = WIFI_MGR_STATE_RUNNING;
    xSemaphoreGive(s_mgr_mtx);
    return ESP_OK;

fail: {
    /* 6.6: unwind in reverse order; only touch what this attempt created. */
    esp_err_t unwind_err = ESP_OK;

    if (s_wifi_started) {
        esp_err_t e = esp_wifi_stop();
        if (e != ESP_OK) unwind_err = e;
        s_wifi_started = false;
    }
    if (ip_handler_registered) {
        esp_err_t e = esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, s_ip_handler_inst);
        if (e != ESP_OK) unwind_err = e;
        s_ip_handler_inst = 0;
    }
    if (wifi_handler_registered) {
        esp_err_t e = esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_wifi_handler_inst);
        if (e != ESP_OK) unwind_err = e;
        s_wifi_handler_inst = 0;
    }
    if (wifi_driver_inited) {
        esp_err_t e = esp_wifi_deinit();
        if (e != ESP_OK) unwind_err = e;
    }
    if (sta_netif_created) {
        esp_netif_destroy_default_wifi(s_sta_netif);
        s_sta_netif = NULL;
    }
    if (ap_netif_created) {
        esp_netif_destroy_default_wifi(s_ap_netif);
        s_ap_netif = NULL;
    }
    /* Do not destroy the global default event loop/netif subsystem — this
     * component does not own it (ESP_ERR_INVALID_STATE above just means
     * some other component already created it). */

    xSemaphoreTake(s_mgr_mtx, portMAX_DELAY);
    s_state = (unwind_err == ESP_OK) ? WIFI_MGR_STATE_UNINITIALIZED : WIFI_MGR_STATE_FAULTED;
    xSemaphoreGive(s_mgr_mtx);

    if (unwind_err != ESP_OK) {
        wifi_record_fault("init_unwind", unwind_err);
    }
    return err;
}
}

esp_err_t wifi_mgr_set_creds(const char *ssid, const char *pass)
{
    if (!wifi_mgr_running()) return ESP_ERR_INVALID_STATE;

    /* Validate inputs first (before any mutation) */
    size_t ssid_len = 0;
    esp_err_t err = wifi_creds_validate_ssid(ssid, &ssid_len);
    if (err != ESP_OK) return err;

    size_t pass_len = 0;
    err = wifi_creds_validate_sta_password(pass, &pass_len);
    if (err != ESP_OK) return err;

    /* 8.9: Serialize provisioning jobs */
    xSemaphoreTake(s_mgr_mtx, portMAX_DELAY);
    if (s_provisioning_job) {
        xSemaphoreGive(s_mgr_mtx);
        return ESP_ERR_INVALID_STATE;  /* 409: provisioning already running */
    }
    s_provisioning_job = true;

    /* 8.5: Transactional — persist first, then apply */
    err = wifi_nvs_save_creds(&s_creds, ssid, ssid_len, pass, pass_len);
    if (err == ESP_OK) {
        esp_err_t apply_err = apply_action(wifi_sm_on_set_creds(&s_sm));
        if (apply_err != ESP_OK) err = apply_err;
    }

    s_provisioning_job = false;
    xSemaphoreGive(s_mgr_mtx);
    return err;
}

esp_err_t wifi_mgr_reset(void)
{
    if (!wifi_mgr_running()) return ESP_ERR_INVALID_STATE;

    xSemaphoreTake(s_mgr_mtx, portMAX_DELAY);
    if (s_provisioning_job) {
        xSemaphoreGive(s_mgr_mtx);
        return ESP_ERR_INVALID_STATE;  /* 409 */
    }
    s_provisioning_job = true;

    esp_err_t err = wifi_nvs_erase_creds(&s_creds);
    if (err == ESP_OK) {
        esp_err_t apply_err = apply_action(wifi_sm_on_clear_creds(&s_sm));
        if (apply_err != ESP_OK) err = apply_err;
    }

    s_provisioning_job = false;
    xSemaphoreGive(s_mgr_mtx);
    return err;
}

void wifi_mgr_get_status(char *buf, size_t buf_sz)
{
    if (!buf || buf_sz == 0) return;
    if (!wifi_mgr_running()) {
        snprintf(buf, buf_sz, "MODE=UNAVAILABLE");
        return;
    }

    wifi_sm_state_t st = wifi_sm_state(&s_sm);
    if (st == WIFI_SM_AP_MODE) {
        esp_netif_ip_info_t ip = {0};
        if (s_ap_netif) esp_netif_get_ip_info(s_ap_netif, &ip);
        wifi_sta_list_t clients = {0};
        esp_wifi_ap_get_sta_list(&clients);
        snprintf(buf, buf_sz, "MODE=AP,SSID=%s,IP=" IPSTR ",CLIENTS=%d",
                 s_creds.ap_ssid, IP2STR(&ip.ip), clients.num);
        return;
    }
    const char *sname = (st == WIFI_SM_STA_CONNECTED) ? "CONNECTED" : "CONNECTING";
    esp_netif_ip_info_t ip = {0};
    if (s_sta_netif) esp_netif_get_ip_info(s_sta_netif, &ip);
    wifi_ap_record_t ap = {0};
    int rssi = (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) ? ap.rssi : 0;
    snprintf(buf, buf_sz, "MODE=STA,STATE=%s,SSID=%s,IP=" IPSTR ",RSSI=%d,MDNS=%s",
             sname, s_creds.ssid, IP2STR(&ip.ip), rssi, s_mdns_available ? "UP" : "DEGRADED");
}

void wifi_mgr_get_info(wifi_mgr_info_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!wifi_mgr_running()) {
        /* 6.7: explicit unavailable snapshot rather than reading s_sm before
         * it has been initialized. */
        strlcpy(out->mode, "N/A", sizeof(out->mode));
        return;
    }

    wifi_sm_state_t st = wifi_sm_state(&s_sm);

    /* STA-side (the provisioning indicator the UI keys off). */
    if (st == WIFI_SM_AP_MODE) {
        strlcpy(out->mode, "AP", sizeof(out->mode));
        strlcpy(out->ssid, s_creds.ap_ssid, sizeof(out->ssid));
    } else {
        esp_netif_ip_info_t ip = {0};
        if (s_sta_netif) esp_netif_get_ip_info(s_sta_netif, &ip);
        wifi_ap_record_t ap = {0};
        strlcpy(out->mode, "STA", sizeof(out->mode));
        strlcpy(out->state, (st == WIFI_SM_STA_CONNECTED) ? "CONNECTED" : "CONNECTING",
                sizeof(out->state));
        strlcpy(out->ssid, s_creds.ssid, sizeof(out->ssid));
        snprintf(out->ip, sizeof(out->ip), IPSTR, IP2STR(&ip.ip));
        out->rssi = (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) ? ap.rssi : 0;
    }

    /* SoftAP block — reflects whatever the radio is actually running now. */
    wifi_mode_t mode = WIFI_MODE_NULL;
    esp_wifi_get_mode(&mode);
    out->ap_on = (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA);
    strlcpy(out->ap_ssid, s_creds.ap_ssid, sizeof(out->ap_ssid));
    /* 8.7: Don't expose password — only whether secured */
    out->ap_secured = (s_creds.ap_pass_len > 0);

    if (out->ap_on) {
        esp_netif_ip_info_t apip = {0};
        if (s_ap_netif) esp_netif_get_ip_info(s_ap_netif, &apip);
        snprintf(out->ap_ip, sizeof(out->ap_ip), IPSTR, IP2STR(&apip.ip));
        wifi_sta_list_t clients = {0};
        if (esp_wifi_ap_get_sta_list(&clients) == ESP_OK) out->ap_clients = clients.num;
    }

    out->mdns_available = s_mdns_available;
}

bool wifi_mgr_ap_enabled(void)
{
    return s_creds.ap_enabled;
}

esp_err_t wifi_mgr_set_ap_enabled(bool enabled)
{
    if (!wifi_mgr_running()) return ESP_ERR_INVALID_STATE;

    /* 6.8: persist -> apply -> publish only after driver success; roll back
     * persistence if the live apply fails. */
    bool old_enabled = s_creds.ap_enabled;
    esp_err_t err = wifi_nvs_save_ap_enabled(enabled);
    if (err != ESP_OK) return err;

    esp_err_t apply_err = apply_ap_enabled_live(enabled);
    if (apply_err != ESP_OK) {
        esp_err_t rollback = wifi_nvs_save_ap_enabled(old_enabled);
        if (rollback != ESP_OK) {
            wifi_record_fault("ap_enabled_rollback", rollback);
        }
        return apply_err;
    }

    s_creds.ap_enabled = enabled;
    ESP_LOGI(TAG, "control AP %s", enabled ? "enabled" : "disabled");
    return ESP_OK;
}

esp_err_t wifi_mgr_set_ap_config(const char *ssid, const char *pass)
{
    if (!wifi_mgr_running()) return ESP_ERR_INVALID_STATE;

    size_t ssid_len = 0;
    esp_err_t err = wifi_creds_validate_ssid(ssid, &ssid_len);
    if (err != ESP_OK) return err;

    size_t pass_len = 0;
    err = wifi_creds_validate_ap_password(pass, &pass_len);
    if (err != ESP_OK) return err;

    /* 6.8: snapshot old runtime values so a failed live apply can restore
     * them exactly, in both RAM and NVS. */
    char old_ssid[WIFI_MGR_SSID_MAX + 1];
    char old_pass[WIFI_MGR_PASS_MAX + 1];
    size_t old_ssid_len = s_creds.ap_ssid_len;
    size_t old_pass_len = s_creds.ap_pass_len;
    memcpy(old_ssid, s_creds.ap_ssid, old_ssid_len + 1u);
    memcpy(old_pass, s_creds.ap_pass, old_pass_len + 1u);

    char tmp_ssid[WIFI_MGR_SSID_MAX + 1];
    char tmp_pass[WIFI_MGR_PASS_MAX + 1];
    memcpy(tmp_ssid, ssid, ssid_len);
    tmp_ssid[ssid_len] = '\0';
    if (pass && pass_len > 0) {
        memcpy(tmp_pass, pass, pass_len);
    }
    tmp_pass[pass_len] = '\0';

    /* Persist candidate. */
    err = wifi_nvs_write_ap_creds(tmp_ssid, tmp_pass);
    if (err != ESP_OK) return err;  /* persistence failed -> runtime state untouched */

    /* Apply to RAM, then (if currently broadcasting) the live driver config. */
    memcpy(s_creds.ap_ssid, tmp_ssid, ssid_len);
    s_creds.ap_ssid[ssid_len] = '\0';
    s_creds.ap_ssid_len = ssid_len;
    memcpy(s_creds.ap_pass, tmp_pass, pass_len);
    s_creds.ap_pass[pass_len] = '\0';
    s_creds.ap_pass_len = pass_len;

    wifi_mode_t mode = WIFI_MODE_NULL;
    esp_wifi_get_mode(&mode);
    if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
        esp_err_t apply_err = ensure_ap_config();
        if (apply_err != ESP_OK) {
            /* Roll back RAM + NVS to the prior values (6.8). */
            s_creds.ap_ssid_len = old_ssid_len;
            memcpy(s_creds.ap_ssid, old_ssid, old_ssid_len + 1u);
            s_creds.ap_pass_len = old_pass_len;
            memcpy(s_creds.ap_pass, old_pass, old_pass_len + 1u);

            esp_err_t rb = wifi_nvs_write_ap_creds(old_ssid, old_pass);
            if (rb != ESP_OK) {
                wifi_record_fault("ap_config_rollback", rb);
            }
            return apply_err;
        }
    }

    ESP_LOGI(TAG, "control AP creds updated: SSID=\"%s\"", s_creds.ap_ssid);
    return ESP_OK;
}
