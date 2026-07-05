/*
 * bt_link_parser — pure line/response parser (LINK-1a). See bt_link_parser.h.
 */
#include "bt_link_parser.h"

#include <string.h>

static bt_link_status_t classify(const char *tok)
{
    if (strcmp(tok, "OK") == 0)    return BT_LINK_OK;
    if (strcmp(tok, "ERR") == 0)   return BT_LINK_ERR;
    if (strcmp(tok, "INFO") == 0)  return BT_LINK_INFO;
    if (strcmp(tok, "EVENT") == 0) return BT_LINK_EVENT;
    return BT_LINK_UNKNOWN;
}

bool bt_link_parse_line(char *line, bt_link_msg_t *out)
{
    if (!line || line[0] == '\0') return false;

    static const char *EMPTY = "";
    out->status_str = line;
    out->command = EMPTY;
    out->result = EMPTY;
    out->data = EMPTY;

    /* field 1: status (up to first '|') */
    char *p = strchr(line, '|');
    if (p) {
        *p = '\0';
        out->command = p + 1;
        /* field 2: command */
        char *q = strchr(out->command, '|');
        if (q) {
            *q = '\0';
            out->result = q + 1;
            /* field 3: result; field 4: data = remainder (keeps any '|') */
            char *r = strchr(out->result, '|');
            if (r) {
                *r = '\0';
                out->data = r + 1;
            }
        }
    }

    out->status = classify(out->status_str);
    return true;
}

bool bt_link_is_terminal(bt_link_status_t s)
{
    return s == BT_LINK_OK || s == BT_LINK_ERR;
}

void bt_link_linebuf_init(bt_link_linebuf_t *lb)
{
    lb->len = 0;
    lb->discarding = false;
    lb->overflow_count = 0;
}

void bt_link_linebuf_feed(bt_link_linebuf_t *lb, const uint8_t *data, size_t n,
                          bt_link_line_cb cb, void *ctx)
{
    for (size_t i = 0; i < n; i++) {
        char c = (char)data[i];
        if (c == '\r') {
            continue;  /* strip CR; the LF terminates the line */
        }
        if (c == '\n') {
            if (lb->discarding) {
                lb->discarding = false;   /* recover on line boundary */
            } else if (lb->len > 0) {
                lb->buf[lb->len] = '\0';
                cb(ctx, lb->buf);
            }
            lb->len = 0;
            continue;
        }
        if (lb->discarding) {
            continue;
        }
        if (lb->len < BT_LINK_LINE_MAX - 1) {
            lb->buf[lb->len++] = c;
        } else {
            /* line too long: drop it and everything until the next newline */
            lb->discarding = true;
            lb->len = 0;
            lb->overflow_count++;
        }
    }
}
