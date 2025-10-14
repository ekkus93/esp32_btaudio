// Canonical Bluetooth API shared between production and test code
#ifndef BT_API_H
#define BT_API_H

#include <stdint.h>
#include "esp_err.h"

// Canonical return type for BT operations used by the tests and mocks.
// Tests expect esp_err_t values (ESP_OK == 0, ESP_FAIL != 0).
typedef esp_err_t bt_err_t;

// If the production bt_manager uses a different enum, add conversion
// helpers here or update the production code to return esp_err_t.

// Note: Do not declare bt_disconnect prototype here to avoid conflicting
// declarations with the production bt_manager API (bt_manager.h). This
// header provides the canonical bt_err_t typedef and conversion helpers.

#endif // BT_API_H
