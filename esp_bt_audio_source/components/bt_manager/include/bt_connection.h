#pragma once

/**
 * bt_connection.h - Bluetooth connection initiation and termination
 * 
 * Manages Bluetooth Classic A2DP connection lifecycle including:
 * - Initiating connections to devices (by MAC or name)
 * - Disconnecting from devices
 * - Connection request validation
 */

#include "bt_api.h"
#include <stdbool.h>

/**
 * Connect to a Bluetooth device by MAC address
 * 
 * Initiates an A2DP source connection to the specified device.
 * Fails if already connected or if the manager is not initialized.
 * 
 * @param mac MAC address string (format: "XX:XX:XX:XX:XX:XX")
 * @return ESP_OK on success, ESP_FAIL on error
 */
bt_err_t bt_connect(const char* mac);

/**
 * Connect to a Bluetooth device by name
 * 
 * Searches discovered and paired device lists for a device with
 * the specified name, then initiates connection.
 * 
 * @param name Device name to search for
 * @return ESP_OK on success, ESP_FAIL/ESP_ERR_* on error
 */
bt_err_t bt_connect_by_name(const char* name);

/**
 * Disconnect from the currently connected device
 * 
 * Stops audio if playing, then disconnects the A2DP connection.
 * Idempotent - returns success if already disconnected or not initialized.
 * 
 * @return ESP_OK on success, ESP_FAIL/ESP_ERR_* on error
 */
bt_err_t bt_disconnect(void);

#endif // BT_CONNECTION_H
