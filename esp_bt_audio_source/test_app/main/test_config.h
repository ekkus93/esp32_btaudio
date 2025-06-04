#ifndef TEST_CONFIG_H
#define TEST_CONFIG_H

#include "esp_err.h"
#include "bt_source.h"

// The interface all BT implementations must provide
typedef struct {
    void (*reset)(void);
    esp_err_t (*start_pairing)(const char* addr);
    esp_err_t (*send_pin)(const char* pin);
    bt_pairing_state_t (*get_pairing_state)(void);
    bt_pairing_method_t (*get_pairing_method)(void);
    esp_err_t (*confirm_ssp)(bool confirm);
    bool (*is_device_paired)(const char* addr);
} bt_interface_t;

// Function to set up the mock implementation
void setup_mock_bt_implementation(void);

// Function to get the current implementation
bt_interface_t* get_bt_implementation(void);

#endif // TEST_CONFIG_H
