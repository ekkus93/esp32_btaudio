/* stations — NVS-backed station store (RADIO-1c). See stations.h. */
#include "stations.h"
#include "station_store.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "stations";

#define NVS_NS   "radio"
#define NVS_KEY  "stations"
#define BLOB_MAGIC   0x53544131u   /* "STA1" */

typedef struct {
    uint32_t        magic;
    station_store_t store;
} stations_blob_t;

static station_store_t   s_store;
static SemaphoreHandle_t s_mtx;
static stations_blob_t   s_blob;  /* static scratch for NVS writes (mutex-protected) */

/* Default presets. NOTE: the SPEC §5.4 internet-radio.com snapshot stations had
 * unreachable stream ports from our test network (verified — the laptop failed
 * too), so we seed reliable SomaFM stations instead. They're web-editable, so
 * users can add/replace with the SPEC list or their own. */
static const struct { const char *name, *url; } SEED[] = {
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
        station_store_add(&s_store, SEED[i].name, SEED[i].url);
    }
    ESP_LOGI(TAG, "seeded %d default stations", s_store.count);
}

static esp_err_t save_locked(stations_blob_t const *blob)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(h, NVS_KEY, blob, sizeof(stations_blob_t));
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

esp_err_t stations_init(void)
{
    s_mtx = xSemaphoreCreateMutex();
    if (!s_mtx) return ESP_ERR_NO_MEM;

    bool loaded = false;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        size_t sz = sizeof(s_blob);
        if (nvs_get_blob(h, NVS_KEY, &s_blob, &sz) == ESP_OK &&
            sz == sizeof(s_blob) && s_blob.magic == BLOB_MAGIC &&
            s_blob.store.count >= 0 && s_blob.store.count <= STATION_MAX) {
            s_store = s_blob.store;
            loaded = true;
        }
        nvs_close(h);
    }
    if (loaded) {
        ESP_LOGI(TAG, "loaded %d stations from NVS", s_store.count);
    } else {
        seed();
        xSemaphoreTake(s_mtx, portMAX_DELAY);
        s_blob.magic = BLOB_MAGIC;
        s_blob.store = s_store;
        save_locked(&s_blob);
        xSemaphoreGive(s_mtx);
    }
    return ESP_OK;
}

int stations_count(void)
{
    return s_store.count;
}

bool stations_get(int idx, char *name, size_t nsz, char *url, size_t usz)
{
    if (!s_mtx) return false;
    bool ok = false;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    if (idx >= 0 && idx < s_store.count) {
        if (name && nsz) { strncpy(name, s_store.items[idx].name, nsz - 1); name[nsz - 1] = '\0'; }
        if (url && usz) { strncpy(url, s_store.items[idx].url, usz - 1); url[usz - 1] = '\0'; }
        ok = true;
    }
    xSemaphoreGive(s_mtx);
    return ok;
}

bool stations_get_url(int idx, char *url, size_t usz)
{
    return stations_get(idx, NULL, 0, url, usz);
}

esp_err_t stations_add(const char *name, const char *url, int *out_idx)
{
    if (!s_mtx) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_mtx, portMAX_DELAY);

    /* Build a candidate blob with the mutation applied. */
    s_blob.magic = BLOB_MAGIC;
    s_blob.store = s_store;
    int idx = station_store_add(&s_blob.store, name, url);
    if (idx < 0) {
        xSemaphoreGive(s_mtx);
        if (out_idx) *out_idx = -1;
        return ESP_ERR_NO_MEM;
    }

    /* Persist first — only swap into RAM on success. */
    esp_err_t err = save_locked(&s_blob);
    if (err == ESP_OK) {
        s_store = s_blob.store;  /* swap on success */
    }
    xSemaphoreGive(s_mtx);

    if (err == ESP_OK && out_idx) *out_idx = idx;
    return err;
}

esp_err_t stations_update(int idx, const char *name, const char *url)
{
    if (!s_mtx) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_mtx, portMAX_DELAY);

    s_blob.magic = BLOB_MAGIC;
    s_blob.store = s_store;
    bool ok = station_store_update(&s_blob.store, idx, name, url);
    if (!ok) {
        xSemaphoreGive(s_mtx);
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = save_locked(&s_blob);
    if (err == ESP_OK) {
        s_store = s_blob.store;
    }
    xSemaphoreGive(s_mtx);

    return err;
}

esp_err_t stations_remove(int idx)
{
    if (!s_mtx) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_mtx, portMAX_DELAY);

    s_blob.magic = BLOB_MAGIC;
    s_blob.store = s_store;
    bool ok = station_store_remove(&s_blob.store, idx);
    if (!ok) {
        xSemaphoreGive(s_mtx);
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = save_locked(&s_blob);
    if (err == ESP_OK) {
        s_store = s_blob.store;
    }
    xSemaphoreGive(s_mtx);

    return err;
}

esp_err_t stations_move(int idx, int delta)
{
    if (!s_mtx) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_mtx, portMAX_DELAY);

    s_blob.magic = BLOB_MAGIC;
    s_blob.store = s_store;
    bool ok = station_store_move(&s_blob.store, idx, delta);
    if (!ok) {
        xSemaphoreGive(s_mtx);
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = save_locked(&s_blob);
    if (err == ESP_OK) {
        s_store = s_blob.store;
    }
    xSemaphoreGive(s_mtx);

    return err;
}
