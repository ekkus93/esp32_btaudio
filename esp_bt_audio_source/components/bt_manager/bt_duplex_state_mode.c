#include "bt_duplex_state_mode.h"

#include "bt_duplex_state_internal.h"

esp_err_t bt_duplex_set_mode_with_reason(
    uint32_t generation,
    const char *peer_mac,
    bt_duplex_mode_t requested_mode,
    bt_hfp_mode_event_reason_t reason)
{
    if (!BT_DUPLEX_ENUM_VALUE_VALID(requested_mode, BT_DUPLEX_MODE_COUNT) ||
        !BT_DUPLEX_ENUM_VALUE_VALID(reason, BT_HFP_MODE_EVENT_REASON_COUNT)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = bt_duplex_lock();
    if (err != ESP_OK) return err;

    err = bt_duplex_validate_event_locked(generation, peer_mac);
    if (err == ESP_OK && !bt_duplex_transient_resources_stopped_locked()) {
        err = bt_duplex_illegal_locked();
    }

    bool changed = false;
    bt_duplex_mode_t old_mode = BT_DUPLEX_MODE_DISABLED;
    if (err == ESP_OK) {
        old_mode = g_bt_duplex_ctx.snapshot.requested_mode;
        changed = old_mode != requested_mode;
        g_bt_duplex_ctx.snapshot.requested_mode = requested_mode;
        g_bt_duplex_ctx.snapshot.effective_mode =
            requested_mode == BT_DUPLEX_MODE_AUTO
                ? BT_DUPLEX_MODE_A2DP_PLUS_HFP_MIC
                : requested_mode;
    }

    err = bt_duplex_unlock_result(err);
    if (err != ESP_OK) return err;
    if (changed) {
        esp_err_t emit = bt_hfp_event_emit_mode(
            old_mode, requested_mode, reason, generation);
        if (emit != ESP_OK) bt_duplex_record_event_delivery_failure(
                generation, peer_mac, emit);
    }
    return ESP_OK;
}

esp_err_t bt_duplex_set_mode(uint32_t generation,
                             const char *peer_mac,
                             bt_duplex_mode_t requested_mode)
{
    return bt_duplex_set_mode_with_reason(
        generation, peer_mac, requested_mode,
        BT_HFP_MODE_EVENT_REASON_COMMAND);
}
