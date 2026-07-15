/*
 * wifi_mgr device glue (WIFI-1b). Drives the pure wifi_sm with esp_wifi
 * events, persists creds in NVS, brings up SoftAP provisioning or STA, and
 * publishes mDNS on the LAN. See wifi_mgr.h.
 */
#include "wifi_mgr.h"
#include "wifi_sm.h"

#include <string.h>
#include <ctype.h>

#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "nvs.h"
#include "mdns.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "wifi_mgr";

/* ---- NVS keys ---- */

#define NVS_NS          "wifi"
#define NVS_KEY_SSID    "ssid"
#define NVS_KEY_PASS    "pass"
#define NVS_KEY_AP_ON   "ap_on"
#define NVS_KEY_AP_SSID "ap_ssid"
#define NVS_KEY_AP_PASS "ap_pass"

/* ---- Lifecycle (8.1) ---- */

typedef enum {
    WIFI_MGR_STATE_UNINITIALIZED = 0,
    WIFI_MGR_STATE_INITIALIZING,
    WIFI_MGR_STATE_RUNNING,
    WIFI_MGR_STATE_FAULTED,
} wifi_mgr_state_t;

static SemaphoreHandle_t s_mgr_mtx;
static wifi_mgr_state_t   s_state;

/* ---- Internal state ---- */

static wifi_sm_t     s_sm;
static esp_netif_t  *s_sta_netif;
static esp_netif_t  *s_ap_netif;

/* Credentials (exact-length, NUL-terminated buffers). */
static char          s_ssid[WIFI_MGR_SSID_MAX + 1];
static size_t        s_ssid_len;
static char          s_pass[WIFI_MGR_PASS_MAX + 1];
static size_t        s_pass_len;

/* AP credentials */
static char          s_ap_ssid[WIFI_MGR_SSID_MAX + 1];
static size_t        s_ap_ssid_len;
static char          s_ap_pass[WIFI_MGR_PASS_MAX + 1];
static size_t        s_ap_pass_len;

static bool          s_mdns_up;
static bool          s_sta_got_ip;   /* true between GOT_IP and DISCONNECTED */
static bool          s_wifi_started;  /* esp_wifi_start() called (any mode) */
static bool          s_ap_enabled;    /* keep the control AP up alongside STA */

/* Provisioning lock — only one set_creds/reset job at a time (8.9). */
static bool          s_provisioning_job;

/* Connection generation for stale-event rejection (8.8). */
static uint32_t      s_connection_generation;

/* Stored event handler instances for cleanup (8.2). */
static esp_event_handler_instance_t s_wifi_handler_inst;
static esp_event_handler_instance_t s_ip_handler_inst;

/* ---- credential validation ---- */

/* 8.3: Exact 32-byte SSID handling */
static esp_err_t validate_ssid(const char *ssid, size_t *out_len)
{
    if (!ssid || !out_len) return ESP_ERR_INVALID_ARG;
    size_t len = strnlen(ssid, WIFI_MGR_SSID_MAX);
    if (len == 0 || len > WIFI_MGR_SSID_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }
    *out_len = len;
    return ESP_OK;
}

