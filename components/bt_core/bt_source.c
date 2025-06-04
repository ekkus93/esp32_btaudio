#include "bt_source.h"
#include "bt_interface.h"
#include "bt_registry.h"

// All API functions delegate to the current implementation

esp_err_t bt_init(void) {
    return bt_get_implementation()->init();
}

esp_err_t bt_scan_start(void) {
    return bt_get_implementation()->scan_start();
}

// ... similar implementations for all other API functions ...

esp_err_t bt_connect(const char* addr) {
    return bt_get_implementation()->connect(addr);
}

esp_err_t bt_start_pairing(const char* addr) {
    return bt_get_implementation()->start_pairing(addr);
}

esp_err_t bt_send_pin_code(const char* pin) {
    return bt_get_implementation()->send_pin_code(pin);
}

bool bt_is_device_paired(const char* addr) {
    return bt_get_implementation()->is_device_paired(addr);
}

// ... and so on for all API functions ...
