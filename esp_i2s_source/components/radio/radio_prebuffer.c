/*
 * radio_prebuffer — prebuffer-threshold NVS persistence, split out of radio.c
 * to keep that file focused on session lifecycle/control. The threshold gates
 * playback (see g_radio_prebuffered / radio_audio_ready()); it is stored in
 * g_radio_prebuffer_bytes and persisted here. Device glue, not host-tested on
 * its own — exercised via radio_init()/radio_set_prebuffer_ms() in
 * test_radio_lifecycle and verified on hardware.
 */
#include "radio_internal.h"

#include <stdatomic.h>
#include <stdio.h>
#include <inttypes.h>

#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "radio";

#define NVS_NS_RADIO   "radio"
#define NVS_KEY_PREBUF "prebuf_ms"

int radio_get_prebuffer_ms(void)
{
    return (int)(atomic_load(&g_radio_prebuffer_bytes) / PCM_BYTES_PER_MS);
}

esp_err_t radio_set_prebuffer_ms(int ms)
{
    ms = (ms < PREBUF_MS_MIN) ? PREBUF_MS_MIN : ms;
    ms = (ms > PREBUF_MS_MAX) ? PREBUF_MS_MAX : ms;
    atomic_store(&g_radio_prebuffer_bytes, (size_t)ms * PCM_BYTES_PER_MS);

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS_RADIO, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_i32(h, NVS_KEY_PREBUF, ms);
        if (err == ESP_OK) err = nvs_commit(h);
    }
    nvs_close(h);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "prebuffer applied but persistence failed: %s",
                 esp_err_to_name(err));
    }
    ESP_LOGI(TAG, "prebuffer set to %d ms (%" PRIu32 " bytes)", ms, (uint32_t)atomic_load(&g_radio_prebuffer_bytes));
    return err;
}

/* 7.11: explicit compile-time default is stored FIRST, before any NVS
 * read, so a genuine load failure never leaves the threshold at whatever
 * it happened to already be. NOT_FOUND (missing namespace or key) is the
 * ordinary fresh-device case and returns ESP_OK with the default in
 * effect; any other error is a real load failure and is returned so the
 * caller can log/report it rather than silently treating it as success. */
esp_err_t radio_prebuffer_load(void)
{
    atomic_store(&g_radio_prebuffer_bytes, (size_t)PREBUF_MS_DEFAULT * PCM_BYTES_PER_MS);

    nvs_handle_t h = 0;
    esp_err_t err = nvs_open(NVS_NS_RADIO, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;

    int32_t ms = PREBUF_MS_DEFAULT;
    err = nvs_get_i32(h, NVS_KEY_PREBUF, &ms);
    nvs_close(h);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    if (ms < PREBUF_MS_MIN || ms > PREBUF_MS_MAX) return ESP_ERR_INVALID_SIZE;

    atomic_store(&g_radio_prebuffer_bytes, (size_t)ms * PCM_BYTES_PER_MS);
    return ESP_OK;
}
