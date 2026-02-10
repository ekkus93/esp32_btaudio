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

#ifdef ESP_PLATFORM
/* Internal functions used by bt_manager GAP callback */

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
#endif  // ESP_PLATFORM
