#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "bt_app_core.h"
#include "bt_hfp_audio.h"
#include "hfp_i2s_output.h"

static bool s_dispatch_accept;
static bool s_callback_registered;
static esp_err_t s_apply_result;
static esp_err_t s_i2s_init_result;
static esp_err_t s_i2s_start_result;
static esp_err_t s_i2s_stop_result;
static bool s_i2s_start_quarantine;
static bool s_i2s_stop_quarantine;
static hfp_i2s_output_snapshot_t s_i2s;
static unsigned s_dispatch_calls;
static unsigned s_i2s_init_calls;
static unsigned s_i2s_start_calls;
static unsigned s_i2s_stop_calls;
static unsigned s_profile_stopping_calls;
static unsigned s_apply_calls;
static uint32_t s_apply_generation;
static uint16_t s_apply_handle;
static char s_apply_peer[BT_DUPLEX_MAC_STR_LEN];

void mock_hfp_audio_control_dependencies_reset(void)
{
    s_dispatch_accept = true;
    s_callback_registered = true;
    s_apply_result = ESP_OK;
    s_i2s_init_result = ESP_OK;
    s_i2s_start_result = ESP_OK;
    s_i2s_stop_result = ESP_OK;
    s_i2s_start_quarantine = false;
    s_i2s_stop_quarantine = false;
    memset(&s_i2s, 0, sizeof(s_i2s));
    s_i2s.state = HFP_I2S_OUTPUT_UNINITIALIZED;
    s_i2s.config.stop_timeout_ms = 10U;
    s_dispatch_calls = 0U;
    s_i2s_init_calls = 0U;
    s_i2s_start_calls = 0U;
    s_i2s_stop_calls = 0U;
    s_profile_stopping_calls = 0U;
    s_apply_calls = 0U;
    s_apply_generation = 0U;
    s_apply_handle = 0U;
    memset(s_apply_peer, 0, sizeof(s_apply_peer));
}

void mock_hfp_audio_control_set_dispatch_accept(bool accept)
{
    s_dispatch_accept = accept;
}

void mock_hfp_audio_control_set_callback_registered(bool registered)
{
    s_callback_registered = registered;
}

void mock_hfp_audio_control_set_apply_result(esp_err_t result)
{
    s_apply_result = result;
}

void mock_hfp_audio_control_set_i2s_init_result(esp_err_t result)
{
    s_i2s_init_result = result;
}

void mock_hfp_audio_control_set_i2s_start_result(esp_err_t result,
                                                 bool quarantine)
{
    s_i2s_start_result = result;
    s_i2s_start_quarantine = quarantine;
}

void mock_hfp_audio_control_set_i2s_stop_result(esp_err_t result,
                                                bool quarantine)
{
    s_i2s_stop_result = result;
    s_i2s_stop_quarantine = quarantine;
}

void mock_hfp_audio_control_force_i2s_running(uint32_t generation,
                                              const char *peer)
{
    s_i2s.initialized = true;
    s_i2s.state = HFP_I2S_OUTPUT_RUNNING;
    s_i2s.generation = generation;
    if (peer != NULL) {
        strncpy(s_i2s.peer_mac, peer, sizeof(s_i2s.peer_mac) - 1U);
        s_i2s.peer_mac[sizeof(s_i2s.peer_mac) - 1U] = '\0';
    }
}

unsigned mock_hfp_audio_control_dispatch_calls(void)
{
    return s_dispatch_calls;
}

unsigned mock_hfp_audio_control_i2s_init_calls(void)
{
    return s_i2s_init_calls;
}

unsigned mock_hfp_audio_control_i2s_start_calls(void)
{
    return s_i2s_start_calls;
}

unsigned mock_hfp_audio_control_i2s_stop_calls(void)
{
    return s_i2s_stop_calls;
}

unsigned mock_hfp_audio_control_profile_stopping_calls(void)
{
    return s_profile_stopping_calls;
}

unsigned mock_hfp_audio_control_apply_calls(void)
{
    return s_apply_calls;
}

uint32_t mock_hfp_audio_control_apply_generation(void)
{
    return s_apply_generation;
}

uint16_t mock_hfp_audio_control_apply_handle(void)
{
    return s_apply_handle;
}

const char *mock_hfp_audio_control_apply_peer(void)
{
    return s_apply_peer;
}

bool bt_app_work_dispatch(bt_app_cb_t callback, uint16_t event,
                          void *params, int param_len,
                          bt_app_copy_cb_t copy_callback)
{
    (void)param_len;
    (void)copy_callback;
    s_dispatch_calls++;
    if (!s_dispatch_accept || callback == NULL) return false;
    callback(event, params);
    return true;
}