/* 8.4: Password validation with optional 64-char hex PSK */
static esp_err_t validate_sta_password(const char *pass, size_t *out_len)
{
    if (!pass || !out_len) return ESP_ERR_INVALID_ARG;
    size_t len = strnlen(pass, WIFI_MGR_PASS_MAX);

    if (len == 0) {
        /* Open network (empty password) */
        *out_len = 0;
        return ESP_OK;
    }
    if (len >= 8 && len <= 63) {
        /* Standard WPA2 passphrase */
        *out_len = len;
        return ESP_OK;
    }
#ifdef CONFIG_ESP_I2S_SOURCE_STA_HEX_PSK
    if (len == 64) {
        /* Raw hex PSK */
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

/* ---- credential persistence (8.5: transactional) ---- */

static esp_err_t load_creds(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err != ESP_OK) return err;

    s_ssid[0] = '\0';
    s_pass[0] = '\0';
    s_ssid_len = 0;
    s_pass_len = 0;

    size_t sl = sizeof(s_ssid), pl = sizeof(s_pass);
    esp_err_t e1 = nvs_get_str(h, NVS_KEY_SSID, s_ssid, &sl);
    esp_err_t e2 = nvs_get_str(h, NVS_KEY_PASS, s_pass, &pl);
    nvs_close(h);

    if (e1 == ESP_OK && sl > 0) {
        s_ssid_len = sl;
    }
    if (e2 == ESP_OK && pl > 0) {
        s_pass_len = pl;
    }

    if (s_ssid_len == 0 || s_ssid[0] == '\0') {
        /* No valid SSID */
        return ESP_ERR_NOT_FOUND;
    }

    return ESP_OK;
}

/* 8.5: Transactional credential update — build candidate, persist, then apply. */
static esp_err_t save_creds(const char *ssid, size_t ssid_len,
                            const char *pass, size_t pass_len)
{
    /* Build candidate in a local struct first */
    char tmp_ssid[WIFI_MGR_SSID_MAX + 1];
    char tmp_pass[WIFI_MGR_PASS_MAX + 1];

    memcpy(tmp_ssid, ssid, ssid_len);
    tmp_ssid[ssid_len] = '\0';
    memcpy(tmp_pass, pass ? pass : "", pass_len);
    tmp_pass[pass_len] = '\0';

    /* Persist to NVS */
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    err = nvs_set_str(h, NVS_KEY_SSID, tmp_ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(h, NVS_KEY_PASS, tmp_pass);
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);

    if (err != ESP_OK) return err;

    /* Only swap into RAM after successful persistence */
    memcpy(s_ssid, ssid, ssid_len);
    s_ssid[ssid_len] = '\0';
    s_ssid_len = ssid_len;
    if (pass && pass_len > 0) {
        memcpy(s_pass, pass, pass_len);
        s_pass[pass_len] = '\0';
        s_pass_len = pass_len;
    } else {
        s_pass[0] = '\0';
        s_pass_len = 0;
    }

    return ESP_OK;
}

/* 8.6: Erase semantics — erase both keys even if one is missing */
static esp_err_t erase_creds(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    /* Erase both keys — treat missing as success */
    esp_err_t e1 = nvs_erase_key(h, NVS_KEY_SSID);
    if (e1 != ESP_OK && e1 != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(h);
        return e1;
    }

    esp_err_t e2 = nvs_erase_key(h, NVS_KEY_PASS);
    if (e2 != ESP_OK && e2 != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(h);
        return e2;
    }

    err = nvs_commit(h);
    nvs_close(h);

    if (err != ESP_OK) return err;

    /* Clear RAM */
    s_ssid[0] = '\0';
    s_ssid_len = 0;
    s_pass[0] = '\0';
    s_pass_len = 0;

    return ESP_OK;
}

/* ---- AP credential helpers ---- */

static esp_err_t load_ap_enabled(void)
{
    s_ap_enabled = true;   /* default: keep the control AP up */
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err != ESP_OK) return err;

    uint8_t v = 1;
    if (nvs_get_u8(h, NVS_KEY_AP_ON, &v) == ESP_OK) {
        s_ap_enabled = (v != 0);
    }
    nvs_close(h);
    return ESP_OK;
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

/* Load user-customised control-AP SSID/password over the defaults. */
static void load_ap_creds(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;

    char tmp_ssid[WIFI_MGR_SSID_MAX + 1];
    char tmp_pass[WIFI_MGR_PASS_MAX + 1];
    size_t sl = sizeof(tmp_ssid), pl = sizeof(tmp_pass);

    tmp_ssid[0] = '\0';
    tmp_pass[0] = '\0';

    if (nvs_get_str(h, NVS_KEY_AP_SSID, tmp_ssid, &sl) == ESP_OK && tmp_ssid[0]) {
        memcpy(s_ap_ssid, tmp_ssid, sl);
        s_ap_ssid[sl] = '\0';
        s_ap_ssid_len = sl;
    }
    if (nvs_get_str(h, NVS_KEY_AP_PASS, tmp_pass, &pl) == ESP_OK) {
        memcpy(s_ap_pass, tmp_pass, pl);
        s_ap_pass[pl] = '\0';
        s_ap_pass_len = pl;
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

    /* 8.3: Exact SSID length */
    memcpy((char *)cfg.ap.ssid, s_ap_ssid, s_ap_ssid_len);
    cfg.ap.ssid_len = (uint8_t)s_ap_ssid_len;

    if (s_ap_pass_len > 0) {
        memcpy((char *)cfg.ap.password, s_ap_pass, s_ap_pass_len);
        cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        cfg.ap.authmode = WIFI_AUTH_OPEN;
    }
    cfg.ap.max_connection = 4;
    esp_wifi_set_config(WIFI_IF_AP, &cfg);
}

static void apply_sta(void)
{
    /* Concurrent AP+STA: keep the control AP up alongside STA when enabled, so
     * the UI stays reachable at 192.168.4.1. STA-only when the AP is disabled. */
    esp_wifi_set_mode(s_ap_enabled ? WIFI_MODE_APSTA : WIFI_MODE_STA);
    if (s_ap_enabled) ensure_ap_config();

    wifi_config_t cfg = {0};

    /* 8.3: Exact binary SSID copy (memcpy with len, not strlcpy) */
    memcpy(cfg.sta.ssid, s_ssid, s_ssid_len);

    /* Exact binary password copy */
    if (s_pass_len > 0) {
        memcpy(cfg.sta.password, s_pass, s_pass_len);
    }

    esp_wifi_set_config(WIFI_IF_STA, &cfg);
    ESP_LOGI(TAG, "STA: associating with SSID len=%zu (control AP %s)",
             s_ssid_len, s_ap_enabled ? "up" : "off");

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
    ESP_LOGW(TAG, "control AP up: SSID=\"%s\"  (join and open http://192.168.4.1)",
             s_ap_ssid);
    printf("DIAG|WIFI|AP|ssid=%s,pass=<redacted>\n", s_ap_ssid);
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

/* ---- event handlers (8.8: stale event rejection) ---- */

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

    /* Check state machine expects this event */
    wifi_sm_state_t st = wifi_sm_state(&s_sm);
    if (st != WIFI_SM_STA_CONNECTING && st != WIFI_SM_STA_CONNECTED) {
        ESP_LOGW(TAG, "ignoring stale GOT_IP event (state=%d)", (int)st);
        return;
    }

    ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
    ESP_LOGI(TAG, "STA got IP: " IPSTR, IP2STR(&e->ip_info.ip));
    printf("DIAG|WIFI|STA|ssid=%s,ip=" IPSTR "\n", s_ssid, IP2STR(&e->ip_info.ip));
    fflush(stdout);
    s_sta_got_ip = true;
    wifi_sm_on_connected(&s_sm);
    start_mdns();
}

/* ---- AP password derivation ---- */

static void derive_ap_password(void)
{
    snprintf(s_ap_pass, sizeof(s_ap_pass), "password");
    s_ap_pass_len = strnlen(s_ap_pass, WIFI_MGR_PASS_MAX);
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

    /* 8.2: Explicit error handling with goto-fail cleanup */
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        goto fail;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        goto fail;
    }

    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (!s_sta_netif) {
        err = ESP_ERR_NO_MEM;
        goto fail;
    }

    s_ap_netif = esp_netif_create_default_wifi_ap();
    if (!s_ap_netif) {
        err = ESP_ERR_NO_MEM;
        goto fail;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) goto fail;

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

    /* 8.2: Store event handler instances for later cleanup */
    s_wifi_handler_inst = 0;
    s_ip_handler_inst = 0;

    err = esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL,
        &s_wifi_handler_inst);
    if (err != ESP_OK) goto fail;

    err = esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip_event, NULL,
        &s_ip_handler_inst);
    if (err != ESP_OK) goto fail;

    derive_ap_password();
    load_ap_creds();       /* user overrides of AP SSID/pass, if any */
    load_ap_enabled();
    bool has = (load_creds() == ESP_OK);

    s_connection_generation = 0;
    s_provisioning_job = false;

    wifi_sm_init(&s_sm, has, WIFI_SM_DEFAULT_MAX_RETRIES);
    ESP_LOGI(TAG, "init: creds=%s -> %s", has ? "present" : "none",
             has ? "STA" : "AP provisioning");
    apply_action(wifi_sm_start(&s_sm));

    xSemaphoreTake(s_mgr_mtx, portMAX_DELAY);
    s_state = WIFI_MGR_STATE_RUNNING;
    xSemaphoreGive(s_mgr_mtx);
    return ESP_OK;

