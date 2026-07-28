#include "bt_duplex_state_internal.h"

esp_err_t bt_duplex_audio_session_begin(const char *peer_mac,
                                        uint32_t *generation_out)
{
    if (!bt_duplex_valid_mac(peer_mac) || generation_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = bt_duplex_lock();
    if (err != ESP_OK) return err;

    err = bt_duplex_validate_event_locked(
        g_bt_duplex_ctx.snapshot.session_generation, peer_mac);
    if (err == ESP_OK &&
        g_bt_duplex_ctx.snapshot.hfp_profile_state !=
            BT_HFP_PROFILE_SLC_CONNECTED) {
        err = bt_duplex_illegal_locked();
    }
    if (err == ESP_OK &&
        (g_bt_duplex_ctx.snapshot.hfp_audio_state !=
             BT_HFP_AUDIO_DISCONNECTED ||
         g_bt_duplex_ctx.snapshot.i2s_state != BT_HFP_I2S_STOPPED)) {
        err = bt_duplex_illegal_locked();
    }
    if (err == ESP_OK &&
        (g_bt_duplex_ctx.snapshot.health >= BT_AUDIO_HEALTH_FAULTED ||
         g_bt_duplex_ctx.snapshot.requested_mode == BT_DUPLEX_MODE_DISABLED ||
         g_bt_duplex_ctx.snapshot.effective_mode == BT_DUPLEX_MODE_DISABLED)) {
        err = bt_duplex_illegal_locked();
    }

    if (err == ESP_OK) {
        uint32_t generation = bt_duplex_next_generation(
            g_bt_duplex_ctx.snapshot.session_generation);
        g_bt_duplex_ctx.snapshot.session_generation = generation;
        *generation_out = generation;
    }

    return bt_duplex_unlock_result(err);
}
