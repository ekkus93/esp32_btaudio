/*
 * wifi_mgr — device glue (WIFI-1b): esp_wifi / esp_netif / NVS creds / mDNS,
 * driving the pure wifi_sm (WIFI-1a). Boots into STA if credentials are stored,
 * else SoftAP provisioning ("ESP32-S3-Audio", WPA2, MAC-derived password
 * printed on the console). mDNS "esp-i2s-source.local" once on the LAN.
 *
 * Console/web provisioning (WIFI-1c / WEB-1b) call set_creds()/reset().
 */
#pragma once

#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_MGR_AP_SSID   "ESP32-S3-Audio"
#define WIFI_MGR_HOSTNAME  "esp-i2s-source"
#define WIFI_MGR_SSID_MAX  32
#define WIFI_MGR_PASS_MAX  64

/* Bring up netif/event/wifi, load stored creds, and start STA or AP per the
 * state machine. Call once after NVS init. */
esp_err_t wifi_mgr_init(void);

/* Provision STA credentials (from console/web): persist to NVS and (re)start
 * STA association. ssid 1..32 chars; pass "" (open) or 8..64 chars. */
esp_err_t wifi_mgr_set_creds(const char *ssid, const char *pass);

/* Clear stored credentials and drop back to AP provisioning mode (WIFI RESET). */
esp_err_t wifi_mgr_reset(void);

/* Human/parse-friendly status line into buf, e.g.
 * "MODE=STA,STATE=CONNECTED,SSID=home,IP=192.168.1.42,RSSI=-54" or
 * "MODE=AP,SSID=ESP32-S3-Audio,IP=192.168.4.1,CLIENTS=1". */
void wifi_mgr_get_status(char *buf, size_t buf_sz);

/* Structured snapshot (for the web API). */
typedef struct {
    char mode[4];    /* "STA" | "AP" — STA-side provisioning indicator */
    char state[12];  /* "CONNECTED" | "CONNECTING" | "" (AP) */
    char ssid[WIFI_MGR_SSID_MAX + 1];
    char ip[16];
    int  rssi;       /* STA only; 0 otherwise */
    /* SoftAP (concurrent AP+STA): the control AP is kept up alongside STA when
     * enabled, so the UI is always reachable via 192.168.4.1. */
    bool ap_on;      /* is the SoftAP currently broadcasting */
    char ap_ssid[WIFI_MGR_SSID_MAX + 1];
    char ap_pass[WIFI_MGR_PASS_MAX + 1];
    char ap_ip[16];
    int  ap_clients; /* stations currently associated to the SoftAP */
} wifi_mgr_info_t;

void wifi_mgr_get_info(wifi_mgr_info_t *out);

/* Concurrent-AP control: when enabled (default), the SoftAP stays up alongside
 * STA (APSTA). When disabled, the AP drops once STA is connected (STA-only).
 * The AP is always forced up when there are no STA creds (setup). Persisted. */
esp_err_t wifi_mgr_set_ap_enabled(bool enabled);
bool      wifi_mgr_ap_enabled(void);

/* Change the control-AP name/password (persisted to NVS, re-applied live).
 * ssid 1..32 chars; pass "" (open AP) or 8..64 chars (WPA2). Changing these
 * bounces any stations currently on the AP — they must rejoin. */
esp_err_t wifi_mgr_set_ap_config(const char *ssid, const char *pass);

#ifdef __cplusplus
}
#endif
