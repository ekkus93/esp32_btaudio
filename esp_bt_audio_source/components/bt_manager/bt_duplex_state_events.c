#include "bt_duplex_state_events.h"

#include "bt_duplex_state_internal.h"

esp_err_t bt_duplex_record_stale_operation_event(
    uint32_t generation,
    const char *peer_mac)
{
    esp_err_t err = bt_duplex_lock();
    if (err != ESP_OK) return err;

    err = bt_duplex_validate_event_locked(generation, peer_mac);
    if (err == ESP_OK) {
        g_bt_duplex_ctx.snapshot.counters.stale_generation_events++;
    }

    return bt_duplex_unlock_result(err);
}
