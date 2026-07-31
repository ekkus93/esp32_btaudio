#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "bt_events_a2dp.h"
#include "bt_manager.h"

#define A2DP_BINDING_MAC_STR_LEN 18U

typedef struct {
    bool valid;
    char peer_mac[A2DP_BINDING_MAC_STR_LEN];
    esp_a2d_conn_hdl_t conn_handle;
    uint32_t lifecycle_serial;
    uint32_t last_duplex_generation;
    uint64_t missing_binding_rejections;
    uint64_t wrong_peer_rejections;
    uint64_t stale_handle_rejections;
    uint64_t generation_sync_failures;
    uint64_t late_terminal_events_ignored;
} a2dp_policy_binding_t;

typedef struct {
    char peer_mac[A2DP_BINDING_MAC_STR_LEN];
    esp_a2d_conn_hdl_t conn_handle;
    uint32_t lifecycle_serial;
    uint32_t generation;
    bt_a2dp_profile_state_t state;
    bt_connected_cb connected_callback;
    bt_disconnected_cb disconnected_callback;
    char callback_mac[A2DP_BINDING_MAC_STR_LEN];
    char callback_name[32];
} a2dp_bound_profile_event_t;

typedef struct {
    char peer_mac[A2DP_BINDING_MAC_STR_LEN];
    esp_a2d_conn_hdl_t conn_handle;
    uint32_t lifecycle_serial;
    uint32_t generation;
    bt_a2dp_audio_state_t state;
} a2dp_bound_audio_event_t;

extern a2dp_policy_binding_t s_policy_binding;

#ifdef UNIT_TEST
extern esp_err_t s_test_last_generation_diag_update_error;
extern esp_err_t s_test_last_binding_clear_error;
extern esp_err_t s_test_last_stale_record_error;
extern esp_err_t s_test_last_unbound_status_error;
extern esp_err_t s_test_last_connection_policy_error;
#endif

/* Shared helpers (bt_events_a2dp.c) */
void bda_to_string(const uint8_t bda[6], char out[18]);
void increment_u64_saturating(uint64_t *value);
void report_policy_result(const char *event_name, esp_err_t err);

/* Binding/policy-state helpers (bt_events_a2dp_binding.c) */
esp_err_t record_rejected_bound_event(uint32_t generation,
                                       const char *event_peer,
                                       const char *reason);
esp_err_t record_rejected_unbound_event(const char *event_peer,
                                         const char *reason);
esp_err_t refresh_bound_generation(const char *peer,
                                    esp_a2d_conn_hdl_t conn_handle,
                                    uint32_t lifecycle_serial,
                                    bool allow_no_session,
                                    uint32_t fallback_generation,
                                    uint32_t *generation_out);
esp_err_t clear_binding_if_identity(uint32_t lifecycle_serial,
                                     esp_a2d_conn_hdl_t conn_handle);
esp_err_t capture_audio_binding(a2dp_bound_audio_event_t *bound);
esp_err_t prepare_connection_event(const esp_a2d_cb_param_t *param,
                                    a2dp_bound_profile_event_t *bound);
esp_err_t prepare_audio_event(const esp_a2d_cb_param_t *param,
                               a2dp_bound_audio_event_t *bound);
