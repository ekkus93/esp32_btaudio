#pragma once

/**
 * bt_scan.h - Bluetooth device discovery and scanning
 * 
 * Manages Bluetooth Classic device discovery (scanning) including:
 * - Starting/stopping device discovery
 * - Processing discovery results (device found events)
 * - Maintaining discovered devices list
 * - Tracking scanning state
 */

#include "bt_api.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef ESP_PLATFORM
#include "esp_gap_bt_api.h"
#else
#include "esp_gap_bt_api.h"
#endif

/**
 * Start Bluetooth device discovery
 * 
 * Initiates a general inquiry to discover nearby Bluetooth devices.
 * Clears the previous discovered devices list before starting.
 * 
 * @return ESP_OK on success, ESP_FAIL if already initialized or on error
 */
bt_err_t bt_start_scan(void);

/**
 * Stop Bluetooth device discovery
 * 
 * Cancels an active device discovery session.
 * 
 * @return ESP_OK on success, ESP_FAIL if not initialized or on error
 */
bt_err_t bt_stop_scan(void);

/* Internal functions used by bt_manager GAP callback and host unit tests */

/**
 * Handle GAP device discovery result event
 *
 * Processes ESP_BT_GAP_DISC_RES_EVT to extract device information
 * (address, name, COD, RSSI) and add to discovered devices list.
 *
 * Internal use only - called from GAP callback
 *
 * @param bda Device Bluetooth address
 * @param num_prop Number of properties
 * @param prop Array of device properties
 */
void bt_scan_handle_discovery_result(const esp_bd_addr_t bda,
                                     int num_prop,
                                     esp_bt_gap_dev_prop_t *prop);

/**
 * Handle GAP discovery state change event
 *
 * Processes ESP_BT_GAP_DISC_STATE_CHANGED_EVT to track when
 * discovery starts or stops.
 *
 * Internal use only - called from GAP callback
 *
 * @param state Discovery state (STARTED or STOPPED)
 */
void bt_scan_handle_state_change(esp_bt_gap_discovery_state_t state);

#ifdef ESP_PLATFORM
/**
 * Handle ESP_BT_GAP_READ_REMOTE_NAME_EVT
 *
 * Called when an explicit remote name request (issued after inquiry stops
 * for devices whose name was not in EIR) completes.  Updates the device
 * name in the discovered-devices list and emits serial results once all
 * pending requests have been satisfied.
 *
 * Internal use only - called from GAP callback (ESP_PLATFORM only)
 *
 * @param bda     Remote device address
 * @param stat    Request status
 * @param rmt_name Remote device name (only valid when stat == ESP_BT_STATUS_SUCCESS)
 */
void bt_scan_handle_remote_name_evt(const esp_bd_addr_t bda,
                                    esp_bt_status_t stat,
                                    const uint8_t *rmt_name);
#endif
