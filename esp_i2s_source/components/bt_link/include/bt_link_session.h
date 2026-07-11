/*
 * bt_link_session — pure command/response session state machine (LINK-1b).
 * No ESP-IDF deps; host-tested. The device UART task (LINK-1c) drives it:
 * feeds parsed messages in, calls begin() when it sends a command, and ticks
 * the clock. Owns:
 *   - one in-flight command with verb correlation + terminal completion
 *   - timeout accounting (retry is the caller's policy)
 *   - EVENT fan-out to multiple subscribers, even while a command is pending
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "bt_link_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BT_LINK_MAX_SUBSCRIBERS 4
#define BT_LINK_VERB_MAX        32
#define BT_LINK_FIELD_MAX       256

typedef enum {
    BT_LINK_CMD_IDLE,      /* nothing in flight */
    BT_LINK_CMD_PENDING,   /* command sent, awaiting terminal response */
    BT_LINK_CMD_DONE_OK,   /* completed with OK */
    BT_LINK_CMD_DONE_ERR,  /* completed with ERR */
    BT_LINK_CMD_TIMEOUT,   /* no terminal response within timeout */
} bt_link_cmd_state_t;

typedef void (*bt_link_event_cb)(void *ctx, const bt_link_msg_t *evt);

typedef struct {
    bt_link_cmd_state_t state;
    char     verb[BT_LINK_VERB_MAX];       /* verb we expect echoed back */
    uint32_t elapsed_ms;
    uint32_t timeout_ms;

    char     last_result[BT_LINK_FIELD_MAX];
    char     last_data[BT_LINK_FIELD_MAX];

    struct { bt_link_event_cb cb; void *ctx; } subs[BT_LINK_MAX_SUBSCRIBERS];
    int      n_subs;

    uint32_t events_dispatched;
    uint32_t infos_dispatched;
    uint32_t timeouts;
    uint32_t completed_ok;
    uint32_t completed_err;
} bt_link_session_t;

void bt_link_session_init(bt_link_session_t *s, uint32_t timeout_ms);

/* Register a subscriber for async lines — both EVENT (pairing prompts, state
 * changes) and INFO (scan results, paired-list items). Inspect m->status to
 * tell them apart. Returns 0 on success, -1 if the table is full. */
int  bt_link_session_subscribe(bt_link_session_t *s, bt_link_event_cb cb, void *ctx);

/* Mark `command` as just sent (verb = first token). Transitions to PENDING.
 * Returns false if a command is already in flight. */
bool bt_link_session_begin(bt_link_session_t *s, const char *command);

/* Feed a parsed message: EVENT -> subscribers; matching terminal -> completes
 * the pending command (captures result/data); INFO / non-matching terminal are
 * ignored for completion. */
void bt_link_session_on_message(bt_link_session_t *s, const bt_link_msg_t *m);

/* Advance the clock; if a pending command exceeds its timeout, transition to
 * TIMEOUT. Returns true iff a timeout just fired on this tick. */
bool bt_link_session_tick(bt_link_session_t *s, uint32_t dt_ms);

bt_link_cmd_state_t bt_link_session_state(const bt_link_session_t *s);

#ifdef __cplusplus
}
#endif
