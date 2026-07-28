#include "bt_hfp_manager_command_stub.h"

#include <string.h>

static bt_hfp_manager_status_t s_status;
static bt_hfp_manager_stats_t s_stats;
static esp_err_t s_status_result;
static esp_err_t s_stats_result;
static esp_err_t s_connect_result;
static esp_err_t s_disconnect_result;
static esp_err_t s_audio_start_result;
static esp_err_t s_audio_stop_result;
static esp_err_t s_mode_result;
static esp_err_t s_reset_stats_result;
static unsigned s_status_calls;
static unsigned s_stats_calls;
static unsigned s_connect_calls;
static unsigned s_disconnect_calls;
static unsigned s_audio_start_calls;
static unsigned s_audio_stop_calls;
static unsigned s_mode_calls;
static unsigned s_reset_stats_calls;
static char s_last_connect_mac[BT_DUPLEX_MAC_STR_LEN];
static bt_duplex_mode_t s_last_mode;

void mock_bt_hfp_manager_reset(void)
{
    memset(&s_status, 0, sizeof(s_status));
    memset(&s_stats, 0, sizeof(s_stats));
    s_status.manager_initialized = true;
    s_status.configured_mode = BT_DUPLEX_MODE_AUTO;
    s_status.duplex.requested_mode = BT_DUPLEX_MODE_AUTO;
    s_status.duplex.effective_mode = BT_DUPLEX_MODE_A2DP_PLUS_HFP_MIC;
    s_status.duplex.a2dp_profile_state = BT_A2DP_PROFILE_DISCONNECTED;
    s_status.duplex.a2dp_audio_state = BT_A2DP_AUDIO_STOPPED;
    s_status.duplex.hfp_profile_state = BT_HFP_PROFILE_DISCONNECTED;
    s_status.duplex.hfp_audio_state = BT_HFP_AUDIO_DISCONNECTED;
    s_status.duplex.codec = BT_HFP_CODEC_NONE;
    s_status.duplex.i2s_state = BT_HFP_I2S_STOPPED;
    s_status.duplex.health = BT_AUDIO_HEALTH_OK;
    s_status.duplex.last_error = ESP_OK;
    s_status_result = ESP_OK;
    s_stats_result = ESP_OK;
    s_connect_result = ESP_OK;
    s_disconnect_result = ESP_OK;
    s_audio_start_result = ESP_OK;
    s_audio_stop_result = ESP_OK;
    s_mode_result = ESP_OK;
    s_reset_stats_result = ESP_OK;
    s_status_calls = 0U;
    s_stats_calls = 0U;
    s_connect_calls = 0U;
    s_disconnect_calls = 0U;
    s_audio_start_calls = 0U;
    s_audio_stop_calls = 0U;
    s_mode_calls = 0U;
    s_reset_stats_calls = 0U;
    s_last_connect_mac[0] = '\0';
    s_last_mode = BT_DUPLEX_MODE_AUTO;
}

void mock_bt_hfp_manager_set_status(const bt_hfp_manager_status_t *status,
                                    esp_err_t result)
{
    if (status != NULL) s_status = *status;
    s_status_result = result;
}

void mock_bt_hfp_manager_set_stats(const bt_hfp_manager_stats_t *stats,
                                   esp_err_t result)
{
    if (stats != NULL) s_stats = *stats;
    s_stats_result = result;
}

void mock_bt_hfp_manager_set_connect_result(esp_err_t result) { s_connect_result = result; }
void mock_bt_hfp_manager_set_disconnect_result(esp_err_t result) { s_disconnect_result = result; }
void mock_bt_hfp_manager_set_audio_start_result(esp_err_t result) { s_audio_start_result = result; }
void mock_bt_hfp_manager_set_audio_stop_result(esp_err_t result) { s_audio_stop_result = result; }
void mock_bt_hfp_manager_set_mode_result(esp_err_t result) { s_mode_result = result; }
void mock_bt_hfp_manager_set_reset_stats_result(esp_err_t result) { s_reset_stats_result = result; }
unsigned mock_bt_hfp_manager_status_calls(void) { return s_status_calls; }
unsigned mock_bt_hfp_manager_stats_calls(void) { return s_stats_calls; }
unsigned mock_bt_hfp_manager_connect_calls(void) { return s_connect_calls; }
unsigned mock_bt_hfp_manager_disconnect_calls(void) { return s_disconnect_calls; }
unsigned mock_bt_hfp_manager_audio_start_calls(void) { return s_audio_start_calls; }
unsigned mock_bt_hfp_manager_audio_stop_calls(void) { return s_audio_stop_calls; }
unsigned mock_bt_hfp_manager_mode_calls(void) { return s_mode_calls; }
unsigned mock_bt_hfp_manager_reset_stats_calls(void) { return s_reset_stats_calls; }
const char *mock_bt_hfp_manager_last_connect_mac(void) { return s_last_connect_mac; }
bt_duplex_mode_t mock_bt_hfp_manager_last_mode(void) { return s_last_mode; }

esp_err_t bt_manager_hfp_connect(const char *mac)
{
    s_connect_calls++;
    if (mac != NULL) {
        strncpy(s_last_connect_mac, mac, sizeof(s_last_connect_mac) - 1U);
        s_last_connect_mac[sizeof(s_last_connect_mac) - 1U] = '\0';
    }
    return s_connect_result;
}

esp_err_t bt_manager_hfp_disconnect(void)
{
    s_disconnect_calls++;
    return s_disconnect_result;
}

esp_err_t bt_manager_hfp_audio_start(void)
{
    s_audio_start_calls++;
    return s_audio_start_result;
}

esp_err_t bt_manager_hfp_audio_stop(void)
{
    s_audio_stop_calls++;
    return s_audio_stop_result;
}

esp_err_t bt_manager_hfp_set_mode(bt_duplex_mode_t mode)
{
    s_mode_calls++;
    s_last_mode = mode;
    if (s_mode_result == ESP_OK) {
        s_status.configured_mode = mode;
        s_status.duplex.requested_mode = mode;
        s_status.duplex.effective_mode = mode == BT_DUPLEX_MODE_AUTO
            ? BT_DUPLEX_MODE_A2DP_PLUS_HFP_MIC : mode;
    }
    return s_mode_result;
}

esp_err_t bt_manager_hfp_get_configured_mode(bt_duplex_mode_t *mode_out)
{
    if (mode_out == NULL) return ESP_ERR_INVALID_ARG;
    *mode_out = s_status.configured_mode;
    return ESP_OK;
}

esp_err_t bt_manager_hfp_get_status(bt_hfp_manager_status_t *out)
{
    s_status_calls++;
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    if (s_status_result == ESP_OK) *out = s_status;
    return s_status_result;
}

esp_err_t bt_manager_hfp_get_stats(bt_hfp_manager_stats_t *out)
{
    s_stats_calls++;
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    if (s_stats_result == ESP_OK) *out = s_stats;
    return s_stats_result;
}

esp_err_t bt_manager_hfp_reset_stats(void)
{
    s_reset_stats_calls++;
    return s_reset_stats_result;
}
