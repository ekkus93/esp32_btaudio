#pragma once

/**
 * bt_pairing_store.h - Bluetooth pairing state management
 * 
 * Manages pending pairing requests (PIN and SSP) and provides the public
 * pairing API. This module handles:
 * - PIN-based legacy pairing
 * - SSP (Secure Simple Pairing) numeric comparison
 * - Pairing event notifications
 * - Pending pairing state tracking
 */

#include "bt_manager.h"
#include "bt_api.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef ESP_PLATFORM
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#else
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#endif

/**
 * Get pending pairing request information
 * 
 * Checks if there is a pending PIN or SSP pairing request and returns
 * the details if one exists.
 * 
 * @param info Output structure to fill with pending request info
 * @return true if a pending request exists, false otherwise
 */
bool bt_pairing_get_pending_request(bt_pairing_request_info_t* info);

/**
 * Confirm or reject an SSP pairing request
 * 
 * @param mac MAC address (or NULL to use current pending request)
 * @param accept true to accept, false to reject
 * @return ESP_OK on success, error code otherwise
 */
bt_err_t bt_pairing_confirm(const char* mac, bool accept);

/**
 * Submit PIN code for legacy pairing
 * 
 * @param mac MAC address (or NULL to use current pending request)
 * @param pin PIN code string (max 16 characters)
 * @return ESP_OK on success, error code otherwise
 */
bt_err_t bt_pairing_submit_pin(const char* mac, const char* pin);

/* Internal functions used by bt_manager and GAP callback */

/**
 * Handle GAP PIN request event
 * Internal use only - called from GAP callback
 */
void bt_pairing_handle_pin_request(const esp_bd_addr_t bda);

/**
 * Handle GAP SSP confirmation request event
 * Internal use only - called from GAP callback
 */
void bt_pairing_handle_ssp_confirm(const esp_bd_addr_t bda, uint32_t passkey);

/**
 * Handle GAP authentication complete event
 * Internal use only - called from GAP callback
 */
void bt_pairing_handle_auth_complete(const esp_bd_addr_t bda, esp_bt_status_t stat);

/**
 * Clear pending pairing state
 * Used for testing and cleanup
 */
void bt_pairing_clear_pending(void);

/**
 * Parse MAC address string to binary format
 * Internal helper for bt_pair and other functions
 */
bool bt_pairing_parse_mac(const char* mac, esp_bd_addr_t out);

/**
 * Prepare pairing state for a new pairing initiation
 * Internal use - called by bt_pair() before initiating GAP pairing
 */
void bt_pairing_prepare_for_initiation(const esp_bd_addr_t bda);

/**
 * Notify the pairing subsystem that an A2DP connection failed.
 *
 * If a pairing was initiated to bda but AUTH_CMPL never fired (e.g. page
 * timeout, HCI connection refused), call this from the A2DP DISCONNECTED
 * handler to emit EVENT|PAIR|FAILED and clear the pending state.
 *
 * Returns true if FAILED was emitted, false if no matching pending pairing.
 */
bool bt_pairing_handle_connection_failed(const esp_bd_addr_t bda);

#if CONFIG_BT_MOCK_TESTING
/**
 * Set mock pairing state for BT_MOCK_TESTING
 * Internal use - called by bt_pair() in mock testing mode
 */
void bt_pairing_set_mock_state(const esp_bd_addr_t bda, bool is_pin, uint32_t passkey);
#endif
