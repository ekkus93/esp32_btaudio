/* Stub ctrl_cfg NVS functions for host tests.
 * ctrl_cfg.c already provides ctrl_cfg_mac_valid().
 * ctrl_cfg_load() and ctrl_cfg_save() are ESP_PLATFORM-only in the real build. */
#include "ctrl_cfg.h"
#include "esp_err.h"
#include <string.h>

void ctrl_cfg_load(ctrl_cfg_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->last_station = CTRL_STATION_NONE;
    out->volume = CTRL_VOLUME_DEFAULT;
}

esp_err_t ctrl_cfg_save(const ctrl_cfg_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;
    /* Stub — no persistence on host */
    return ESP_OK;
}
