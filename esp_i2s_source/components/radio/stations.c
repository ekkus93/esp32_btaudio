/*
 * stations — NVS-backed station store (RADIO-1c, FIX3 Phase 5A). See
 * stations.h. Pure blob/CRC/validation/migration logic lives in
 * stations_persist_core.c (host-tested); this file owns NVS I/O, the
 * mutex, and the load-classification/persist-before-publish state
 * machine required by FIX3 §8.
 */
#include "stations.h"
#include "station_store.h"
#include "stations_persist_core.h"

#include <string.h>
#include <stdatomic.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "stations";

#define NVS_NS         "radio"
#define NVS_KEY        "stations_v2"
#define NVS_KEY_LEGACY "stations"  /* retained for migration, never overwritten */

static station_store_t    s_store;
static SemaphoreHandle_t  s_mtx;
static _Atomic bool       s_initialized;
/* True only for the boot cycle that seeded fresh defaults (no V2, no
 * legacy) — stations_resolve_legacy_index() must never resolve a legacy
 * ctrl index against stations nobody actually configured (FIX3 §9.4/
 * responses doc answer 7: "do not pretend those defaults are the legacy
 * list"). */
static bool                s_seeded_this_boot;

typedef enum {
    LOAD_OK = 0,
    LOAD_NOT_FOUND,
    LOAD_CORRUPT,
    LOAD_NVS_ERROR,
} load_result_t;

/* Read the exact stored size of `key` first, so a short/long/absent blob
 * is classified before any field is examined (FIX3 §8.3). */
static load_result_t read_and_validate_v2(nvs_handle_t h, station_store_t *out)
{
    size_t size = 0;
    esp_err_t err = nvs_get_blob(h, NVS_KEY, NULL, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) return LOAD_NOT_FOUND;
    if (err != ESP_OK) return LOAD_NVS_ERROR;

    stations_blob_v2_t *blob = malloc(sizeof(*blob));
    if (!blob) return LOAD_NVS_ERROR;

    size_t read_size = size;
    err = nvs_get_blob(h, NVS_KEY, blob, &read_size);
    if (err != ESP_OK) {
        free(blob);
        return LOAD_NVS_ERROR;
    }

    stations_blob_result_t vr = stations_blob_validate(blob, read_size);
    if (vr != STATIONS_BLOB_OK) {
        ESP_LOGE(TAG, "V2 blob failed validation: reason=%d size=%u", (int)vr, (unsigned)read_size);
        free(blob);
        return LOAD_CORRUPT;
    }

    *out = blob->store;
    free(blob);
    return LOAD_OK;
}

static load_result_t read_and_migrate_legacy(nvs_handle_t h, station_store_t *out)
{
    size_t size = 0;
    esp_err_t err = nvs_get_blob(h, NVS_KEY_LEGACY, NULL, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) return LOAD_NOT_FOUND;
    if (err != ESP_OK) return LOAD_NVS_ERROR;

    stations_blob_v1_t *blob = malloc(sizeof(*blob));
    if (!blob) return LOAD_NVS_ERROR;

    size_t read_size = size;
    err = nvs_get_blob(h, NVS_KEY_LEGACY, blob, &read_size);
    if (err != ESP_OK) {
        free(blob);
        return LOAD_NVS_ERROR;
    }

    bool ok = stations_migrate_v1(blob, read_size, out);
    free(blob);
    if (!ok) {
        ESP_LOGE(TAG, "legacy V1 blob failed migration validation (size=%u)", (unsigned)read_size);
        return LOAD_CORRUPT;
    }
    return LOAD_OK;
}

/* One persistence path for every mutation: open, write, commit, close,
 * then read back and re-validate the exact bytes now on flash (FIX3
 * §8.5) — never trust the in-memory candidate alone. */
static esp_err_t persist_blob_verified(const station_store_t *store)
{
    stations_blob_v2_t *blob = malloc(sizeof(*blob));
    if (!blob) return ESP_ERR_NO_MEM;
    stations_build_blob(store, blob);

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        free(blob);
        return err;
    }

    err = nvs_set_blob(h, NVS_KEY, blob, sizeof(*blob));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) {
        free(blob);
        return err;
    }

    /* Read back and validate before trusting the write actually landed
     * (power loss / flash wear could otherwise go unnoticed). Reuse the
     * already-heap-allocated blob's `store` field as scratch space rather
     * than allocating another ~12 KiB station_store_t. */
    err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err != ESP_OK) {
        free(blob);
        return err;
    }
    load_result_t lr = read_and_validate_v2(h, &blob->store);
    nvs_close(h);
    free(blob);
    if (lr != LOAD_OK) {
        ESP_LOGE(TAG, "post-write read-back validation failed (result=%d)", (int)lr);
        return ESP_ERR_INVALID_CRC;
    }
    return ESP_OK;
}