esp_err_t bt_hfp_audio_get_snapshot(bt_hfp_audio_snapshot_t *out)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    out->initialized = true;
    out->callback_registered = s_callback_registered;
    return ESP_OK;
}

void bt_hfp_audio_profile_stopping(void)
{
    s_profile_stopping_calls++;
}

esp_err_t bt_hfp_audio_apply_duplex_state(
    const bt_duplex_snapshot_t *snapshot,
    const char *event_peer_mac,
    uint16_t sync_conn_handle,
    uint16_t preferred_frame_size)
{
    (void)preferred_frame_size;
    s_apply_calls++;
    s_apply_generation = snapshot == NULL ? 0U : snapshot->session_generation;
    s_apply_handle = sync_conn_handle;
    if (event_peer_mac != NULL) {
        strncpy(s_apply_peer, event_peer_mac, sizeof(s_apply_peer) - 1U);
        s_apply_peer[sizeof(s_apply_peer) - 1U] = '\0';
    }
    return s_apply_result;
}

esp_err_t hfp_i2s_output_default_config(hfp_i2s_output_config_t *out)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    out->port = 0;
    out->bclk_gpio = 32;
    out->ws_gpio = 33;
    out->dout_gpio = 27;
    out->sample_rate_hz = 16000U;
    out->ring_bytes = 4096U;
    out->dma_desc_num = 8U;
    out->dma_frame_num = 120U;
    out->writer_samples = 120U;
    out->task_stack_bytes = 4096U;
    out->task_priority = 5U;
    out->write_timeout_ms = 20U;
    out->stop_timeout_ms = 10U;
    out->max_consecutive_write_failures = 3U;
    out->underflow_degraded_threshold = 20U;
    return ESP_OK;
}

esp_err_t hfp_i2s_output_get_runtime_pin_owners(hfp_i2s_pin_owners_t *out)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    out->playback_i2s_valid = true;
    out->playback_i2s_port = 1;
    return ESP_OK;
}

esp_err_t hfp_i2s_output_init(const hfp_i2s_output_config_t *config,
                              const hfp_i2s_pin_owners_t *owners)
{
    if (config == NULL || owners == NULL) return ESP_ERR_INVALID_ARG;
    s_i2s_init_calls++;
    if (s_i2s_init_result != ESP_OK) return s_i2s_init_result;
    s_i2s.initialized = true;
    s_i2s.state = HFP_I2S_OUTPUT_STOPPED;
    s_i2s.config = *config;
    s_i2s.last_error = ESP_OK;
    return ESP_OK;
}

esp_err_t hfp_i2s_output_start(uint32_t generation, const char *peer_mac)
{
    s_i2s_start_calls++;
    if (s_i2s_start_result != ESP_OK) {
        s_i2s.initialized = true;
        s_i2s.state = s_i2s_start_quarantine
            ? HFP_I2S_OUTPUT_QUARANTINED
            : HFP_I2S_OUTPUT_STOPPED;
        s_i2s.quarantined = s_i2s_start_quarantine;
        s_i2s.last_error = s_i2s_start_result;
        return s_i2s_start_result;
    }
    s_i2s.initialized = true;
    s_i2s.state = HFP_I2S_OUTPUT_RUNNING;
    s_i2s.generation = generation;
    strncpy(s_i2s.peer_mac, peer_mac, sizeof(s_i2s.peer_mac) - 1U);
    s_i2s.peer_mac[sizeof(s_i2s.peer_mac) - 1U] = '\0';
    s_i2s.last_error = ESP_OK;
    return ESP_OK;
}

esp_err_t hfp_i2s_output_stop(uint32_t timeout_ms)
{
    (void)timeout_ms;
    s_i2s_stop_calls++;
    if (s_i2s_stop_result != ESP_OK) {
        s_i2s.state = s_i2s_stop_quarantine
            ? HFP_I2S_OUTPUT_QUARANTINED
            : HFP_I2S_OUTPUT_FAULTED;
        s_i2s.quarantined = s_i2s_stop_quarantine;
        s_i2s.last_error = s_i2s_stop_result;
        return s_i2s_stop_result;
    }
    s_i2s.state = HFP_I2S_OUTPUT_STOPPED;
    s_i2s.generation = 0U;
    s_i2s.peer_mac[0] = '\0';
    s_i2s.last_error = ESP_OK;
    return ESP_OK;
}

esp_err_t hfp_i2s_output_get_snapshot(hfp_i2s_output_snapshot_t *out)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    if (!s_i2s.initialized &&
        s_i2s.state == HFP_I2S_OUTPUT_UNINITIALIZED) {
        return ESP_ERR_INVALID_STATE;
    }
    *out = s_i2s;
    return ESP_OK;
}
