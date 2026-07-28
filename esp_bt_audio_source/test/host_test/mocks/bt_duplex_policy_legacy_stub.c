#include "bt_duplex_policy.h"

#include <string.h>

static bt_duplex_policy_snapshot_t s_snapshot;

/* Pre-FD-16 focused suites validate HFP profile/event/manager behavior without
 * linking the real FD-16 manager adapter. These stubs make that boundary
 * explicit. The dedicated FD-16 sanitizer suite links the real adapter. */
void bt_manager_hfp_policy_runtime_reset(void)
{
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_snapshot.reason = BT_DUPLEX_POLICY_REASON_REQUESTED_MODE;
    s_snapshot.downlink_owner = BT_DUPLEX_DOWNLINK_OWNER_A2DP;
}

esp_err_t bt_manager_hfp_policy_refresh(void)
{
    return ESP_OK;
}

esp_err_t bt_manager_hfp_policy_refresh_locked(void)
{
    return ESP_OK;
}

void bt_manager_hfp_policy_copy_locked(bt_duplex_policy_snapshot_t *out)
{
    if (out != NULL) *out = s_snapshot;
}

esp_err_t bt_manager_hfp_policy_note_hfp_profile_transition(
    uint32_t generation,
    const char *peer_mac,
    bt_hfp_profile_state_t old_state,
    bt_hfp_profile_state_t new_state)
{
    (void)generation;
    (void)peer_mac;
    (void)old_state;
    (void)new_state;
    return ESP_OK;
}

esp_err_t bt_manager_hfp_policy_note_hfp_audio_transition(
    uint32_t generation,
    const char *peer_mac,
    bt_hfp_audio_state_t old_state,
    bt_hfp_audio_state_t new_state)
{
    (void)generation;
    (void)peer_mac;
    (void)old_state;
    (void)new_state;
    return ESP_OK;
}
