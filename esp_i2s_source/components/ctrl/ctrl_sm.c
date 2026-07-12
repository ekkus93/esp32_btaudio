/* ctrl_sm — boot orchestrator state machine (CTRL-1b), pure. See ctrl_sm.h. */
#include "ctrl_sm.h"

#include <string.h>

void ctrl_sm_init(ctrl_sm_t *sm, bool autostart_wanted, bool have_station)
{
    memset(sm, 0, sizeof(*sm));
    sm->have_station = have_station;
    sm->cfg.poll_interval_ms   = 2000;
    sm->cfg.connect_timeout_ms = 20000;  /* generous: earbuds link slowly */
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
            /* ok = CONNECT accepted (INITIATED). A rejected CONNECT backs off.
             * Otherwise nudge START and then wait for the async link to come up
             * (confirmed by STATUS RUN=1) rather than trusting START's result. */
            if (in->ok) {
                enter(sm, CTRL_ST_STARTING);
                return CTRL_ACT_SEND_START;
            }
            enter(sm, CTRL_ST_BACKOFF);
        }
        return CTRL_ACT_WAIT;

    case CTRL_ST_STARTING:
        /* START is only a nudge — some sinks auto-start media on connect, and a
         * slow sink (earbuds) isn't linked yet when START is sent, so START may
         * report failure even though the connection is coming up. Ignore its
         * result and poll STATUS for the real RUN=1 confirmation. */
        if (in->ev == CTRL_EV_START_ACK) {
            enter(sm, CTRL_ST_CONNECT_WAIT);
        }
        return CTRL_ACT_WAIT;

    case CTRL_ST_CONNECT_WAIT:
        /* Poll STATUS until the sink is actually connected+streaming (RUN=1),
         * or give up this attempt after connect_timeout and back off. */
        if (in->ev == CTRL_EV_STATUS) {
            if (in->connected) {
                if (sm->have_station) {
                    enter(sm, CTRL_ST_RESUMING);
                    return CTRL_ACT_RESUME_RADIO;
                }
                enter(sm, CTRL_ST_RUNNING);
            }
            return CTRL_ACT_WAIT;         /* not up yet; await next poll */
        }
        if (in->ev == CTRL_EV_TICK) {
            sm->timer_ms += in->dt_ms;
            sm->poll_ms  += in->dt_ms;
            if (sm->timer_ms >= sm->cfg.connect_timeout_ms) {
                enter(sm, CTRL_ST_BACKOFF);
                return CTRL_ACT_WAIT;
            }
            if (sm->poll_ms >= sm->cfg.poll_interval_ms) {
                sm->poll_ms = 0;
                return CTRL_ACT_SEND_STATUS;
            }
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
