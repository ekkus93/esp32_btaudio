#include "bt_duplex_state_mode.h"

#include "bt_duplex_state_internal.h"

esp_err_t bt_duplex_set_mode(uint32_t generation,
                             const char *peer_mac,
                             bt_duplex_mode_t requested_mode)
{
    if (!BT_DUPLEX_ENUM_VALUE_VALID(requested_mode, BT_DUPLEX_MODE_COUNT)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = bt_duplex_lock();
    if (err != ESP_OK) return err;

    err = bt_duplex_validate_event_locked(generation, peer_mac);
    if (err == ESP_OK && !bt_duplex_transient_resources_stopped_locked()) {
        err = bt_duplex_illegal_locked();
    }

    if (err == ESP_OK) {
        g_bt_duplex_ctx.snapshot.requested_mode = requested_mode;
        g_bt_duplex_ctx.snapshot.effective_mode =
            requested_mode == BT_DUPLEX_MODE_AUTO
                ? BT_DUPLEX_MODE_A2DP_PLUS_HFP_MIC
                : requested_mode;
    }

    return bt_duplex_unlock_result(err);
}
