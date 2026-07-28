#pragma once

#include <stdint.h>

#include "bt_duplex_policy.h"
#include "bt_duplex_state.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BT_HFP_MODE_EVENT_REASON_COMMAND = 0,
    BT_HFP_MODE_EVENT_REASON_SESSION_BEGIN,
    BT_HFP_MODE_EVENT_REASON_RECOVERY,
    BT_HFP_MODE_EVENT_REASON_POLICY,
    BT_HFP_MODE_EVENT_REASON_REMOTE_SUSPENDED_A2DP_DURING_SCO,
    BT_HFP_MODE_EVENT_REASON_A2DP_STOPPED_DURING_SCO,
    BT_HFP_MODE_EVENT_REASON_A2DP_RESUMED,
    BT_HFP_MODE_EVENT_REASON_SCO_STOPPED,
    BT_HFP_MODE_EVENT_REASON_COUNT,
} bt_hfp_mode_event_reason_t;

/* Stable HFP event contract. All fields are emitted as bounded,
 * delimiter-free protocol tokens through the command interface's EVENT path. */
esp_err_t bt_hfp_event_emit_profile(bt_hfp_profile_state_t state,
                                     const char *peer_mac,
                                     uint32_t session_generation);
esp_err_t bt_hfp_event_emit_audio(bt_hfp_audio_state_t state,
                                  bt_hfp_codec_t codec,
                                  uint32_t session_generation);
esp_err_t bt_hfp_event_emit_mode(bt_duplex_mode_t old_mode,
                                 bt_duplex_mode_t new_mode,
                                 bt_hfp_mode_event_reason_t reason,
                                 uint32_t session_generation);
esp_err_t bt_hfp_event_emit_policy(bt_duplex_policy_state_t state,
                                   bt_duplex_policy_reason_t reason,
                                   bt_duplex_mode_t requested,
                                   bt_duplex_mode_t effective,
                                   bt_duplex_downlink_owner_t downlink_owner,
                                   uint32_t session_generation);
esp_err_t bt_hfp_event_emit_i2s(bt_hfp_i2s_state_t state,
                                esp_err_t error,
                                uint32_t session_generation);
esp_err_t bt_hfp_event_emit_health(bt_audio_health_t severity,
                                   esp_err_t reason_error,
                                   uint64_t count,
                                   uint32_t session_generation);

#ifdef __cplusplus
}
#endif
