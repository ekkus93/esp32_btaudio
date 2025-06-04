#ifndef BT_TEST_COMPAT_H
#define BT_TEST_COMPAT_H

// This header ensures the enum values we use match what the tests expect

// Pairing states expected by tests
#define BT_PAIRING_STATE_IDLE 0
#define BT_PAIRING_STATE_PIN_REQUESTED 1
#define BT_PAIRING_STATE_PAIRED 2  
#define BT_PAIRING_STATE_SSP_REQUESTED 3
#define BT_PAIRING_STATE_FAILED 5  
#define BT_PAIRING_STATE_TIMEOUT 6  

// Pairing methods expected by tests
#define BT_PAIRING_NONE 0
#define BT_PAIRING_METHOD_PIN 1
#define BT_PAIRING_METHOD_SSP 2

// Error codes that tests expect
#define BT_ERROR_INVALID_ARG 258
#define BT_ERROR_NOT_FOUND 259
#define BT_ERROR_INVALID_STATE 261

#endif // BT_TEST_COMPAT_H
