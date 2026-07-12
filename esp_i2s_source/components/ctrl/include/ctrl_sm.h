/*
 * ctrl_sm — boot orchestrator state machine (CTRL-1b), pure and host-tested.
 *
 * Drives: boot -> WiFi up -> STATUS -> CONNECT <sink> -> START -> resume last
 * station -> steady RUNNING with health polling -> reconnect on drop. The
 * machine is I/O-free: ctrl_sm_step() consumes one input event and returns the
 * next action for the device glue (ctrl.c) to perform (send a bt_link command,
 * resume radio, or wait). Results of those actions are fed back as the next
 * event. Time advances via CTRL_EV_TICK with a dt_ms, so timeouts/backoff are
 * fully deterministic under test.
 *
 * Note (see CTRL-1b docs): the WROOM32 emits no connect/disconnect EVENT, so
 * "reconnect on drop" is realised by periodic STATUS polling (RUN/connected),
 * not a push event — the FSM is agnostic to which, it just consumes STATUS.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CTRL_ST_IDLE,          /* autostart off / no sink -> manual mode, terminal */
    CTRL_ST_WAIT_WIFI,     /* waiting for WiFi before orchestrating */
    CTRL_ST_QUERY,         /* STATUS sent, awaiting reply */
    CTRL_ST_CONNECTING,    /* CONNECT sent, awaiting ack */
    CTRL_ST_STARTING,      /* START nudge sent, awaiting ack (result ignored) */
    CTRL_ST_CONNECT_WAIT,  /* polling STATUS until RUN=1 (link up) or timeout */
    CTRL_ST_RESUMING,      /* radio resume dispatched */
    CTRL_ST_RUNNING,       /* connected + streaming; health-poll STATUS */
    CTRL_ST_BACKOFF,       /* dropped/failed; wait then retry CONNECT */
    CTRL_ST_GAVEUP,        /* exhausted retries, terminal */
} ctrl_state_t;

typedef enum {
    CTRL_EV_TICK,          /* dt_ms elapsed, no external change */
    CTRL_EV_WIFI_UP,       /* WiFi reached CONNECTED */
    CTRL_EV_STATUS,        /* STATUS reply: in.connected */
    CTRL_EV_CONNECT_ACK,   /* CONNECT reply: in.ok = INITIATED accepted */
    CTRL_EV_START_ACK,     /* START reply: in.ok = STARTED */
    CTRL_EV_RESUME_DONE,   /* radio resume was dispatched */
} ctrl_event_t;

typedef enum {
    CTRL_ACT_WAIT,         /* do nothing this step */
    CTRL_ACT_SEND_STATUS,  /* send "STATUS", feed reply as CTRL_EV_STATUS */
    CTRL_ACT_SEND_CONNECT, /* send "CONNECT <mac>", feed CTRL_EV_CONNECT_ACK */
    CTRL_ACT_SEND_START,   /* send "START", feed CTRL_EV_START_ACK */
    CTRL_ACT_RESUME_RADIO, /* radio_play(last_station), feed CTRL_EV_RESUME_DONE */
    CTRL_ACT_IDLE,         /* orchestration finished / not wanted */
} ctrl_action_t;

typedef struct {
    ctrl_event_t ev;
    uint32_t     dt_ms;     /* for CTRL_EV_TICK */
    bool         connected; /* for CTRL_EV_STATUS */
    bool         ok;        /* for CTRL_EV_*_ACK */
} ctrl_input_t;

/* Tunables (ms / counts). Defaults set by ctrl_sm_init; overwrite before use
 * in a test to shorten waits. */
typedef struct {
    uint32_t poll_interval_ms;  /* gap between STATUS polls (connect-wait + running) */
    uint32_t connect_timeout_ms;/* max wait for RUN=1 after CONNECT before backoff */
    uint32_t backoff_ms;        /* wait before a reconnect attempt */
    int      max_retries;       /* CONNECT attempts before GAVEUP (<0 = infinite) */
} ctrl_sm_cfg_t;

typedef struct {
    ctrl_state_t  state;
    bool          have_station;  /* resume a station vs go idle-connected */
    int           retries;
    uint32_t      timer_ms;      /* phase elapsed (timeout / backoff) */
    uint32_t      poll_ms;       /* elapsed since last STATUS poll */
    ctrl_sm_cfg_t cfg;
} ctrl_sm_t;

/* Initialise. autostart_wanted = (cfg.autostart && valid sink MAC).
 * have_station = (cfg.last_station >= 0). Sets sensible default tunables. */
void ctrl_sm_init(ctrl_sm_t *sm, bool autostart_wanted, bool have_station);

/* Advance the machine by one input event; returns the action to perform. */
ctrl_action_t ctrl_sm_step(ctrl_sm_t *sm, const ctrl_input_t *in);

/* Introspection (for logs/tests). */
const char *ctrl_state_str(ctrl_state_t s);

#ifdef __cplusplus
}
#endif
