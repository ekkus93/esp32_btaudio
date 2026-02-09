/**
 * @file bt_events_avrc.h
 * @brief Bluetooth AVRC (Audio Video Remote Control) event handlers
 *
 * Handles AVRCP controller events for remote control functionality.
 * Currently provides minimal logging for connection state and passthrough events.
 */

#ifndef BT_EVENTS_AVRC_H
#define BT_EVENTS_AVRC_H

#ifdef ESP_PLATFORM
#include "esp_avrc_api.h"

/**
 * @brief AVRC controller callback for remote control events
 * 
 * Handles:
 * - ESP_AVRC_CT_CONNECTION_STATE_EVT: AVRCP connection state changes
 * - ESP_AVRC_CT_PASSTHROUGH_RSP_EVT: Passthrough command responses
 * - ESP_AVRC_CT_REMOTE_FEATURES_EVT: Remote device feature discovery
 * 
 * Currently provides logging only, no substantive action.
 * 
 * @param event AVRC controller event type
 * @param param Event parameters (event-specific data)
 */
void bt_events_avrc_callback(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *param);

#endif // ESP_PLATFORM

#endif // BT_EVENTS_AVRC_H