/* Default seed stations (web-editable, so users can replace them). */
static const struct { const char *name; const char *url; } SEED[] = {
    { "Groove Salad",    "http://somafm.com/groovesalad.pls" },
    { "Drone Zone",      "http://somafm.com/dronezone.pls" },
    { "DEF CON Radio",   "http://somafm.com/defcon.pls" },
    { "Indie Pop Rocks", "http://somafm.com/indiepop.pls" },
    { "Beat Blender",    "http://somafm.com/beatblender.pls" },
};

static void build_seed(station_store_t *out)
{
    station_store_init(out);
    for (size_t i = 0; i < sizeof(SEED) / sizeof(SEED[0]); i++) {
        station_store_add(out, SEED[i].name, SEED[i].url, NULL);
    }
}

esp_err_t stations_init(void)
{
    if (atomic_load(&s_initialized)) return ESP_OK;

    s_mtx = xSemaphoreCreateMutex();
    if (!s_mtx) return ESP_ERR_NO_MEM;

    /* station_store_t is ~12 KiB — heap-allocate, never stack (TODO 5.7). */
    station_store_t *candidate = malloc(sizeof(*candidate));
    if (!candidate) {
        vSemaphoreDelete(s_mtx);
        s_mtx = NULL;
        return ESP_ERR_NO_MEM;
    }

    nvs_handle_t h;
    esp_err_t open_err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (open_err != ESP_OK && open_err != ESP_ERR_NVS_NOT_FOUND) {
        free(candidate);
        vSemaphoreDelete(s_mtx);
        s_mtx = NULL;
        return open_err;
    }

    load_result_t v2_result = LOAD_NOT_FOUND;
    if (open_err == ESP_OK) {
        v2_result = read_and_validate_v2(h, candidate);
    }

    if (v2_result == LOAD_OK) {
        nvs_close(h);
        s_store = *candidate;
        free(candidate);
        ESP_LOGI(TAG, "loaded v2 blob: %d stations, next_id=%u", s_store.count, s_store.next_id);
        atomic_store(&s_initialized, true);
        return ESP_OK;
    }

    if (v2_result == LOAD_CORRUPT) {
        /* Never auto-replace corrupt current data, and never fall back to
         * legacy migration as though V2 were merely absent (FIX3 §8.3). */
        nvs_close(h);
        free(candidate);
        printf("DIAG|STATIONS|CORRUPT|key=" NVS_KEY ",reason=validation\n");
        fflush(stdout);
        return ESP_ERR_INVALID_CRC;
    }

    if (v2_result == LOAD_NVS_ERROR) {
        nvs_close(h);
        free(candidate);
        return ESP_FAIL;
    }

    /* v2_result == LOAD_NOT_FOUND: only now may legacy be considered. */
    load_result_t legacy_result = (open_err == ESP_OK) ? read_and_migrate_legacy(h, candidate)
                                                       : LOAD_NOT_FOUND;
    if (open_err == ESP_OK) nvs_close(h);

    if (legacy_result == LOAD_OK) {
        esp_err_t err = persist_blob_verified(candidate);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "legacy migration persist/verify failed: %s", esp_err_to_name(err));
            printf("DIAG|STATIONS|MIGRATION_PENDING|reason=%s\n", esp_err_to_name(err));
            fflush(stdout);
            free(candidate);
            return err;   /* legacy key untouched; do not mark initialized */
        }
        s_store = *candidate;
        free(candidate);
        ESP_LOGI(TAG, "migrated V1 blob: %d stations -> V2 with IDs", s_store.count);
        atomic_store(&s_initialized, true);
        return ESP_OK;
    }

    if (legacy_result == LOAD_CORRUPT) {
        /* Preserve the corrupt legacy blob; do not seed over it. */
        free(candidate);
        printf("DIAG|STATIONS|CORRUPT|key=" NVS_KEY_LEGACY ",reason=migration\n");
        fflush(stdout);
        return ESP_ERR_INVALID_CRC;
    }

    if (legacy_result == LOAD_NVS_ERROR) {
        free(candidate);
        return ESP_FAIL;
    }

    /* Both V2 and legacy genuinely absent: seed defaults. */
    build_seed(candidate);
    esp_err_t err = persist_blob_verified(candidate);
    if (err != ESP_OK) {
        /* Do not mark initialized after a failed initial persist (FIX3
         * §8.3) — a caller retrying stations_init() gets a fresh attempt
         * rather than silently running on unpersisted defaults. */
        ESP_LOGE(TAG, "initial seed persist/verify failed: %s", esp_err_to_name(err));
        free(candidate);
        return err;
    }
    s_store = *candidate;
    free(candidate);
    s_seeded_this_boot = true;
    ESP_LOGI(TAG, "seeded %d default stations", s_store.count);
    atomic_store(&s_initialized, true);
    return ESP_OK;
}

