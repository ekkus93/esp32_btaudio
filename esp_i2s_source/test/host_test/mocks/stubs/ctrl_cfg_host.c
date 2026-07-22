/* Stub ctrl_cfg NVS functions for host tests.
 * ctrl_cfg.c already provides ctrl_cfg_mac_valid().
 * ctrl_cfg_load() and ctrl_cfg_save() are ESP_PLATFORM-only in the real build. */
#include "ctrl_cfg.h"
#include "esp_err.h"
#include <string.h>

static esp_err_t s_save_err = ESP_OK;
static bool      s_load_needs_legacy_resolve = false;
static int16_t   s_load_legacy_index = CTRL_STATION_NONE;

void ctrl_cfg_load(ctrl_cfg_t *out, bool *out_needs_legacy_resolve, int16_t *out_legacy_index)
{
    if (!out || !out_needs_legacy_resolve || !out_legacy_index) return;
    memset(out, 0, sizeof(*out));
    out->last_station_id = CTRL_LAST_STATION_NONE;
    out->volume = CTRL_VOLUME_DEFAULT;
    *out_needs_legacy_resolve = s_load_needs_legacy_resolve;
    *out_legacy_index = s_load_legacy_index;
}

esp_err_t ctrl_cfg_save(const ctrl_cfg_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;
    /* Stub — no real persistence on host, just an injectable result. */
    return s_save_err;
}

/* Test control hooks. */
void mock_ctrl_cfg_set_save_err(esp_err_t err) { s_save_err = err; }
void mock_ctrl_cfg_set_legacy(bool needs_resolve, int16_t legacy_index)
{
    s_load_needs_legacy_resolve = needs_resolve;
    s_load_legacy_index = legacy_index;
}
void mock_ctrl_cfg_reset(void)
{
    s_save_err = ESP_OK;
    s_load_needs_legacy_resolve = false;
    s_load_legacy_index = CTRL_STATION_NONE;
}
