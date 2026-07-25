/* Stub device functions for ctrl host tests.
 * ctrl.c calls these. Defaults match the original fixed behavior (wifi IDLE,
 * bt_link_send always TIMEOUT, no stations, radio_play_async succeeds) so
 * existing consumers (test_ctrl_init.c) are unaffected; I2S-3
 * (docs/UNIT_TESTS2_TODO.md) needs these controllable for do_action()/
 * wifi_connected() tests, so mock_* setters were added — additive only. */
#include <stddef.h>
#include <string.h>

#include "bt_link.h"
#include "wifi_mgr.h"
#include "radio.h"
#include "stations.h"
#include "esp_err.h"

/* ---- wifi_mgr ---- */
static char s_wifi_state[16] = "IDLE";

esp_err_t wifi_mgr_init(void) { return ESP_OK; }
void wifi_mgr_get_info(wifi_mgr_info_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    strlcpy(out->state, s_wifi_state, sizeof(out->state));
}
bool wifi_mgr_ap_enabled(void) { return false; }

void mock_wifi_set_state(const char *state)
{
    strlcpy(s_wifi_state, state ? state : "IDLE", sizeof(s_wifi_state));
}

/* ---- bt_link ---- */
static bt_link_cmd_state_t s_bt_link_st = BT_LINK_CMD_TIMEOUT;
static char                s_bt_link_data[256] = "";
static char                s_bt_link_last_cmd[64] = "";
static unsigned            s_bt_link_send_calls = 0;

esp_err_t bt_link_init(uint32_t timeout_ms) { (void)timeout_ms; return ESP_OK; }
esp_err_t bt_link_send(const char *cmd, bt_link_cmd_state_t *st, char *result, size_t result_sz, char *data, size_t data_sz)
{
    (void)result; (void)result_sz;
    s_bt_link_send_calls++;
    if (cmd) strlcpy(s_bt_link_last_cmd, cmd, sizeof(s_bt_link_last_cmd));
    if (st) *st = s_bt_link_st;
    if (data && data_sz) strlcpy(data, s_bt_link_data, data_sz);
    return ESP_OK;
}

void mock_bt_link_set_response(bt_link_cmd_state_t st, const char *data)
{
    s_bt_link_st = st;
    strlcpy(s_bt_link_data, data ? data : "", sizeof(s_bt_link_data));
}
const char *mock_bt_link_get_last_cmd(void) { return s_bt_link_last_cmd; }
unsigned mock_bt_link_get_send_calls(void) { return s_bt_link_send_calls; }
void mock_bt_link_reset(void)
{
    s_bt_link_st = BT_LINK_CMD_TIMEOUT;
    s_bt_link_data[0] = '\0';
    s_bt_link_last_cmd[0] = '\0';
    s_bt_link_send_calls = 0;
}

/* ---- radio ---- */
static esp_err_t s_radio_play_async_result = ESP_OK;
static char      s_radio_play_async_last_url[256] = "";

esp_err_t radio_init(size_t ring_bytes) { (void)ring_bytes; return ESP_OK; }
void radio_get_status(radio_status_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
}
esp_err_t radio_play_async(const char *url)
{
    if (url) strlcpy(s_radio_play_async_last_url, url, sizeof(s_radio_play_async_last_url));
    return s_radio_play_async_result;
}
esp_err_t radio_stop_async(void) { return ESP_OK; }
radio_state_t radio_get_state(void) { return RADIO_STATE_STOPPED; }
bool radio_audio_ready(void) { return false; }
size_t radio_pcm_read(int16_t *buf, size_t frames) { (void)buf; (void)frames; return 0; }
const char *radio_codec_str(radio_codec_t codec) { (void)codec; return ""; }

void mock_radio_set_play_async_result(esp_err_t err) { s_radio_play_async_result = err; }
const char *mock_radio_get_play_async_last_url(void) { return s_radio_play_async_last_url; }
void mock_radio_reset(void)
{
    s_radio_play_async_result = ESP_OK;
    s_radio_play_async_last_url[0] = '\0';
}

/* ---- stations ---- */
#define MOCK_STATIONS_MAX 4
typedef struct {
    uint32_t id;
    char     url[128];
} mock_station_t;
static mock_station_t s_stations[MOCK_STATIONS_MAX];
static int             s_stations_count = 0;

esp_err_t stations_init(void) { return ESP_OK; }
int stations_count(void) { return s_stations_count; }
bool stations_get(int idx, char *name, size_t nsz,
                  char *url, size_t usz, uint32_t *out_id)
{
    (void)name; (void)nsz;
    if (idx < 0 || idx >= s_stations_count) return false;
    if (url) strlcpy(url, s_stations[idx].url, usz);
    if (out_id) *out_id = s_stations[idx].id;
    return true;
}
bool stations_get_url(int idx, char *url, size_t usz)
{
    return stations_get(idx, NULL, 0, url, usz, NULL);
}

void mock_stations_set_list_entry(int idx, uint32_t id, const char *url)
{
    if (idx < 0 || idx >= MOCK_STATIONS_MAX) return;
    s_stations[idx].id = id;
    strlcpy(s_stations[idx].url, url ? url : "", sizeof(s_stations[idx].url));
    if (idx + 1 > s_stations_count) s_stations_count = idx + 1;
}
void mock_stations_set_count(int count)
{
    s_stations_count = (count < 0) ? 0 : (count > MOCK_STATIONS_MAX ? MOCK_STATIONS_MAX : count);
}

static esp_err_t s_resolve_err = ESP_ERR_NOT_FOUND;  /* result for a non-negative legacy_index */
static uint32_t  s_resolve_id = 0;

esp_err_t stations_resolve_legacy_index(int16_t legacy_index, uint32_t *out_station_id)
{
    if (!out_station_id) return ESP_ERR_INVALID_ARG;
    if (legacy_index < 0) { *out_station_id = 0; return ESP_OK; }
    if (s_resolve_err == ESP_OK) *out_station_id = s_resolve_id;
    return s_resolve_err;
}

void mock_stations_set_resolve_legacy_result(esp_err_t err, uint32_t station_id)
{
    s_resolve_err = err;
    s_resolve_id = station_id;
}
