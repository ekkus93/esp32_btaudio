/* radio_parse — pure playlist + ICY metadata parsers (RADIO-1a). See header. */
#include "radio_parse.h"

#include <string.h>
#include <ctype.h>

static bool starts_with_ci(const char *s, size_t n, const char *prefix)
{
    size_t pl = strlen(prefix);
    if (n < pl) return false;
    for (size_t i = 0; i < pl; i++) {
        if (tolower((unsigned char)s[i]) != tolower((unsigned char)prefix[i])) return false;
    }
    return true;
}

/* Case-insensitive substring search. */
static bool contains_ci(const char *hay, const char *needle)
{
    size_t nl = strlen(needle);
    if (nl == 0) return true;
    for (const char *p = hay; *p; p++) {
        if (starts_with_ci(p, strlen(p), needle)) return true;
    }
    return false;
}

/* Trim leading/trailing ASCII whitespace of [start, start+len); returns the new
 * start and updates *len. */
static const char *trim(const char *start, size_t *len)
{
    while (*len && isspace((unsigned char)*start)) { start++; (*len)--; }
    while (*len && isspace((unsigned char)start[*len - 1])) { (*len)--; }
    return start;
}

static bool is_http_url(const char *s, size_t n)
{
    return starts_with_ci(s, n, "http://") || starts_with_ci(s, n, "https://");
}

static bool copy_out(const char *s, size_t n, char *out, size_t out_sz)
{
    if (n == 0 || n >= out_sz) return false;
    memcpy(out, s, n);
    out[n] = '\0';
    return true;
}

/* Iterate lines: on each call, sets line and len to the next line (excluding
 * the newline) and returns the pointer just past it, or NULL at end. */
static const char *next_line(const char *p, const char **line, size_t *len)
{
    if (!p || *p == '\0') return NULL;
    const char *start = p;
    while (*p && *p != '\n') p++;
    size_t n = (size_t)(p - start);
    if (n && start[n - 1] == '\r') n--;   /* strip CR */
    *line = start;
    *len = n;
    return (*p == '\n') ? p + 1 : p;      /* skip the newline if present */
}

bool radio_playlist_first_url(const char *text, char *out, size_t out_sz)
{
    if (!text || !out || out_sz == 0) return false;

    bool is_pls = contains_ci(text, "[playlist]");
    const char *line;
    size_t len;
    const char *p = text;

    while ((p = next_line(p, &line, &len)) != NULL) {
        const char *t = trim(line, &len);
        if (len == 0) continue;

        if (is_pls) {
            /* PLS: "FileN=URL" (case-insensitive key). Take the first one. */
            if (starts_with_ci(t, len, "file")) {
                const char *eq = memchr(t, '=', len);
                if (eq) {
                    size_t vlen = len - (size_t)(eq + 1 - t);
                    const char *v = trim(eq + 1, &vlen);
                    if (is_http_url(v, vlen)) return copy_out(v, vlen, out, out_sz);
                }
            }
        } else if (t[0] != '#' && is_http_url(t, len)) {
            /* M3U / bare URL: first non-comment http(s) line. */
            return copy_out(t, len, out, out_sz);
        }
    }
    return false;
}

bool radio_icy_stream_title(const char *block, char *title, size_t title_sz)
{
    if (!block || !title || title_sz == 0) return false;
    title[0] = '\0';

    const char *key = "StreamTitle='";
    const char *start = NULL;
    for (const char *p = block; *p; p++) {
        if (starts_with_ci(p, strlen(p), key)) { start = p + strlen(key); break; }
    }
    if (!start) return false;

    /* Value runs until the terminating "';" (or a lone closing quote / end). */
    const char *end = start;
    while (*end) {
        if (end[0] == '\'' && (end[1] == ';' || end[1] == '\0')) break;
        end++;
    }
    size_t vlen = (size_t)(end - start);
    if (vlen >= title_sz) vlen = title_sz - 1;
    memcpy(title, start, vlen);
    title[vlen] = '\0';
    return true;
}
