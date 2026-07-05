/*
 * bt_link_parser — pure line/response parser for the WROOM32 command protocol
 * (LINK-1a). No ESP-IDF deps; host-tested.
 *
 * Wire format (as emitted by esp_bt_audio_source commands.c):
 *   STATUS|COMMAND|RESULT|DATA\r\n
 * exactly four '|'-separated fields, DATA may be empty and is the remainder of
 * the line (so it may itself contain '|'). STATUS ∈ {OK, ERR, INFO, EVENT}.
 *   - OK / ERR  = the single terminal response to a command
 *   - INFO      = informational line emitted mid-response (e.g. scan items)
 *   - EVENT     = asynchronous broadcast, may interleave with a response
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BT_LINK_LINE_MAX 512

typedef enum {
    BT_LINK_OK,
    BT_LINK_ERR,
    BT_LINK_INFO,
    BT_LINK_EVENT,
    BT_LINK_UNKNOWN,
} bt_link_status_t;

typedef struct {
    bt_link_status_t status;
    const char *status_str;  /* raw status token */
    const char *command;     /* field 2, "" if absent */
    const char *result;      /* field 3, "" if absent */
    const char *data;        /* field 4 remainder, "" if absent (never NULL) */
} bt_link_msg_t;

/* Parse one complete line (NUL-terminated, newline already stripped). Mutates
 * `line` in place (writes NULs at the field delimiters). Returns false only
 * for an empty line; otherwise fills `out` with pointers into `line`. */
bool bt_link_parse_line(char *line, bt_link_msg_t *out);

/* True for a terminal command response (OK or ERR). */
bool bt_link_is_terminal(bt_link_status_t s);

/* --- Line assembler: byte stream -> complete lines, partial-read safe --- */
typedef void (*bt_link_line_cb)(void *ctx, char *line);

typedef struct {
    char     buf[BT_LINK_LINE_MAX];
    size_t   len;
    bool     discarding;      /* current line overflowed; drop to next newline */
    uint32_t overflow_count;  /* number of over-length lines dropped */
} bt_link_linebuf_t;

void bt_link_linebuf_init(bt_link_linebuf_t *lb);

/* Feed `n` bytes; invoke `cb` once per complete non-empty line (CR stripped,
 * NUL-terminated). Safe across arbitrary chunk boundaries. */
void bt_link_linebuf_feed(bt_link_linebuf_t *lb, const uint8_t *data, size_t n,
                          bt_link_line_cb cb, void *ctx);

#ifdef __cplusplus
}
#endif
