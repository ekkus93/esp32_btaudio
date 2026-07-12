/* ctrl_cfg — NVS-backed orchestration config (CTRL-1a). See ctrl_cfg.h. */
#include "ctrl_cfg.h"

#include <string.h>

static bool is_hex(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

bool ctrl_cfg_mac_valid(const char *mac)
{
    if (!mac) return false;
    /* "XX:XX:XX:XX:XX:XX" — 6 hex pairs, colon between, exactly 17 chars. */
    for (int i = 0; i < 17; i++) {
        if ((i % 3) == 2) {
            if (mac[i] != ':') return false;
        } else if (!is_hex(mac[i])) {
            return false;
        }
    }
    return mac[17] == '\0';
}

#ifdef ESP_PLATFORM
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "ctrl_cfg";

#define NVS_NS      "ctrl"
#define NVS_KEY     "cfg"
#define BLOB_MAGIC  0x43315631u   /* "C1V1" */

typedef struct {
    uint32_t  magic;
    ctrl_cfg_t cfg;
} ctrl_cfg_blob_t;

void ctrl_cfg_load(ctrl_cfg_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->last_station = CTRL_STATION_NONE;
    out->volume = CTRL_VOLUME_DEFAULT;

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    ctrl_cfg_blob_t blob;
    size_t sz = sizeof(blob);
    if (nvs_get_blob(h, NVS_KEY, &blob, &sz) == ESP_OK &&
        sz == sizeof(blob) && blob.magic == BLOB_MAGIC) {
        /* Guard against a stray unterminated MAC / out-of-range volume. */
        blob.cfg.sink_mac[CTRL_MAC_LEN - 1] = '\0';
        if (blob.cfg.volume > 100) blob.cfg.volume = 100;
        *out = blob.cfg;
        ESP_LOGI(TAG, "loaded: mac=%s autostart=%u last_station=%d volume=%u",
                 out->sink_mac[0] ? out->sink_mac : "(none)",
                 out->autostart, out->last_station, out->volume);
    }
    nvs_close(h);
}

esp_err_t ctrl_cfg_save(const ctrl_cfg_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t e = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (e != ESP_OK) return e;
    ctrl_cfg_blob_t blob = { .magic = BLOB_MAGIC, .cfg = *cfg };
    blob.cfg.sink_mac[CTRL_MAC_LEN - 1] = '\0';
    e = nvs_set_blob(h, NVS_KEY, &blob, sizeof(blob));
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return e;
}
#endif /* ESP_PLATFORM */
