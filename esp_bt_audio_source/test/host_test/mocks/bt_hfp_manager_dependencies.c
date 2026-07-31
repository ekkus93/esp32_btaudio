#include "bt_hfp_manager_dependencies.h"

#include <string.h>

#include "platform_sync.h"
#include "util_safe.h"

bt_manager_context_t bt_ctx;

static platform_mutex_t s_mock_bt_ctx_mutex;
static bt_hfp_connection_snapshot_t s_slc;
static bt_hfp_audio_control_snapshot_t s_control;
static bt_hfp_audio_snapshot_t s_incoming;
static hfp_i2s_output_snapshot_t s_i2s;
static esp_err_t s_slc_snapshot_result;
static esp_err_t s_control_snapshot_result;
static esp_err_t s_incoming_snapshot_result;
static esp_err_t s_i2s_snapshot_result;
static esp_err_t s_slc_connect_result;
static esp_err_t s_slc_disconnect_result;
static esp_err_t s_audio_start_result;
static esp_err_t s_audio_stop_result;
static unsigned s_slc_connect_calls;
static unsigned s_slc_disconnect_calls;
static unsigned s_audio_start_calls;
static unsigned s_audio_stop_calls;
static char s_last_slc_peer[BT_DUPLEX_MAC_STR_LEN];

esp_err_t bt_ctx_lock(uint32_t timeout_ms)
{
    if (s_mock_bt_ctx_mutex == NULL) return ESP_ERR_INVALID_STATE;
    return platform_mutex_lock(s_mock_bt_ctx_mutex, timeout_ms);
}

void bt_ctx_unlock(void)
{
    if (s_mock_bt_ctx_mutex != NULL) {
        (void)platform_mutex_unlock(s_mock_bt_ctx_mutex);
    }
}

void mock_bt_hfp_manager_dependencies_reset_modules(void)
{
    memset(&s_slc, 0, sizeof(s_slc));
    memset(&s_control, 0, sizeof(s_control));
    memset(&s_incoming, 0, sizeof(s_incoming));
    memset(&s_i2s, 0, sizeof(s_i2s));
    s_slc.state = BT_HFP_OPERATION_IDLE;
    s_slc.immediate_result = ESP_OK;
    s_slc.last_error = ESP_OK;
    s_control.state = BT_HFP_AUDIO_OPERATION_IDLE;
    s_control.immediate_result = ESP_OK;
    s_control.completion_result = ESP_OK;
    s_control.cleanup_result = ESP_OK;
    s_control.last_error = ESP_OK;
    s_i2s.state = HFP_I2S_OUTPUT_UNINITIALIZED;
    s_slc_snapshot_result = ESP_ERR_INVALID_STATE;
    s_control_snapshot_result = ESP_ERR_INVALID_STATE;
    s_incoming_snapshot_result = ESP_ERR_INVALID_STATE;
    s_i2s_snapshot_result = ESP_ERR_INVALID_STATE;
    s_slc_connect_result = ESP_OK;
    s_slc_disconnect_result = ESP_OK;
    s_audio_start_result = ESP_OK;
    s_audio_stop_result = ESP_OK;
    s_slc_connect_calls = 0U;
    s_slc_disconnect_calls = 0U;
    s_audio_start_calls = 0U;
    s_audio_stop_calls = 0U;
    s_last_slc_peer[0] = '\0';
}

esp_err_t mock_bt_hfp_manager_dependencies_init(const char *active_peer)
{
    if (s_mock_bt_ctx_mutex != NULL) platform_mutex_delete(s_mock_bt_ctx_mutex);
    s_mock_bt_ctx_mutex = platform_mutex_create();
    if (s_mock_bt_ctx_mutex == NULL) return ESP_ERR_NO_MEM;

    memset(&bt_ctx, 0, sizeof(bt_ctx));
    bt_ctx.initialized = true;
    if (active_peer != NULL) {
        bt_ctx.connected = true;
        util_safe_copy_str(bt_ctx.connected_mac,
                           sizeof(bt_ctx.connected_mac), active_peer);
    }
    mock_bt_hfp_manager_dependencies_reset_modules();
    return ESP_OK;
}

