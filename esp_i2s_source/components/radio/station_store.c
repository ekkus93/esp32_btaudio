/* station_store — pure preset CRUD. See station_store.h. */
#include "station_store.h"

#include <string.h>
#include <ctype.h>

void station_store_init(station_store_t *s)
{
    s->count = 0;
}

bool station_url_valid(const char *url)
{
    if (!url) return false;
    size_t n = strlen(url);
    if (n == 0 || n >= STATION_URL_MAX) return false;
    return strncasecmp(url, "http://", 7) == 0 || strncasecmp(url, "https://", 8) == 0;
}

/* Copy the host (between "://" and the next '/' or ':') of a valid URL into
 * `out`; falls back to the whole URL if no scheme separator is found. */
static void host_of(const char *url, char *out, size_t out_sz)
{
    const char *p = strstr(url, "://");
    p = p ? p + 3 : url;
    size_t i = 0;
    while (p[i] && p[i] != '/' && p[i] != ':' && i < out_sz - 1) {
        out[i] = p[i];
        i++;
    }
    out[i] = '\0';
    if (i == 0 && out_sz) {   /* nothing extracted -> use the url */
        strncpy(out, url, out_sz - 1);
        out[out_sz - 1] = '\0';
    }
}

static void set_entry(station_t *e, const char *name, const char *url)
{
    strncpy(e->url, url, STATION_URL_MAX - 1);
    e->url[STATION_URL_MAX - 1] = '\0';
    if (name && name[0]) {
        strncpy(e->name, name, STATION_NAME_MAX - 1);
        e->name[STATION_NAME_MAX - 1] = '\0';
    } else {
        host_of(url, e->name, STATION_NAME_MAX);
    }
}

int station_store_find(const station_store_t *s, const char *url)
{
    if (!url) return -1;
    for (int i = 0; i < s->count; i++) {
        if (strcmp(s->items[i].url, url) == 0) return i;
    }
    return -1;
}

int station_store_add(station_store_t *s, const char *name, const char *url)
{
    if (!station_url_valid(url)) return -1;
    if (s->count >= STATION_MAX) return -1;
    if (station_store_find(s, url) >= 0) return -1;   /* dedupe */
    set_entry(&s->items[s->count], name, url);
    return s->count++;
}

bool station_store_update(station_store_t *s, int idx, const char *name, const char *url)
{
    if (idx < 0 || idx >= s->count) return false;
    if (!station_url_valid(url)) return false;
    int dup = station_store_find(s, url);
    if (dup >= 0 && dup != idx) return false;   /* would collide with another */
    set_entry(&s->items[idx], name, url);
    return true;
}

bool station_store_remove(station_store_t *s, int idx)
{
    if (idx < 0 || idx >= s->count) return false;
    for (int i = idx; i < s->count - 1; i++) {
        s->items[i] = s->items[i + 1];
    }
    s->count--;
    return true;
}

bool station_store_move(station_store_t *s, int idx, int delta)
{
    if (delta != -1 && delta != 1) return false;   /* one step up/down only */
    int dst = idx + delta;
    if (idx < 0 || idx >= s->count) return false;
    if (dst < 0 || dst >= s->count) return false;   /* already at an end */
    station_t tmp = s->items[idx];
    s->items[idx] = s->items[dst];
    s->items[dst] = tmp;
    return true;
}
