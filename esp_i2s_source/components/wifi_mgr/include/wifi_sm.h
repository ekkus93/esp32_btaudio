/*
 * wifi_sm — pure WiFi connection state machine (WIFI-1a). No ESP-IDF deps;
 * host-tested. The device layer (wifi_mgr.c, WIFI-1b) drives it: calls start()
 * at boot, feeds WiFi events (connected / disconnected) and provisioning
 * actions (set/clear creds), and performs the returned action.
 *
 * Policy (SPEC §4): boot with stored creds -> STA connect; on repeated failure
 * (>= max_retries consecutive) or no creds -> fall back to AP provisioning
 * mode. New creds (web/console provisioning) restart STA; WIFI RESET clears
 * creds and returns to AP mode. Credential *storage* is the device layer's job
 * (NVS); this SM only tracks whether creds exist and the retry count.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_SM_DEFAULT_MAX_RETRIES 5

typedef enum {
    WIFI_SM_INIT,            /* constructed, not started */
    WIFI_SM_STA_CONNECTING,  /* attempting STA association */
    WIFI_SM_STA_CONNECTED,   /* associated + got IP (on the LAN) */
    WIFI_SM_AP_MODE,         /* SoftAP provisioning fallback */
} wifi_sm_state_t;

/* Action the device layer must perform after an event. */
typedef enum {
    WIFI_SM_ACT_NONE,        /* nothing to do */
    WIFI_SM_ACT_START_STA,   /* (re)start STA association with stored creds */
    WIFI_SM_ACT_START_AP,    /* start SoftAP provisioning */
} wifi_sm_action_t;

typedef struct {
    wifi_sm_state_t state;
    bool     has_creds;   /* are STA credentials stored? */
    int      attempts;    /* consecutive failed STA attempts this cycle */
    int      max_retries; /* attempts before AP fallback (>=1) */

    /* observability */
    uint32_t sta_connects;   /* times we reached STA_CONNECTED */
    uint32_t ap_fallbacks;   /* times we entered AP_MODE */
    uint32_t disconnects;    /* STA disconnect events seen */
} wifi_sm_t;

/* Initialise. has_creds reflects whether NVS holds STA credentials.
 * max_retries <= 0 is clamped to WIFI_SM_DEFAULT_MAX_RETRIES. State = INIT. */
void wifi_sm_init(wifi_sm_t *s, bool has_creds, int max_retries);

/* Kick off after boot: STA if creds exist, else AP. */
wifi_sm_action_t wifi_sm_start(wifi_sm_t *s);

/* STA association succeeded (got IP). -> STA_CONNECTED, retry count reset. */
wifi_sm_action_t wifi_sm_on_connected(wifi_sm_t *s);

/* STA association failed or an established link dropped. Retries up to
 * max_retries, then falls back to AP mode. */
wifi_sm_action_t wifi_sm_on_disconnected(wifi_sm_t *s);

/* New STA credentials provided (web/console provisioning). Restart STA. */
wifi_sm_action_t wifi_sm_on_set_creds(wifi_sm_t *s);

/* Credentials cleared (WIFI RESET). Drop to AP provisioning mode. */
wifi_sm_action_t wifi_sm_on_clear_creds(wifi_sm_t *s);

wifi_sm_state_t wifi_sm_state(const wifi_sm_t *s);

#ifdef __cplusplus
}
#endif
