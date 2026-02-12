/**
 * @file bt_events_gap.h
 * @brief Bluetooth GAP (Generic Access Profile) event handlers
 *
 * Handles Bluetooth GAP events including device discovery and pairing events.
 * Delegates to appropriate subsystems (bt_scan, bt_pairing_store).
 */

#ifndef BT_EVENTS_GAP_H
#define BT_EVENTS_GAP_H

#include "esp_gap_bt_api.h"

// Expose callback for both ESP_PLATFORM and UNIT_TEST
#if defined(ESP_PLATFORM) || defined(UNIT_TEST)
/**
 * @brief GAP callback for Bluetooth events
 * 
 * Handles:
 * - ESP_BT_GAP_DISC_RES_EVT: Device discovery results (delegates to bt_scan)
 * - ESP_BT_GAP_DISC_STATE_CHANGED_EVT: Discovery state changes (delegates to bt_scan)
 * - ESP_BT_GAP_PIN_REQ_EVT: PIN code request for legacy pairing
 * - ESP_BT_GAP_CFM_REQ_EVT: SSP confirmation request (numeric comparison)
 * - ESP_BT_GAP_AUTH_CMPL_EVT: Authentication complete (pairing result)
 * 
 * @param event GAP event type
 * @param param Event parameters (event-specific data)
 */
void bt_events_gap_callback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param);

#endif // defined(ESP_PLATFORM) || defined(UNIT_TEST)

#endif // BT_EVENTS_GAP_H
