#pragma once

#include "bt_hfp_manager.h"

void mock_bt_hfp_manager_reset(void);
void mock_bt_hfp_manager_set_status(const bt_hfp_manager_status_t *status,
                                    esp_err_t result);
void mock_bt_hfp_manager_set_stats(const bt_hfp_manager_stats_t *stats,
                                   esp_err_t result);
void mock_bt_hfp_manager_set_diagnostics(
    const bt_hfp_manager_diagnostics_t *diagnostics,
    esp_err_t result);
void mock_bt_hfp_manager_set_connect_result(esp_err_t result);
void mock_bt_hfp_manager_set_disconnect_result(esp_err_t result);
void mock_bt_hfp_manager_set_audio_start_result(esp_err_t result);
void mock_bt_hfp_manager_set_audio_stop_result(esp_err_t result);
void mock_bt_hfp_manager_set_mode_result(esp_err_t result);
void mock_bt_hfp_manager_set_reset_stats_result(esp_err_t result);
unsigned mock_bt_hfp_manager_status_calls(void);
unsigned mock_bt_hfp_manager_stats_calls(void);
unsigned mock_bt_hfp_manager_diagnostics_calls(void);
unsigned mock_bt_hfp_manager_connect_calls(void);
unsigned mock_bt_hfp_manager_disconnect_calls(void);
unsigned mock_bt_hfp_manager_audio_start_calls(void);
unsigned mock_bt_hfp_manager_audio_stop_calls(void);
unsigned mock_bt_hfp_manager_mode_calls(void);
unsigned mock_bt_hfp_manager_reset_stats_calls(void);
const char *mock_bt_hfp_manager_last_connect_mac(void);
bt_duplex_mode_t mock_bt_hfp_manager_last_mode(void);
