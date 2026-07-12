/*
 * station_store — pure preset CRUD for internet-radio stations (RADIO-1c). No
 * ESP-IDF deps; host-tested. Add/edit/delete with URL validation, blank-name
 * defaulting (to the URL host), and exact-URL dedupe. The device layer
 * (stations.c) wraps this with NVS persistence and a mutex.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STATION_MAX       40   /* capacity (>= 32 per RADIO-1c-i) */
#define STATION_NAME_MAX  48
#define STATION_URL_MAX  256

typedef struct {
    char name[STATION_NAME_MAX];
    char url[STATION_URL_MAX];
} station_t;

typedef struct {
    int      count;
    station_t items[STATION_MAX];
} station_store_t;

void station_store_init(station_store_t *s);

/* True iff url has an http/https scheme, is non-empty, and fits. */
bool station_url_valid(const char *url);

/* Add a station. If name is NULL/empty, it defaults to the URL's host. Rejects
 * an invalid URL, a duplicate (exact-URL) entry, or a full store. Returns the
 * new index, or -1 on rejection. */
int station_store_add(station_store_t *s, const char *name, const char *url);

/* Replace the entry at idx (name may be blank -> derived from host). Returns
 * false on a bad index or invalid URL. Dedupe ignores the entry being edited. */
bool station_store_update(station_store_t *s, int idx, const char *name, const char *url);

/* Remove the entry at idx, shifting the rest down. */
bool station_store_remove(station_store_t *s, int idx);

/* Index of the entry with exactly this URL, or -1. */
int station_store_find(const station_store_t *s, const char *url);

#ifdef __cplusplus
}
#endif
