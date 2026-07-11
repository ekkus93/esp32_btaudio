/* WIFI-1a: pure WiFi STA/AP fallback state machine. See wifi_sm.h. */
#include "wifi_sm.h"

static wifi_sm_action_t go_ap(wifi_sm_t *s)
{
    s->state = WIFI_SM_AP_MODE;
    s->attempts = 0;
    s->ap_fallbacks++;
    return WIFI_SM_ACT_START_AP;
}

static wifi_sm_action_t go_sta(wifi_sm_t *s)
{
    s->state = WIFI_SM_STA_CONNECTING;
    s->attempts = 0;
    return WIFI_SM_ACT_START_STA;
}

void wifi_sm_init(wifi_sm_t *s, bool has_creds, int max_retries)
{
    s->state = WIFI_SM_INIT;
    s->has_creds = has_creds;
    s->attempts = 0;
    s->max_retries = (max_retries > 0) ? max_retries : WIFI_SM_DEFAULT_MAX_RETRIES;
    s->sta_connects = 0;
    s->ap_fallbacks = 0;
    s->disconnects = 0;
}

wifi_sm_action_t wifi_sm_start(wifi_sm_t *s)
{
    return s->has_creds ? go_sta(s) : go_ap(s);
}

wifi_sm_action_t wifi_sm_on_connected(wifi_sm_t *s)
{
    s->state = WIFI_SM_STA_CONNECTED;
    s->attempts = 0;
    s->sta_connects++;
    return WIFI_SM_ACT_NONE;
}

wifi_sm_action_t wifi_sm_on_disconnected(wifi_sm_t *s)
{
    s->disconnects++;
    s->attempts++;
    if (s->attempts >= s->max_retries) {
        return go_ap(s);
    }
    s->state = WIFI_SM_STA_CONNECTING;
    return WIFI_SM_ACT_START_STA;
}

wifi_sm_action_t wifi_sm_on_set_creds(wifi_sm_t *s)
{
    s->has_creds = true;
    return go_sta(s);
}

wifi_sm_action_t wifi_sm_on_clear_creds(wifi_sm_t *s)
{
    s->has_creds = false;
    return go_ap(s);
}

wifi_sm_state_t wifi_sm_state(const wifi_sm_t *s)
{
    return s->state;
}
