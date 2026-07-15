/* stations — NVS-backed station store (RADIO-1c). See stations.h. */
#include "stations.h"
#include "station_store.h"

#include <string.h>
#include <stdatomic.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "stations";

/* V2 blob: STN2 magic + version + CRC-32 */
#define STATIONS_V2_MAGIC       0x53544e32u  /* "STN2" */
#define STATIONS_V2_VERSION     2u

/* V1 blob: legacy STA1 format (no version, no CRC, no IDs) */
#define STATIONS_V1_MAGIC       0x53544131u  /* "STA1" */

#define NVS_NS   "radio"
#define NVS_KEY  "stations_v2"
#define NVS_KEY_LEGACY "stations"  /* retained for migration, not overwritten */

/* V1 blob (legacy format) */
typedef struct __attribute__((packed)) {
    uint32_t  magic;
    int       count;
    char      items[STATION_MAX][STATION_NAME_MAX + STATION_URL_MAX];
} stations_blob_v1_t;

/* V2 blob (current format) */
typedef struct {
    uint32_t  magic;
    uint16_t  version;
    uint16_t  header_size;    /* sizeof(header) */
    uint32_t  payload_size;   /* sizeof(station_store_t) */
    uint32_t  next_id;
    uint32_t  crc32;          /* CRC-32 over the store payload */
    station_store_t store;
} stations_blob_v2_t;

static station_store_t    s_store;
static SemaphoreHandle_t  s_mtx;
static _Atomic bool       s_initialized;

/* CRC-32 (IEEE 802.3) */
static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 8; bit != 0; bit--) {
            crc >>= 1;
            if (crc & 0x80000000u) {
                crc ^= 0xEDB88320u;
            }
        }
    }
    return crc;
}

static uint32_t compute_crc(const station_store_t *store)
{
    return crc32_update(0, (const uint8_t *)store, sizeof(*store));
}

/* Build the V2 blob from current store state. */
static stations_blob_v2_t build_blob(void)
{
    stations_blob_v2_t blob;
    memset(&blob, 0, sizeof(blob));
    blob.magic = STATIONS_V2_MAGIC;
    blob.version = STATIONS_V2_VERSION;
    blob.header_size = (uint16_t)offsetof(stations_blob_v2_t, store);
    blob.payload_size = sizeof(station_store_t);
    blob.next_id = s_store.next_id;
    memcpy(&blob.store, &s_store, sizeof(station_store_t));
    blob.crc32 = compute_crc(&blob.store);
    return blob;
}

static esp_err_t persist_blob(const stations_blob_v2_t *blob)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    err = nvs_set_blob(h, NVS_KEY, blob, sizeof(*blob));
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

/* Default seed stations (web-editable, so users can replace them). */
static const struct { const char *name; const char *url; } SEED[] = {
    { "Groove Salad",    "http://somafm.com/groovesalad.pls" },
    { "Drone Zone",      "http://somafm.com/dronezone.pls" },
    { "DEF CON Radio",   "http://somafm.com/defcon.pls" },
    { "Indie Pop Rocks", "http://somafm.com/indiepop.pls" },
    { "Beat Blender",    "http://somafm.com/beatblender.pls" },
};

static void seed(void)
{
    station_store_init(&s_store);
    for (size_t i = 0; i < sizeof(SEED) / sizeof(SEED[0]); i++) {
        station_store_add(&s_store, SEED[i].name, SEED[i].url, NULL);
    }
    ESP_LOGI(TAG, "seeded %d default stations", s_store.count);
}

