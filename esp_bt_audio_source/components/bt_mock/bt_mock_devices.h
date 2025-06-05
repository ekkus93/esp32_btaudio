#ifndef BT_MOCK_DEVICES_H
#define BT_MOCK_DEVICES_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "bt_source.h"  // This already includes the enum definitions we need

// Make sure the bt_mock_confirm_ssp function is properly declared here
esp_err_t bt_mock_confirm_ssp(bool confirm);

// Add other function declarations
void bt_mock_init(void);
void bt_mock_reset(void);
void bt_mock_cleanup(void);
void bt_mock_add_device(const char* addr, const char* name, bt_device_type_t type, bool paired);
esp_err_t bt_mock_connect(const char* addr);
esp_err_t bt_mock_disconnect(void);
bool bt_mock_is_connected(void);
void bt_mock_start_scan(void);
void bt_mock_stop_scan(void);
int bt_mock_get_scan_results(bt_device_t* devices, int max_count);
void bt_mock_set_connect_by_name_hook(const char* name, const char* addr);
bool bt_mock_is_device_paired(const char* addr);
esp_err_t bt_mock_add_paired_device(const bt_device_t* device);
esp_err_t bt_mock_hook_connect_by_name(const char* name);
void bt_mock_simulate_pairing_timeout(void);
esp_err_t bt_mock_send_pin(const char* pin);
void bt_mock_simulate_pin_failure(void);
esp_err_t bt_mock_simulate_ssp_request(uint32_t passkey);
void bt_mock_set_ssp_supported(bool supported);
esp_err_t bt_mock_start_pairing(const char* addr);
uint32_t bt_mock_get_ssp_passkey(void);
bt_pairing_state_t bt_mock_get_pairing_state(void);
bt_pairing_method_t bt_mock_get_pairing_method(void);
bool bt_mock_is_ssp_confirm_requested(void);
esp_err_t bt_mock_unpair_device(const char* addr);
esp_err_t bt_mock_unpair_all_devices(void);
int bt_mock_get_paired_device_count(void);
int bt_mock_get_paired_devices(bt_device_t* devices, int max_count);

#endif /* BT_MOCK_DEVICES_H */