void mock_bt_hfp_manager_dependencies_deinit(void)
{
    memset(&bt_ctx, 0, sizeof(bt_ctx));
    if (s_mock_bt_ctx_mutex != NULL) {
        platform_mutex_delete(s_mock_bt_ctx_mutex);
        s_mock_bt_ctx_mutex = NULL;
    }
}

void mock_bt_hfp_manager_set_slc_snapshot(
    const bt_hfp_connection_snapshot_t *snapshot, esp_err_t result)
{
    if (snapshot != NULL) s_slc = *snapshot;
    s_slc_snapshot_result = result;
}

void mock_bt_hfp_manager_set_audio_control_snapshot(
    const bt_hfp_audio_control_snapshot_t *snapshot, esp_err_t result)
{
    if (snapshot != NULL) s_control = *snapshot;
    s_control_snapshot_result = result;
}

void mock_bt_hfp_manager_set_incoming_snapshot(
    const bt_hfp_audio_snapshot_t *snapshot, esp_err_t result)
{
    if (snapshot != NULL) s_incoming = *snapshot;
    s_incoming_snapshot_result = result;
}

void mock_bt_hfp_manager_set_i2s_snapshot(
    const hfp_i2s_output_snapshot_t *snapshot, esp_err_t result)
{
    if (snapshot != NULL) s_i2s = *snapshot;
    s_i2s_snapshot_result = result;
}

void mock_bt_hfp_manager_set_slc_connect_result(esp_err_t result)
{
    s_slc_connect_result = result;
}

void mock_bt_hfp_manager_set_slc_disconnect_result(esp_err_t result)
{
    s_slc_disconnect_result = result;
}

void mock_bt_hfp_manager_set_audio_start_result(esp_err_t result)
{
    s_audio_start_result = result;
}

void mock_bt_hfp_manager_set_audio_stop_result(esp_err_t result)
{
    s_audio_stop_result = result;
}

unsigned mock_bt_hfp_manager_slc_connect_calls(void) { return s_slc_connect_calls; }
unsigned mock_bt_hfp_manager_slc_disconnect_calls(void) { return s_slc_disconnect_calls; }
unsigned mock_bt_hfp_manager_audio_start_calls(void) { return s_audio_start_calls; }
unsigned mock_bt_hfp_manager_audio_stop_calls(void) { return s_audio_stop_calls; }
const char *mock_bt_hfp_manager_last_slc_peer(void) { return s_last_slc_peer; }

esp_err_t bt_hfp_connection_get_snapshot(bt_hfp_connection_snapshot_t *out)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    if (s_slc_snapshot_result == ESP_OK) *out = s_slc;
    return s_slc_snapshot_result;
}

esp_err_t bt_hfp_audio_control_get_snapshot(
    bt_hfp_audio_control_snapshot_t *out)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    if (s_control_snapshot_result == ESP_OK) *out = s_control;
    return s_control_snapshot_result;
}

esp_err_t bt_hfp_audio_get_snapshot(bt_hfp_audio_snapshot_t *out)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    if (s_incoming_snapshot_result == ESP_OK) *out = s_incoming;
    return s_incoming_snapshot_result;
}

esp_err_t hfp_i2s_output_get_snapshot(hfp_i2s_output_snapshot_t *out)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    if (s_i2s_snapshot_result == ESP_OK) *out = s_i2s;
    return s_i2s_snapshot_result;
}

esp_err_t bt_hfp_connect(const char *peer_mac)
{
    s_slc_connect_calls++;
    if (peer_mac != NULL) {
        util_safe_copy_str(s_last_slc_peer, sizeof(s_last_slc_peer), peer_mac);
    }
    return s_slc_connect_result;
}

esp_err_t bt_hfp_disconnect(void)
{
    s_slc_disconnect_calls++;
    return s_slc_disconnect_result;
}

esp_err_t bt_hfp_audio_start(void)
{
    s_audio_start_calls++;
    return s_audio_start_result;
}

esp_err_t bt_hfp_audio_stop(void)
{
    s_audio_stop_calls++;
    return s_audio_stop_result;
}