static bool load_v2(nvs_handle_t h)
{
    stations_blob_v2_t blob;
    size_t sz = sizeof(blob);
    if (nvs_get_blob(h, NVS_KEY, &blob, &sz) != ESP_OK) return false;
    if (sz != sizeof(blob)) return false;
    if (blob.magic != STATIONS_V2_MAGIC) return false;
    if (blob.version != STATIONS_V2_VERSION) return false;
    if (blob.payload_size != sizeof(station_store_t)) return false;

    /* Validate CRC */
    uint32_t expected = compute_crc(&blob.store);
    if (expected != blob.crc32) return false;

    /* Validate count */
    if (blob.store.count < 0 || blob.store.count > STATION_MAX) return false;

    s_store = blob.store;
    ESP_LOGI(TAG, "loaded v2 blob: %d stations, next_id=%u",
             s_store.count, s_store.next_id);
    return true;
}

static bool migrate_v1(nvs_handle_t h)
{
    /* V1 blob: STA1 magic followed by station_store_t (old format without next_id).
     * We detect it by size and magic. */
    typedef struct {
        uint32_t magic;
        station_store_t store;
    } v1_blob_t;

    v1_blob_t blob;
    size_t sz = sizeof(blob);
    if (nvs_get_blob(h, NVS_KEY_LEGACY, &blob, &sz) != ESP_OK) return false;
    if (sz != sizeof(blob)) return false;
    if (blob.magic != STATIONS_V1_MAGIC) return false;

    /* Validate count */
    if (blob.store.count < 0 || blob.store.count > STATION_MAX) return false;

    /* Migrate: assign sequential IDs to old entries */
    station_store_init(&s_store);
    for (int i = 0; i < blob.store.count; i++) {
        int new_idx = -1;
        station_result_t result = station_store_add(&s_store,
                                                     blob.store.items[i].name,
                                                     blob.store.items[i].url,
                                                     &new_idx);
        if (result != STATION_OK) {
            ESP_LOGW(TAG, "migration failed at station %d: err=%d", i, result);
            return false;
        }
        if (new_idx != i) {
            ESP_LOGW(TAG, "migration index mismatch at %d", i);
            return false;
        }
    }

    ESP_LOGI(TAG, "migrated V1 blob: %d stations -> V2 with IDs", s_store.count);
    return true;
}

esp_err_t stations_init(void)
{
    if (atomic_load(&s_initialized)) return ESP_OK;

    s_mtx = xSemaphoreCreateMutex();
    if (!s_mtx) {
        atomic_store(&s_initialized, false);
        return ESP_ERR_NO_MEM;
    }

    bool loaded = false;

    /* Try V2 blob first */
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        loaded = load_v2(h);
        if (!loaded) {
            /* Try V1 migration */
            loaded = migrate_v1(h);
        }
        nvs_close(h);
    }

    if (!loaded) {
        seed();
        xSemaphoreTake(s_mtx, portMAX_DELAY);
        stations_blob_v2_t blob = build_blob();
        esp_err_t err = persist_blob(&blob);
        xSemaphoreGive(s_mtx);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "initial save failed: %s", esp_err_to_name(err));
        }
    }

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

esp_err_t stations_add(const char *name, const char *url, int *out_idx)
{
    if (!atomic_load(&s_initialized)) return ESP_ERR_INVALID_STATE;

    station_result_t result = station_validate_url(url);
    if (result != STATION_OK) {
        if (out_idx) *out_idx = -1;
        return station_result_to_err(result);
    }

    xSemaphoreTake(s_mtx, portMAX_DELAY);

    /* Build a candidate blob with the mutation applied. */
    stations_blob_v2_t blob;
    memcpy(&blob.store, &s_store, sizeof(station_store_t));
    blob.magic = STATIONS_V2_MAGIC;
    blob.version = STATIONS_V2_VERSION;
    blob.header_size = (uint16_t)offsetof(stations_blob_v2_t, store);
    blob.payload_size = sizeof(station_store_t);
    blob.next_id = s_store.next_id;

    int idx = -1;
    station_result_t r = station_store_add(&blob.store, name, url, &idx);
    if (r != STATION_OK) {
        xSemaphoreGive(s_mtx);
        if (out_idx) *out_idx = -1;
        return station_result_to_err(r);
    }

    /* Persist to NVS */
    blob.crc32 = compute_crc(&blob.store);
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, NVS_KEY, &blob, sizeof(blob));
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
    }
    nvs_close(h);

    if (err == ESP_OK) {
        s_store = blob.store;  /* swap on success */
    }
    xSemaphoreGive(s_mtx);

    if (err == ESP_OK && out_idx) *out_idx = idx;
    return err;
}