fail:
    /* Cleanup: unregister event handlers, reset state */
    if (s_wifi_handler_inst) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_wifi_handler_inst);
        s_wifi_handler_inst = 0;
    }
    if (s_ip_handler_inst) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, s_ip_handler_inst);
        s_ip_handler_inst = 0;
    }
    xSemaphoreTake(s_mgr_mtx, portMAX_DELAY);
    s_state = WIFI_MGR_STATE_UNINITIALIZED;
    xSemaphoreGive(s_mgr_mtx);
    return err;
}

esp_err_t wifi_mgr_set_creds(const char *ssid, const char *pass)
{
    /* Validate inputs first (before any mutation) */
    size_t ssid_len = 0;
    esp_err_t err = validate_ssid(ssid, &ssid_len);
    if (err != ESP_OK) return err;

    size_t pass_len = 0;
    err = validate_sta_password(pass, &pass_len);
    if (err != ESP_OK) return err;

    /* 8.9: Serialize provisioning jobs */
    xSemaphoreTake(s_mgr_mtx, portMAX_DELAY);
    if (s_provisioning_job) {
        xSemaphoreGive(s_mgr_mtx);
        return ESP_ERR_INVALID_STATE;  /* 409: provisioning already running */
    }
    s_provisioning_job = true;

    /* 8.5: Transactional — persist first, then apply */
    err = save_creds(ssid, ssid_len, pass, pass_len);
    if (err == ESP_OK) {
        apply_action(wifi_sm_on_set_creds(&s_sm));
    }

    s_provisioning_job = false;
    xSemaphoreGive(s_mgr_mtx);
    return err;
}

