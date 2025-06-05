#ifndef _BT_A2DP_TEST_H_
#define _BT_A2DP_TEST_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Test that Bluetooth stack initializes successfully (test #6)
 */
void test_bluetooth_stack_init(void);

/**
 * @brief Test A2DP starts and stops streaming (test #3)
 */
void test_a2dp_streaming(void);

/**
 * @brief Test Bluetooth scan starts successfully (test #1)
 */
void test_bluetooth_scan_start(void);

/**
 * @brief Test Bluetooth connection management (tests #2, #4)
 */
void test_bluetooth_connection(void);

/**
 * @brief Test A2DP remembers paired devices (test #5)
 */
void test_a2dp_paired_devices(void);

/**
 * @brief Run all Bluetooth A2DP tests
 */
void run_bt_a2dp_tests(void);

#ifdef __cplusplus
}
#endif

#endif /* _BT_A2DP_TEST_H_ */
