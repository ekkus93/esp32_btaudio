#include "bt_duplex_state_internal.h"

#include <string.h>

#include "bt_hfp_event_contract.h"

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

    bool changed = current != state;
    bool audio_changed =
        state == BT_HFP_PROFILE_UNINITIALIZED &&
        g_bt_duplex_ctx.snapshot.hfp_audio_state !=
            BT_HFP_AUDIO_DISCONNECTED;
    uint32_t generation = g_bt_duplex_ctx.snapshot.session_generation;
    bool peer_valid = g_bt_duplex_ctx.snapshot.peer_valid;
    char peer[BT_DUPLEX_MAC_STR_LEN] = {0};
    if (peer_valid) {
        memcpy(peer, g_bt_duplex_ctx.snapshot.peer_mac, sizeof(peer));
    }

    g_bt_duplex_ctx.snapshot.hfp_profile_state = state;
    if (state == BT_HFP_PROFILE_UNINITIALIZED) {
        g_bt_duplex_ctx.snapshot.hfp_audio_state =
            BT_HFP_AUDIO_DISCONNECTED;
        g_bt_duplex_ctx.snapshot.codec = BT_HFP_CODEC_NONE;
    }
    err = bt_duplex_unlock_result(ESP_OK);
    if (err != ESP_OK) return err;

    if (changed) {
        esp_err_t emit = bt_hfp_event_emit_profile(
            state, peer_valid ? peer : NULL,
            peer_valid ? generation : 0U);
        if (emit != ESP_OK) bt_duplex_record_event_delivery_failure(
                peer_valid ? generation : 0U,
                peer_valid ? peer : NULL, emit);
    }
    if (audio_changed && peer_valid && generation != 0U) {
        esp_err_t emit = bt_hfp_event_emit_audio(
            BT_HFP_AUDIO_DISCONNECTED, BT_HFP_CODEC_NONE, generation);
        if (emit != ESP_OK) bt_duplex_record_event_delivery_failure(
                peer_valid ? generation : 0U,
                peer_valid ? peer : NULL, emit);
    }
    return ESP_OK;
}
