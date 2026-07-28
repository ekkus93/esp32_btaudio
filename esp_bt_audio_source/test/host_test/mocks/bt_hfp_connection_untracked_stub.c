#include "bt_hfp_connection.h"

esp_err_t bt_hfp_connection_handle_event(
    const char *peer_mac,
    bt_hfp_ag_connection_state_t state)
{
    (void)peer_mac;
    (void)state;
    return ESP_ERR_NOT_FOUND;
}
