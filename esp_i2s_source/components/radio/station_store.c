/* station_store — pure preset CRUD. See station_store.h. */
#include "station_store.h"

#include <string.h>
#include <ctype.h>

void station_store_init(station_store_t *s)
{
    s->next_id = 1;
    s->count = 0;
}

/* Check whether a URL is valid for station use. Returns STATION_OK if valid. */
station_result_t station_validate_url(const char *url)
{
    if (!url) return STATION_ERR_INVALID_ARG;
    size_t n = strlen(url);
    if (n == 0) return STATION_ERR_INVALID_URL;
    if (n >= STATION_URL_MAX) return STATION_ERR_TOO_LONG;

    /* Reject control characters (except space through tilde are fine). */
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)url[i];
        if (c <= 0x20 || c == 0x7f) return STATION_ERR_INVALID_URL;
    }

    /* Require http:// or https:// scheme. */
    if (strncasecmp(url, "http://", 7) == 0 || strncasecmp(url, "https://", 8) == 0) {
        return STATION_OK;
    }
    return STATION_ERR_INVALID_URL;
}

bool station_url_valid(const char *url)
{
    return station_validate_url(url) == STATION_OK;
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
    if (i == 0 && out_sz) {
        strncpy(out, url, out_sz - 1);
        out[out_sz - 1] = '\0';
    }
}

static void set_entry(station_t *e, const char *name, const char *url)
{
    size_t url_len = strlen(url);
    memcpy(e->url, url, url_len + 1);

    if (name && name[0]) {
        size_t name_len = strlen(name);
        if (name_len >= STATION_NAME_MAX) {
            name_len = STATION_NAME_MAX - 1;
        }
        memcpy(e->name, name, name_len);
        e->name[name_len] = '\0';
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

int station_store_index_by_id(const station_store_t *s, uint32_t id)
{
    if (id == 0) return -1;
    for (int i = 0; i < s->count; i++) {
        if (s->items[i].id == id) return i;
    }
    return -1;
}

station_result_t station_store_add(station_store_t *s,
                                    const char *name, const char *url,
                                    int *out_idx)
{
    if (out_idx) *out_idx = -1;

    station_result_t result = station_validate_url(url);
    if (result != STATION_OK) return result;

    if (s->count >= STATION_MAX) return STATION_ERR_FULL;
    if (station_store_find(s, url) >= 0) return STATION_ERR_DUPLICATE;

    /* Check name length */
    if (name && strlen(name) >= STATION_NAME_MAX) {
        return STATION_ERR_TOO_LONG;
    }

    int idx = s->count;
    set_entry(&s->items[idx], name, url);
    s->items[idx].id = s->next_id++;
    s->count++;
    if (out_idx) *out_idx = idx;
    return STATION_OK;
}

station_result_t station_store_update(station_store_t *s,
                                        int idx, const char *name, const char *url)
{
    if (idx < 0 || idx >= s->count) return STATION_ERR_NOT_FOUND;

    station_result_t result = station_validate_url(url);
    if (result != STATION_OK) return result;

    /* Dedupe — ignore the entry being edited */
    int dup = station_store_find(s, url);
    if (dup >= 0 && dup != idx) return STATION_ERR_DUPLICATE;

    set_entry(&s->items[idx], name, url);
    /* ID is unchanged — the station keeps its stable identity. */
    return STATION_OK;
}

station_result_t station_store_remove(station_store_t *s, int idx)
{
    if (idx < 0 || idx >= s->count) return STATION_ERR_NOT_FOUND;

    for (int i = idx; i < s->count - 1; i++) {
        s->items[i] = s->items[i + 1];
    }
    s->count--;
    return STATION_OK;
}

station_result_t station_store_move(station_store_t *s, int idx, int delta)
{
    if (delta != -1 && delta != 1) return STATION_ERR_INVALID_ARG;
    if (idx < 0 || idx >= s->count) return STATION_ERR_NOT_FOUND;

    int dst = idx + delta;
    if (dst < 0 || dst >= s->count) return STATION_ERR_NOT_FOUND;

    station_t tmp = s->items[idx];
    s->items[idx] = s->items[dst];
    s->items[dst] = tmp;
    return STATION_OK;
}
