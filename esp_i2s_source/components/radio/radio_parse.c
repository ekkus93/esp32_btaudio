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

/* --- incremental ICY demuxer --- */

void radio_icy_demux_init(radio_icy_demux_t *d, int metaint)
{
    d->metaint = metaint > 0 ? metaint : 0;
    d->audio_left = d->metaint;   /* audio first, then the first length byte */
    d->meta_left = -1;            /* -1 = the byte after audio is the length */
    d->meta_pos = 0;
    d->meta_buf[0] = '\0';
}

void radio_icy_demux_feed(radio_icy_demux_t *d, const unsigned char *in, size_t n,
                          radio_icy_audio_fn audio_fn, radio_icy_title_fn title_fn,
                          void *ctx)
{
    /* No metadata interleaving: everything is audio. */
    if (d->metaint == 0) {
        if (n && audio_fn) audio_fn(ctx, in, n);
        return;
    }

    while (n > 0) {
        if (d->audio_left > 0) {
            size_t chunk = (n < (size_t)d->audio_left) ? n : (size_t)d->audio_left;
            if (audio_fn) audio_fn(ctx, in, chunk);
            in += chunk;
            n -= chunk;
            d->audio_left -= (int)chunk;
            continue;
        }
        if (d->meta_left < 0) {
            /* This byte is the metadata length (in 16-byte units). */
            int blk = (int)(*in) * 16;
            in++;
            n--;
            if (blk == 0) {
                d->audio_left = d->metaint;   /* empty block, back to audio */
            } else {
                d->meta_left = blk;
                d->meta_pos = 0;
            }
            continue;
        }
        /* Collecting the metadata block. */
        size_t chunk = (n < (size_t)d->meta_left) ? n : (size_t)d->meta_left;
        int room = RADIO_ICY_META_MAX - 1 - d->meta_pos;
        if (room > 0) {
            int copy = (chunk < (size_t)room) ? (int)chunk : room;
            memcpy(d->meta_buf + d->meta_pos, in, (size_t)copy);
            d->meta_pos += copy;
        }
        in += chunk;
        n -= chunk;
        d->meta_left -= (int)chunk;
        if (d->meta_left == 0) {
            d->meta_buf[d->meta_pos] = '\0';
            char title[RADIO_ICY_META_MAX];
            if (title_fn && radio_icy_stream_title(d->meta_buf, title, sizeof(title))) {
                title_fn(ctx, title);
            }
            d->audio_left = d->metaint;
            d->meta_left = -1;
        }
    }
}
