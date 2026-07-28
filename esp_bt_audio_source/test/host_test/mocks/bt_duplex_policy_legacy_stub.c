#include "bt_duplex_policy.h"

/* Pre-FD-16 focused suites validate HFP profile/event behavior without linking
 * the bt_manager facade. These stubs make the boundary explicit. The real
 * policy adapter is exercised by the dedicated FD-16 sanitizer suite. */
esp_err_t bt_manager_hfp_policy_refresh(void)
{
    return ESP_OK;
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