esp_err_t wifi_mgr_reset(void)
{
    xSemaphoreTake(s_mgr_mtx, portMAX_DELAY);
    if (s_provisioning_job) {
        xSemaphoreGive(s_mgr_mtx);
        return ESP_ERR_INVALID_STATE;  /* 409 */
    }
    s_provisioning_job = true;

    esp_err_t err = erase_creds();
    if (err == ESP_OK) {
        apply_action(wifi_sm_on_clear_creds(&s_sm));
    }

    s_provisioning_job = false;
    xSemaphoreGive(s_mgr_mtx);
    return err;
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
    /* 8.7: Don't expose password — only whether secured */
    out->ap_secured = (s_ap_pass_len > 0);

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
    size_t ssid_len = 0;
    esp_err_t err = validate_ssid(ssid, &ssid_len);
    if (err != ESP_OK) return err;

    size_t pass_len = 0;
    if (pass) {
        pass_len = strnlen(pass, WIFI_MGR_PASS_MAX);
        if (pass_len > 0 && pass_len < 8) {
            return ESP_ERR_INVALID_ARG;
        }
    }

    /* Persist */
    nvs_handle_t h;
    err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    char tmp_ssid[WIFI_MGR_SSID_MAX + 1];
    char tmp_pass[WIFI_MGR_PASS_MAX + 1];
    memcpy(tmp_ssid, ssid, ssid_len);
    tmp_ssid[ssid_len] = '\0';
    if (pass && pass_len > 0) {
        memcpy(tmp_pass, pass, pass_len);
    }
    tmp_pass[pass_len] = '\0';

    err = nvs_set_str(h, NVS_KEY_AP_SSID, tmp_ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(h, NVS_KEY_AP_PASS, tmp_pass);
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);

    if (err != ESP_OK) return err;

    /* Apply live */
    memcpy(s_ap_ssid, ssid, ssid_len);
    s_ap_ssid[ssid_len] = '\0';
    s_ap_ssid_len = ssid_len;
    if (pass && pass_len > 0) {
        memcpy(s_ap_pass, pass, pass_len);
        s_ap_pass[pass_len] = '\0';
        s_ap_pass_len = pass_len;
    } else {
        s_ap_pass[0] = '\0';
        s_ap_pass_len = 0;
    }

    /* Re-apply live if the AP is currently broadcasting — this bounces any
     * stations currently on the AP; they must rejoin with the new creds. */
    wifi_mode_t mode = WIFI_MODE_NULL;
    esp_wifi_get_mode(&mode);
    if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
        ensure_ap_config();
    }
    ESP_LOGI(TAG, "control AP creds updated: SSID=\"%s\"", s_ap_ssid);
    return ESP_OK;
}
