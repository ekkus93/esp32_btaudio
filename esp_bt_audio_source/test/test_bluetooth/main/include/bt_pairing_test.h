/**
 * @file bt_pairing_test.h
 * @brief Header for Bluetooth pairing tests
 */

#ifndef BT_PAIRING_TEST_H
#define BT_PAIRING_TEST_H

#ifdef __cplusplus
extern "C" {
#endif

// Test function declarations
void test_pin_pairing_success(void);
void test_pin_pairing_failure(void);
void test_ssp_confirmation_request(void);
void test_ssp_confirmation_accepted(void);
void test_ssp_confirmation_rejected(void);
void test_ssp_fallback_to_pin(void);
void test_pin_pairing_timeout(void);

// Main entry point for pairing tests
void run_bt_pairing_tests(void);

#ifdef __cplusplus
}
#endif

#endif /* BT_PAIRING_TEST_H */
