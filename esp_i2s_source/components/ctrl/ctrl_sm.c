/* ctrl_sm — boot orchestrator state machine (CTRL-1b), pure. See ctrl_sm.h. */
#include "ctrl_sm.h"

#include <string.h>

void ctrl_sm_init(ctrl_sm_t *sm, bool autostart_wanted, bool have_station)
{
    memset(sm, 0, sizeof(*sm));
    sm->have_station = have_station;
    sm->cfg.poll_interval_ms   = 3000;
    sm->cfg.connect_settle_ms  = 4000;
    sm->cfg.backoff_ms         = 5000;
    sm->cfg.max_retries        = 10;
    sm->state = autostart_wanted ? CTRL_ST_WAIT_WIFI : CTRL_ST_IDLE;
}

/* Enter a fresh phase: reset both timers. */
static void enter(ctrl_sm_t *sm, ctrl_state_t st)
{
    sm->state = st;
    sm->timer_ms = 0;
    sm->poll_ms = 0;
}

ctrl_action_t ctrl_sm_step(ctrl_sm_t *sm, const ctrl_input_t *in)
{
    switch (sm->state) {
    case CTRL_ST_IDLE:
    case CTRL_ST_GAVEUP:
        return CTRL_ACT_IDLE;

    case CTRL_ST_WAIT_WIFI:
        if (in->ev == CTRL_EV_WIFI_UP) {
            enter(sm, CTRL_ST_QUERY);
            return CTRL_ACT_SEND_STATUS;
        }
        return CTRL_ACT_WAIT;

    case CTRL_ST_QUERY:
        if (in->ev == CTRL_EV_STATUS) {
            if (in->connected) {          /* already linked -> just stream */
                enter(sm, CTRL_ST_STARTING);
                return CTRL_ACT_SEND_START;
            }
            enter(sm, CTRL_ST_CONNECTING);
            return CTRL_ACT_SEND_CONNECT;
        }
        return CTRL_ACT_WAIT;

    case CTRL_ST_CONNECTING:
        if (in->ev == CTRL_EV_CONNECT_ACK) {
            /* ok = CONNECT accepted (INITIATED). A rejected CONNECT backs off. */
            enter(sm, in->ok ? CTRL_ST_CONNECT_WAIT : CTRL_ST_BACKOFF);
        }
        return CTRL_ACT_WAIT;

    case CTRL_ST_CONNECT_WAIT:
        /* CONNECT completion is async and the WROOM32 exposes no pre-stream
         * "connected" flag, so settle briefly then START — START only returns
         * STARTED if the A2DP link actually came up (else -> backoff). */
        if (in->ev == CTRL_EV_TICK) {
            sm->timer_ms += in->dt_ms;
            if (sm->timer_ms >= sm->cfg.connect_settle_ms) {
                enter(sm, CTRL_ST_STARTING);
                return CTRL_ACT_SEND_START;
            }
        }
        return CTRL_ACT_WAIT;

    case CTRL_ST_STARTING:
        if (in->ev == CTRL_EV_START_ACK) {
            if (!in->ok) {
                enter(sm, CTRL_ST_BACKOFF);  /* link not up -> retry CONNECT */
                return CTRL_ACT_WAIT;
            }
            if (sm->have_station) {
                enter(sm, CTRL_ST_RESUMING);
                return CTRL_ACT_RESUME_RADIO;
            }
            enter(sm, CTRL_ST_RUNNING);   /* connected, no station to resume */
        }
        return CTRL_ACT_WAIT;

    case CTRL_ST_RESUMING:
        if (in->ev == CTRL_EV_RESUME_DONE) {
            enter(sm, CTRL_ST_RUNNING);
        }
        return CTRL_ACT_WAIT;

    case CTRL_ST_RUNNING:
        /* Steady: health-poll STATUS; a dropped link falls to backoff+reconnect
         * (successful reconnect clears the retry count). */
        if (in->ev == CTRL_EV_STATUS) {
            if (!in->connected) {
                enter(sm, CTRL_ST_BACKOFF);
            } else {
                sm->retries = 0;
            }
            return CTRL_ACT_WAIT;
        }
        if (in->ev == CTRL_EV_TICK) {
            sm->poll_ms += in->dt_ms;
            if (sm->poll_ms >= sm->cfg.poll_interval_ms) {
                sm->poll_ms = 0;
                return CTRL_ACT_SEND_STATUS;
            }
        }
        return CTRL_ACT_WAIT;

    case CTRL_ST_BACKOFF:
        if (in->ev == CTRL_EV_TICK) {
            sm->timer_ms += in->dt_ms;
            if (sm->timer_ms >= sm->cfg.backoff_ms) {
                sm->retries++;
                if (sm->cfg.max_retries >= 0 && sm->retries > sm->cfg.max_retries) {
                    enter(sm, CTRL_ST_GAVEUP);
                    return CTRL_ACT_IDLE;
                }
                enter(sm, CTRL_ST_CONNECTING);
                return CTRL_ACT_SEND_CONNECT;
            }
        }
        return CTRL_ACT_WAIT;
    }
    return CTRL_ACT_WAIT;
}

const char *ctrl_state_str(ctrl_state_t s)
{
    switch (s) {
    case CTRL_ST_IDLE:         return "IDLE";
    case CTRL_ST_WAIT_WIFI:    return "WAIT_WIFI";
    case CTRL_ST_QUERY:        return "QUERY";
    case CTRL_ST_CONNECTING:   return "CONNECTING";
    case CTRL_ST_CONNECT_WAIT: return "CONNECT_WAIT";
    case CTRL_ST_STARTING:     return "STARTING";
    case CTRL_ST_RESUMING:     return "RESUMING";
    case CTRL_ST_RUNNING:      return "RUNNING";
    case CTRL_ST_BACKOFF:      return "BACKOFF";
    case CTRL_ST_GAVEUP:       return "GAVEUP";
    default:                   return "?";
    }
}
