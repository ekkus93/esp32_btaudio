#pragma once

#include "bt_hfp_audio.h"
#include "bt_hfp_connection.h"
#include "bt_manager_internal.h"
#include "hfp_i2s_output.h"

esp_err_t mock_bt_hfp_manager_dependencies_init(const char *active_peer);
void mock_bt_hfp_manager_dependencies_deinit(void);
void mock_bt_hfp_manager_dependencies_reset_modules(void);
void mock_bt_hfp_manager_set_slc_snapshot(
    const bt_hfp_connection_snapshot_t *snapshot, esp_err_t result);
void mock_bt_hfp_manager_set_audio_control_snapshot(
    const bt_hfp_audio_control_snapshot_t *snapshot, esp_err_t result);
void mock_bt_hfp_manager_set_incoming_snapshot(
    const bt_hfp_audio_snapshot_t *snapshot, esp_err_t result);
void mock_bt_hfp_manager_set_i2s_snapshot(
    const hfp_i2s_output_snapshot_t *snapshot, esp_err_t result);
void mock_bt_hfp_manager_set_slc_connect_result(esp_err_t result);
void mock_bt_hfp_manager_set_slc_disconnect_result(esp_err_t result);
void mock_bt_hfp_manager_set_audio_start_result(esp_err_t result);
void mock_bt_hfp_manager_set_audio_stop_result(esp_err_t result);
unsigned mock_bt_hfp_manager_slc_connect_calls(void);
unsigned mock_bt_hfp_manager_slc_disconnect_calls(void);
unsigned mock_bt_hfp_manager_audio_start_calls(void);
unsigned mock_bt_hfp_manager_audio_stop_calls(void);
const char *mock_bt_hfp_manager_last_slc_peer(void);
