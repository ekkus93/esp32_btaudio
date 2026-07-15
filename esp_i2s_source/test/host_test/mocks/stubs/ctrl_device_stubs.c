/* Stub device functions for ctrl host tests.
 * ctrl.c calls these; they are not needed for ctrl_init() testing. */
#include <stddef.h>
#include <string.h>

#include "bt_link.h"
#include "wifi_mgr.h"
#include "radio.h"
#include "stations.h"
#include "esp_err.h"

/* wifi_mgr stubs */
esp_err_t wifi_mgr_init(void) { return ESP_OK; }
void wifi_mgr_get_info(wifi_mgr_info_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    strlcpy(out->state, "IDLE", sizeof(out->state));
}
bool wifi_mgr_ap_enabled(void) { return false; }

/* bt_link stubs */
esp_err_t bt_link_init(uint32_t timeout_ms) { (void)timeout_ms; return ESP_OK; }
void bt_link_send(const char *cmd, bt_link_cmd_state_t *st, char *result, size_t result_sz, char *data, size_t data_sz)
{
    (void)cmd; (void)result; (void)result_sz; (void)data; (void)data_sz;
    if (st) *st = BT_LINK_CMD_TIMEOUT;
}

/* radio stubs */
esp_err_t radio_init(size_t ring_bytes) { (void)ring_bytes; return ESP_OK; }
void radio_get_status(radio_status_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
}
esp_err_t radio_play_async(const char *url) { (void)url; return ESP_OK; }
esp_err_t radio_stop_async(void) { return ESP_OK; }
radio_state_t radio_get_state(void) { return RADIO_STATE_STOPPED; }
bool radio_audio_ready(void) { return false; }
size_t radio_pcm_read(int16_t *buf, size_t frames) { (void)buf; (void)frames; return 0; }
const char *radio_codec_str(radio_codec_t codec) { (void)codec; return ""; }

/* stations stubs */
esp_err_t stations_init(void) { return ESP_OK; }
int stations_count(void) { return 0; }
bool stations_get(int idx, char *name, size_t nsz,
                  char *url, size_t usz, uint32_t *out_id)
{
    (void)idx; (void)name; (void)nsz; (void)url; (void)usz; (void)out_id;
    return false;
}
bool stations_get_url(int idx, char *url, size_t usz)
{
    return stations_get(idx, NULL, 0, url, usz, NULL);
}
