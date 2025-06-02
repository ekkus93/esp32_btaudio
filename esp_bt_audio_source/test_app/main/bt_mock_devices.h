#ifndef BT_MOCK_DEVICES_H
#define BT_MOCK_DEVICES_H

#include <stdlib.h> // Add for size_t definition
#include "esp_err.h"
#include "bt_source.h"

/**
 * Mock device structure
 */
typedef struct {
    uint8_t addr[6];
    char name[32];
    bt_device_type_t type;
    bool paired;
    uint32_t cod;  // Class of Device
} bt_mock_device_t;

/**
 * Mock callback functions
 */
typedef struct {
    void (*device_found)(bt_device_t *device, void *user_data);
    void (*connection_change)(bool connected, bt_device_t *device, esp_err_t status, void *user_data);
    void *user_data;
} bt_mock_callbacks_t;

// Basic mock control
void bt_mock_init(void);
void bt_mock_reset(void);
void bt_mock_register_callbacks(bt_mock_callbacks_t *callbacks);

// Device management functions
esp_err_t bt_mock_add_device(const char* addr, const char* name, 
                       bt_device_type_t type, bool paired);
int bt_mock_get_devices(bt_device_t *devices, int max_count);
int bt_mock_get_paired_devices(bt_device_t *devices, int max_count);
bool bt_mock_is_device_paired(const char* addr);

// Scanning functions
esp_err_t bt_mock_start_scan(void);
esp_err_t bt_mock_stop_scan(void);
bool bt_mock_is_scanning(void);

// Connection functions
esp_err_t bt_mock_connect(const char* addr);
esp_err_t bt_mock_disconnect(void);
bool bt_mock_is_connected(void);

// Streaming functions
esp_err_t bt_mock_start_streaming(void);
esp_err_t bt_mock_stop_streaming(void);
esp_err_t bt_mock_pause_streaming(void);
esp_err_t bt_mock_resume_streaming(void);
bool bt_mock_is_streaming(void);
bool bt_mock_is_paused(void);
bt_streaming_state_t bt_mock_get_streaming_state(void);

// Pairing simulation functions
esp_err_t bt_mock_start_pairing(const char* addr);
esp_err_t bt_mock_send_pin_code(const char* pin);
bt_pairing_state_t bt_mock_get_pairing_state(void);
bt_pairing_method_t bt_mock_get_pairing_method(void);
esp_err_t bt_mock_get_ssp_passkey(char* passkey, size_t size);
bool bt_mock_is_ssp_confirm_requested(void);
esp_err_t bt_mock_ssp_confirm(bool confirm);
esp_err_t bt_mock_set_default_pin(const char* pin);
esp_err_t bt_mock_get_default_pin(char* pin, size_t size);
esp_err_t bt_mock_unpair_device(const char* addr);
esp_err_t bt_mock_unpair_all_devices(void);

// Test control functions
void bt_mock_simulate_pin_failure(void);
void bt_mock_simulate_pairing_timeout(void);
void bt_mock_simulate_ssp_request(uint32_t passkey);
void bt_mock_set_ssp_supported(bool supported);

#endif // BT_MOCK_DEVICES_H