esp_err_t stations_update(int idx, const char *name, const char *url)
{
    if (!atomic_load(&s_initialized)) return ESP_ERR_INVALID_STATE;

    station_result_t result = station_validate_url(url);
    if (result != STATION_OK) {
        return station_result_to_err(result);
    }

    xSemaphoreTake(s_mtx, portMAX_DELAY);

    stations_blob_v2_t blob;
    memcpy(&blob.store, &s_store, sizeof(station_store_t));
    blob.magic = STATIONS_V2_MAGIC;
    blob.version = STATIONS_V2_VERSION;
    blob.header_size = (uint16_t)offsetof(stations_blob_v2_t, store);
    blob.payload_size = sizeof(station_store_t);
    blob.next_id = s_store.next_id;

    station_result_t r = station_store_update(&blob.store, idx, name, url);
    if (r != STATION_OK) {
        xSemaphoreGive(s_mtx);
        return station_result_to_err(r);
    }

    /* Persist to NVS */
    blob.crc32 = compute_crc(&blob.store);
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, NVS_KEY, &blob, sizeof(blob));
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
    }
    nvs_close(h);

    if (err == ESP_OK) {
        s_store = blob.store;
    }
    xSemaphoreGive(s_mtx);

    return err;
}

esp_err_t stations_remove(int idx)
{
    if (!atomic_load(&s_initialized)) return ESP_ERR_INVALID_STATE;

    xSemaphoreTake(s_mtx, portMAX_DELAY);

    stations_blob_v2_t blob;
    memcpy(&blob.store, &s_store, sizeof(station_store_t));
    blob.magic = STATIONS_V2_MAGIC;
    blob.version = STATIONS_V2_VERSION;
    blob.header_size = (uint16_t)offsetof(stations_blob_v2_t, store);
    blob.payload_size = sizeof(station_store_t);
    blob.next_id = s_store.next_id;

    station_result_t r = station_store_remove(&blob.store, idx);
    if (r != STATION_OK) {
        xSemaphoreGive(s_mtx);
        return station_result_to_err(r);
    }

    blob.crc32 = compute_crc(&blob.store);
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, NVS_KEY, &blob, sizeof(blob));
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
    }
    nvs_close(h);

    if (err == ESP_OK) {
        s_store = blob.store;
    }
    xSemaphoreGive(s_mtx);

    return err;
}

esp_err_t stations_move(int idx, int delta)
{
    if (!atomic_load(&s_initialized)) return ESP_ERR_INVALID_STATE;

    xSemaphoreTake(s_mtx, portMAX_DELAY);

    stations_blob_v2_t blob;
    memcpy(&blob.store, &s_store, sizeof(station_store_t));
    blob.magic = STATIONS_V2_MAGIC;
    blob.version = STATIONS_V2_VERSION;
    blob.header_size = (uint16_t)offsetof(stations_blob_v2_t, store);
    blob.payload_size = sizeof(station_store_t);
    blob.next_id = s_store.next_id;

    station_result_t r = station_store_move(&blob.store, idx, delta);
    if (r != STATION_OK) {
        xSemaphoreGive(s_mtx);
        return station_result_to_err(r);
    }

    blob.crc32 = compute_crc(&blob.store);
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, NVS_KEY, &blob, sizeof(blob));
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
    }
    nvs_close(h);

    if (err == ESP_OK) {
        s_store = blob.store;
    }
    xSemaphoreGive(s_mtx);

    return err;
}
