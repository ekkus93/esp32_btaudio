/*
 * bt_link_session — pure command/response session (LINK-1b). See header.
 */
#include "bt_link_session.h"

#include <string.h>

static void copy_field(char *dst, size_t dst_sz, const char *src)
{
    if (!src) { dst[0] = '\0'; return; }
    size_t n = strlen(src);
    if (n >= dst_sz) n = dst_sz - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/* first whitespace-delimited token of `command` -> verb */
static void extract_verb(char *dst, size_t dst_sz, const char *command)
{
    size_t i = 0;
    while (command && command[i] && command[i] != ' ' && i < dst_sz - 1) {
        dst[i] = command[i];
        i++;
    }
    dst[i] = '\0';
}

void bt_link_session_init(bt_link_session_t *s, uint32_t timeout_ms)
{
    memset(s, 0, sizeof(*s));
    s->state = BT_LINK_CMD_IDLE;
    s->timeout_ms = timeout_ms;
}

int bt_link_session_subscribe(bt_link_session_t *s, bt_link_event_cb cb, void *ctx)
{
    if (s->n_subs >= BT_LINK_MAX_SUBSCRIBERS) return -1;
    s->subs[s->n_subs].cb = cb;
    s->subs[s->n_subs].ctx = ctx;
    s->n_subs++;
    return 0;
}

bool bt_link_session_begin(bt_link_session_t *s, const char *command)
{
    if (s->state == BT_LINK_CMD_PENDING) return false;
    extract_verb(s->verb, sizeof(s->verb), command);
    s->elapsed_ms = 0;
    s->last_result[0] = '\0';
    s->last_data[0] = '\0';
    s->state = BT_LINK_CMD_PENDING;
    return true;
}

void bt_link_session_on_message(bt_link_session_t *s, const bt_link_msg_t *m)
{
    /* EVENT and INFO are async/informational broadcasts — fan them out to
     * subscribers (pairing prompts, scan results, paired-list items) and never
     * let them complete the in-flight command. INFO may arrive during a pending
     * command (PAIRED items) or with none pending (SCAN results after STARTED);
     * both are surfaced to subscribers. */
    if (m->status == BT_LINK_EVENT || m->status == BT_LINK_INFO) {
        for (int i = 0; i < s->n_subs; i++) {
            if (s->subs[i].cb) s->subs[i].cb(s->subs[i].ctx, m);
        }
        if (m->status == BT_LINK_EVENT) s->events_dispatched++;
        else s->infos_dispatched++;
        return;
    }

    if (s->state != BT_LINK_CMD_PENDING) return;

    if (bt_link_is_terminal(m->status)) {
        /* correlate: response command field must echo the verb we sent */
        if (s->verb[0] != '\0' && strcmp(m->command, s->verb) != 0) {
            return;  /* stray terminal for a different command — ignore */
        }
        copy_field(s->last_result, sizeof(s->last_result), m->result);
        copy_field(s->last_data, sizeof(s->last_data), m->data);
        if (m->status == BT_LINK_OK) {
            s->state = BT_LINK_CMD_DONE_OK;
            s->completed_ok++;
        } else {
            s->state = BT_LINK_CMD_DONE_ERR;
            s->completed_err++;
        }
    }
    /* INFO lines are informational; they don't complete the command. */
}

bool bt_link_session_tick(bt_link_session_t *s, uint32_t dt_ms)
{
    if (s->state != BT_LINK_CMD_PENDING) return false;
    s->elapsed_ms += dt_ms;
    if (s->elapsed_ms >= s->timeout_ms) {
        s->state = BT_LINK_CMD_TIMEOUT;
        s->timeouts++;
        return true;
    }
    return false;
}

bt_link_cmd_state_t bt_link_session_state(const bt_link_session_t *s)
{
    return s->state;
}
