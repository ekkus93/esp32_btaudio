#include "bt_duplex_state_internal.h"

esp_err_t bt_duplex_set_hfp_profile_global_state(
    bt_hfp_profile_state_t state)
{
    if (!BT_DUPLEX_ENUM_VALUE_VALID(state, BT_HFP_PROFILE_STATE_COUNT)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (state != BT_HFP_PROFILE_UNINITIALIZED &&
        state != BT_HFP_PROFILE_DISCONNECTED &&
        state != BT_HFP_PROFILE_FAULTED) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = bt_duplex_lock();
    if (err != ESP_OK) {
        return err;
    }

    bt_hfp_profile_state_t current =
        g_bt_duplex_ctx.snapshot.hfp_profile_state;
    bool allowed = current == state;

    if (state == BT_HFP_PROFILE_FAULTED) {
        allowed = true;
    } else if (state == BT_HFP_PROFILE_DISCONNECTED) {
        allowed = current == BT_HFP_PROFILE_UNINITIALIZED;
    } else if (state == BT_HFP_PROFILE_UNINITIALIZED) {
        allowed = bt_duplex_transient_resources_stopped_locked();
    }

    if (!allowed) {
        return bt_duplex_unlock_result(bt_duplex_illegal_locked());
    }

    g_bt_duplex_ctx.snapshot.hfp_profile_state = state;
    if (state == BT_HFP_PROFILE_UNINITIALIZED) {
        g_bt_duplex_ctx.snapshot.hfp_audio_state =
            BT_HFP_AUDIO_DISCONNECTED;
        g_bt_duplex_ctx.snapshot.codec = BT_HFP_CODEC_NONE;
    }
    return bt_duplex_unlock_result(ESP_OK);
}
