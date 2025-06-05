#ifndef BT_TEST_COMPAT_H
#define BT_TEST_COMPAT_H

#include "bt_source.h"

/**
 * This header provides compatibility mappings between enum values in tests and actual enum values.
 * This allows tests to continue working with expected values while the main codebase uses proper enums.
 */

// Map between test-expected pairing states and actual enum values
#define BT_PAIRING_STATE_NONE            BT_PAIRING_STATE_IDLE
#define BT_PAIRING_STATE_PIN_REQUESTED   BT_PAIRING_STATE_STARTED
#define BT_PAIRING_STATE_SSP_CONFIRM     BT_PAIRING_STATE_SSP_REQUESTED
#define BT_PAIRING_STATE_COMPLETE        BT_PAIRING_STATE_PAIRED

// Map between test-expected pairing methods and actual enum values
#define BT_PAIRING_NONE                  BT_PAIRING_METHOD_NONE
#define BT_PAIRING_PIN                   BT_PAIRING_METHOD_PIN
#define BT_PAIRING_SSP                   BT_PAIRING_METHOD_SSP

// Error codes that tests expect (these match ESP-IDF values)
#define BT_ERROR_INVALID_ARG             ESP_ERR_INVALID_ARG
#define BT_ERROR_NOT_FOUND               ESP_ERR_NOT_FOUND
#define BT_ERROR_INVALID_STATE           ESP_ERR_INVALID_STATE

#endif // BT_TEST_COMPAT_H
