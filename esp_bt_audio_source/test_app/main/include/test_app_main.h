#pragma once

/**
 * @brief Entry point invoked by the Unity harness to execute all suites.
 */
void app_test_main(void);

/**
 * @brief Run all BT pairing tests
 */
void app_main_bt_pairing_tests(void);

/**
 * @brief Run all BT A2DP tests
 */
void app_main_bt_a2dp_tests(void);

/**
 * @brief Run a specific test group by name
 * 
 * @param test_group The name of the test group to run
 */
void run_test_group(const char* test_group);