int stations_count(void)
{
    if (!atomic_load(&s_initialized)) return 0;
    int count;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    count = s_store.count;
    xSemaphoreGive(s_mtx);
    return count;
}

bool stations_get(int idx, char *name, size_t nsz,
                  char *url, size_t usz, uint32_t *out_id)
{
    if (!atomic_load(&s_initialized)) return false;
    bool ok = false;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    if (idx >= 0 && idx < s_store.count) {
        if (name && nsz) {
            strncpy(name, s_store.items[idx].name, nsz - 1);
            name[nsz - 1] = '\0';
        }
        if (url && usz) {
            strncpy(url, s_store.items[idx].url, usz - 1);
            url[usz - 1] = '\0';
        }
        if (out_id) {
            *out_id = s_store.items[idx].id;
        }
        ok = true;
    }
    xSemaphoreGive(s_mtx);
    return ok;
}

bool stations_get_url(int idx, char *url, size_t usz)
{
    return stations_get(idx, NULL, 0, url, usz, NULL);
}

/* Resolve a legacy ctrl "last station index" to its stable ID (FIX3 §9.4,
 * responses doc answer 7). Index 0 maps to the ID assigned to the first
 * migrated/loaded station; a negative index maps to none. Deliberately
 * refuses to resolve against freshly-seeded defaults — those were never
 * anyone's "legacy list." */
esp_err_t stations_resolve_legacy_index(int16_t legacy_index, uint32_t *out_station_id)
{
    if (!out_station_id) return ESP_ERR_INVALID_ARG;
    if (legacy_index < 0) {
        *out_station_id = STATION_ID_NONE;
        return ESP_OK;
    }
    if (!atomic_load(&s_initialized)) return ESP_ERR_INVALID_STATE;

    esp_err_t result = ESP_OK;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    if (s_seeded_this_boot) {
        result = ESP_ERR_NOT_FOUND;
    } else if (legacy_index >= s_store.count) {
        result = ESP_ERR_INVALID_ARG;
    } else {
        *out_station_id = s_store.items[legacy_index].id;
    }
    xSemaphoreGive(s_mtx);
    return result;
}

esp_err_t stations_reset_persisted(void)
{
    /* Erase both keys first (best-effort — a genuinely absent legacy key
     * returning NOT_FOUND is not an error here), then let stations_init()'s
     * normal not-found-on-both path build the seed and persist it, the same
     * fresh-boot logic that runs when nothing was ever saved. */
    nvs_handle_t h;
    esp_err_t open_err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (open_err != ESP_OK) return open_err;

    esp_err_t e1 = nvs_erase_key(h, NVS_KEY);
    if (e1 == ESP_ERR_NVS_NOT_FOUND) e1 = ESP_OK;
    esp_err_t e2 = nvs_erase_key(h, NVS_KEY_LEGACY);
    if (e2 == ESP_ERR_NVS_NOT_FOUND) e2 = ESP_OK;
    esp_err_t commit_err = (e1 == ESP_OK && e2 == ESP_OK) ? nvs_commit(h) : ESP_FAIL;
    nvs_close(h);
    if (e1 != ESP_OK) return e1;
    if (e2 != ESP_OK) return e2;
    if (commit_err != ESP_OK) return commit_err;

    if (s_mtx) {
        vSemaphoreDelete(s_mtx);
        s_mtx = NULL;
    }
    atomic_store(&s_initialized, false);
    s_seeded_this_boot = false;
    printf("DIAG|STATIONS|RESET|key=" NVS_KEY "\n");
    fflush(stdout);
    return stations_init();
}

