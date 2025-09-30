/**
 * Bluetooth Mock Devices
 *
 * This file provides mock implementations for Bluetooth device management functions.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "bt_source.h" // Include the main header to use its type definitions

#ifdef __cplusplus
extern "C" {
#endif

// Initialization and control functions
void bt_mock_init(void);
void bt_mock_reset(void);
void bt_mock_cleanup(void);
void bt_mock_add_device(const char *addr, const char *name, bt_device_type_t type, bool paired);

// Connection functions
esp_err_t bt_mock_connect(const char* addr);
esp_err_t bt_mock_disconnect(void);
bool bt_mock_is_connected(void);
esp_err_t bt_mock_hook_connect_by_name(const char* name);
void bt_mock_set_connect_by_name_hook(const char* name, const char* addr);

// Scanning functions
void bt_mock_start_scan(void);
void bt_mock_stop_scan(void);
int bt_mock_get_scan_results(bt_device_t* devices, int max_count);

// Pairing functions
esp_err_t bt_mock_start_pairing(const char* addr);
bt_pairing_state_t bt_mock_get_pairing_state(void);
bt_pairing_method_t bt_mock_get_pairing_method(void);
void bt_mock_set_ssp_supported(bool supported);
esp_err_t bt_mock_simulate_ssp_request(uint32_t passkey);
bool bt_mock_is_ssp_confirm_requested(void);
esp_err_t bt_mock_confirm_ssp(bool confirm);
void bt_mock_simulate_pairing_timeout(void);
void bt_mock_simulate_pin_failure(void);

// Device management
bool bt_mock_is_device_paired(const char* addr);
esp_err_t bt_mock_add_paired_device(bt_device_t* device);
esp_err_t bt_mock_get_paired_devices(bt_device_t* devices, uint16_t max_count, uint16_t *actual_count);
esp_err_t bt_mock_unpair_device(const char* addr);
esp_err_t bt_mock_unpair_all_devices(void);
uint16_t bt_mock_get_paired_device_count(void);
esp_err_t bt_mock_set_default_pin(const char* pin);
esp_err_t bt_mock_get_default_pin(char* pin, size_t size);

#ifdef __cplusplus
}
#endif