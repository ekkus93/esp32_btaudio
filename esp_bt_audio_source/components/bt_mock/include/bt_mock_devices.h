/**
 * Bluetooth Mock Devices Header
 */

#ifndef BT_MOCK_DEVICES_H
#define BT_MOCK_DEVICES_H

#include <stdbool.h>
#include "esp_err.h"
#include "bt_source.h"  // Include the main API header - single source of truth for types

// Structure to store the mock state
typedef struct {
    // Scan related fields
    bool scanning;
    
    // Device list related fields
    bt_device_t devices[10]; // Array of mock devices
    int device_count;
    
    // Connection related fields
    bool connected;
    char connected_addr[18]; // Address string in format xx:xx:xx:xx:xx:xx
    
    // For connect_by_name hook
    bool connect_by_name_hook_enabled;
    char connect_by_name_device[64];
    char connect_by_name_addr[18];
} mock_state_t;

// MOCK CONTROL API
// These functions control the mock behavior for tests

// Reset mock state between tests
void bt_mock_reset(void);

// Clean up mock resources
void bt_mock_cleanup(void);

// Add test device to mock database
void bt_mock_add_device(const char *addr, const char *name, bt_device_type_t type, bool paired);

// Set expected return values for various operations
void bt_mock_set_init_return(esp_err_t ret);
void bt_mock_set_scan_start_return(esp_err_t ret);
void bt_mock_set_connect_return(esp_err_t ret);

// Control mock device scanning
void bt_mock_start_scan(void);
void bt_mock_stop_scan(void);
int bt_mock_get_scan_results(bt_device_t* devices, int max_count);

// Control mock device connection
esp_err_t bt_mock_connect(const char* addr);
esp_err_t bt_mock_disconnect(void);
bool bt_mock_is_connected(void);

// Mock pairing operations
esp_err_t bt_mock_start_pairing(const char* addr);
bt_pairing_state_t bt_mock_get_pairing_state(void);
bt_pairing_method_t bt_mock_get_pairing_method(void);
esp_err_t bt_mock_send_pin(const char* pin);
esp_err_t bt_mock_confirm_ssp(bool confirm);
bool bt_mock_is_device_paired(const char* addr);
esp_err_t bt_mock_add_paired_device(const bt_device_t* device);
void bt_mock_set_ssp_supported(bool supported);

// Helper for implementing connect-by-name
void bt_mock_set_connect_by_name_hook(const char* name, const char* addr);

// TEST SCENARIO SIMULATION
// These functions help simulate specific test scenarios

// Simulate pairing failures
void bt_mock_simulate_pin_failure(void);
void bt_mock_simulate_pairing_timeout(void);

// Simulate SSP (Secure Simple Pairing) request
esp_err_t bt_mock_simulate_ssp_request(uint32_t passkey);

// Get connected device address
const char* bt_mock_get_connected_addr(void); 
esp_err_t bt_mock_copy_connected_addr(char* addr_buf, size_t buf_size);

// Paired device management functions
esp_err_t bt_mock_unpair_device(const char* addr);
esp_err_t bt_mock_unpair_all_devices(void);
int bt_mock_get_paired_device_count(void);
int bt_mock_get_paired_devices(bt_device_t* devices, int max_count);

// SSP confirmation functions
bool bt_mock_is_ssp_confirm_requested(void);
uint32_t bt_mock_get_ssp_passkey(void);

// PIN code functions
esp_err_t bt_mock_set_default_pin(const char* pin);

#endif // BT_MOCK_DEVICES_H