/* Helper: map station_result_t to esp_err_t */
static esp_err_t station_result_to_err(station_result_t result)
{
    switch (result) {
        case STATION_OK:            return ESP_OK;
        case STATION_ERR_FULL:      return ESP_ERR_NO_MEM;
        case STATION_ERR_DUPLICATE:
        case STATION_ERR_INVALID_URL:
        case STATION_ERR_TOO_LONG:
        case STATION_ERR_INVALID_ARG:
        case STATION_ERR_NOT_FOUND: return ESP_ERR_INVALID_ARG;
        case STATION_ERR_PERSIST:   return ESP_ERR_INVALID_STATE;
        default:                    return ESP_FAIL;
    }
}

/* station_store_t is ~12 KiB — too large for a task stack (TODO 5.7).
 * Every mutation heap-allocates its candidate. */
esp_err_t stations_add(const char *name, const char *url, int *out_idx)
{
    if (!atomic_load(&s_initialized)) return ESP_ERR_INVALID_STATE;
    if (out_idx) *out_idx = -1;

    station_store_t *candidate = malloc(sizeof(*candidate));
    if (!candidate) return ESP_ERR_NO_MEM;

    xSemaphoreTake(s_mtx, portMAX_DELAY);
    *candidate = s_store;
    int idx = -1;
    station_result_t r = station_store_add(candidate, name, url, &idx);
    if (r != STATION_OK) {
        xSemaphoreGive(s_mtx);
        free(candidate);
        return station_result_to_err(r);
    }

    esp_err_t err = persist_blob_verified(candidate);
    if (err == ESP_OK) {
        s_store = *candidate;
        if (out_idx) *out_idx = idx;
    }
    xSemaphoreGive(s_mtx);
    free(candidate);
    return err;
}

esp_err_t stations_update(int idx, const char *name, const char *url)
{
    if (!atomic_load(&s_initialized)) return ESP_ERR_INVALID_STATE;

    station_store_t *candidate = malloc(sizeof(*candidate));
    if (!candidate) return ESP_ERR_NO_MEM;

    xSemaphoreTake(s_mtx, portMAX_DELAY);
    *candidate = s_store;
    station_result_t r = station_store_update(candidate, idx, name, url);
    if (r != STATION_OK) {
        xSemaphoreGive(s_mtx);
        free(candidate);
        return station_result_to_err(r);
    }

    esp_err_t err = persist_blob_verified(candidate);
    if (err == ESP_OK) {
        s_store = *candidate;
    }
    xSemaphoreGive(s_mtx);
    free(candidate);
    return err;
}

esp_err_t stations_remove(int idx)
{
    if (!atomic_load(&s_initialized)) return ESP_ERR_INVALID_STATE;

    station_store_t *candidate = malloc(sizeof(*candidate));
    if (!candidate) return ESP_ERR_NO_MEM;

    xSemaphoreTake(s_mtx, portMAX_DELAY);
    *candidate = s_store;
    station_result_t r = station_store_remove(candidate, idx);
    if (r != STATION_OK) {
        xSemaphoreGive(s_mtx);
        free(candidate);
        return station_result_to_err(r);
    }

    esp_err_t err = persist_blob_verified(candidate);
    if (err == ESP_OK) {
        s_store = *candidate;
    }
    xSemaphoreGive(s_mtx);
    free(candidate);
    return err;
}

esp_err_t stations_move(int idx, int delta)
{
    if (!atomic_load(&s_initialized)) return ESP_ERR_INVALID_STATE;

    station_store_t *candidate = malloc(sizeof(*candidate));
    if (!candidate) return ESP_ERR_NO_MEM;

    xSemaphoreTake(s_mtx, portMAX_DELAY);
    *candidate = s_store;
    station_result_t r = station_store_move(candidate, idx, delta);
    if (r != STATION_OK) {
        xSemaphoreGive(s_mtx);
        free(candidate);
        return station_result_to_err(r);
    }

    esp_err_t err = persist_blob_verified(candidate);
    if (err == ESP_OK) {
        s_store = *candidate;
    }
    xSemaphoreGive(s_mtx);
    free(candidate);
    return err;
}
