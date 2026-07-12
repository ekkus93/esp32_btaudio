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

static void save_locked(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    /* ~12 KB — must NOT go on the (small) task stack; save_locked is always
     * called under s_mtx, so a static scratch is safe. */
    static stations_blob_t blob;
    blob.magic = BLOB_MAGIC;
    blob.store = s_store;
    if (nvs_set_blob(h, NVS_KEY, &blob, sizeof(blob)) == ESP_OK) {
        nvs_commit(h);
    }
    nvs_close(h);
}

esp_err_t stations_init(void)
{
    s_mtx = xSemaphoreCreateMutex();
    if (!s_mtx) return ESP_ERR_NO_MEM;

    bool loaded = false;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        static stations_blob_t blob;
        size_t sz = sizeof(blob);
        if (nvs_get_blob(h, NVS_KEY, &blob, &sz) == ESP_OK &&
            sz == sizeof(blob) && blob.magic == BLOB_MAGIC &&
            blob.store.count >= 0 && blob.store.count <= STATION_MAX) {
            s_store = blob.store;
            loaded = true;
        }
        nvs_close(h);
    }
    if (loaded) {
        ESP_LOGI(TAG, "loaded %d stations from NVS", s_store.count);
    } else {
        seed();
        xSemaphoreTake(s_mtx, portMAX_DELAY);
        save_locked();
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

int stations_add(const char *name, const char *url)
{
    if (!s_mtx) return -1;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    int i = station_store_add(&s_store, name, url);
    if (i >= 0) save_locked();
    xSemaphoreGive(s_mtx);
    return i;
}

bool stations_update(int idx, const char *name, const char *url)
{
    if (!s_mtx) return false;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    bool ok = station_store_update(&s_store, idx, name, url);
    if (ok) save_locked();
    xSemaphoreGive(s_mtx);
    return ok;
}

bool stations_remove(int idx)
{
    if (!s_mtx) return false;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    bool ok = station_store_remove(&s_store, idx);
    if (ok) save_locked();
    xSemaphoreGive(s_mtx);
    return ok;
}
