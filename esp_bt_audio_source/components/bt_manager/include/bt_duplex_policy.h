#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "bt_duplex_state.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BT_DUPLEX_POLICY_SATISFIED = 0,
    BT_DUPLEX_POLICY_WAITING,
    BT_DUPLEX_POLICY_INCOMPATIBLE,
    BT_DUPLEX_POLICY_COMPATIBILITY_REQUIRED,
    BT_DUPLEX_POLICY_STATE_COUNT,
} bt_duplex_policy_state_t;

typedef enum {
    BT_DUPLEX_POLICY_REASON_REQUESTED_MODE = 0,
    BT_DUPLEX_POLICY_REASON_WAITING_A2DP_CONNECTION,
    BT_DUPLEX_POLICY_REASON_WAITING_A2DP_STREAM,
    BT_DUPLEX_POLICY_REASON_WAITING_HFP_SLC,
    BT_DUPLEX_POLICY_REASON_WAITING_SCO,
    BT_DUPLEX_POLICY_REASON_REMOTE_SUSPENDED_A2DP_DURING_SCO,
    BT_DUPLEX_POLICY_REASON_A2DP_STOPPED_DURING_SCO,
    BT_DUPLEX_POLICY_REASON_A2DP_RESUMED,
    BT_DUPLEX_POLICY_REASON_SCO_STOPPED,
    BT_DUPLEX_POLICY_REASON_COUNT,
} bt_duplex_policy_reason_t;

typedef enum {
    BT_DUPLEX_DOWNLINK_OWNER_A2DP = 0,
    BT_DUPLEX_DOWNLINK_OWNER_HFP,
    BT_DUPLEX_DOWNLINK_OWNER_COUNT,
} bt_duplex_downlink_owner_t;

typedef struct {
    bt_duplex_mode_t requested;
    bt_duplex_mode_t current_effective;
    bool a2dp_connected;
    bool a2dp_streaming;
    bool hfp_slc_connected;
    bool sco_connected;
    bool a2dp_interrupted_during_sco;
    bt_duplex_policy_reason_t interruption_reason;
} bt_duplex_policy_input_t;

typedef struct {
    bt_duplex_mode_t effective;
    bt_duplex_policy_state_t state;
    bt_duplex_policy_reason_t reason;
    bt_duplex_downlink_owner_t downlink_owner;
    bool request_hfp_downlink;
} bt_duplex_policy_result_t;

typedef struct {
    bool initialized;
    uint32_t generation;
    bt_duplex_mode_t requested;
    bt_duplex_mode_t effective;
    bt_duplex_policy_state_t state;
    bt_duplex_policy_reason_t reason;
    bt_duplex_downlink_owner_t downlink_owner;
    bool request_hfp_downlink;
    uint64_t evaluations_lifetime;
    uint64_t effective_mode_changes_lifetime;
    uint64_t incompatibilities_lifetime;
    uint64_t a2dp_interruptions_lifetime;
} bt_duplex_policy_snapshot_t;

/* Pure and allocation-free. The result always names exactly one downlink
 * owner, so A2DP and HFP can never simultaneously own the same downlink path. */
esp_err_t bt_duplex_policy_evaluate(const bt_duplex_policy_input_t *input,
                                    bt_duplex_policy_result_t *result_out);

const char *bt_duplex_policy_state_to_string(bt_duplex_policy_state_t value);
const char *bt_duplex_policy_reason_to_string(bt_duplex_policy_reason_t value);
const char *bt_duplex_downlink_owner_to_string(
    bt_duplex_downlink_owner_t value);

/* FD-16 manager adapter. These APIs serialize policy application through the
 * existing bt_ctx ownership lock and bind every event to the active peer and
 * generation. Expected duplicate transitions are harmless and emit nothing. */
void bt_manager_hfp_policy_runtime_reset(void);
esp_err_t bt_manager_hfp_policy_refresh(void);
esp_err_t bt_manager_hfp_policy_note_hfp_profile_transition(
    uint32_t generation,
    const char *peer_mac,
    bt_hfp_profile_state_t old_state,
    bt_hfp_profile_state_t new_state);
esp_err_t bt_manager_hfp_policy_note_hfp_audio_transition(
    uint32_t generation,
    const char *peer_mac,
    bt_hfp_audio_state_t old_state,
    bt_hfp_audio_state_t new_state);
esp_err_t bt_manager_hfp_handle_a2dp_profile_event(
    const char *peer_mac,
    bt_a2dp_profile_state_t state);
esp_err_t bt_manager_hfp_handle_a2dp_audio_event(
    const char *peer_mac,
    bt_a2dp_audio_state_t state);
esp_err_t bt_manager_hfp_get_policy_snapshot(
    bt_duplex_policy_snapshot_t *out);

/* Internal helpers. Caller must hold bt_ctx. */
esp_err_t bt_manager_hfp_policy_refresh_locked(void);
void bt_manager_hfp_policy_copy_locked(bt_duplex_policy_snapshot_t *out);

#ifdef __cplusplus
}
#endif
