/**
 * @file bt_a2dp_test.h
 * @brief Header for Bluetooth A2DP tests
 */

#ifndef BT_A2DP_TEST_H
#define BT_A2DP_TEST_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Main test entry points
void run_bt_a2dp_tests(void);

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
 * @brief Test Bluetooth scan discovers devices (test #1)
 */
void test_bluetooth_scan_discovered_devices(void);

/**
 * @brief Test Bluetooth scan filters by type (test #1)
 */
void test_bluetooth_scan_filter_by_type(void);

/**
 * @brief Test basic Bluetooth scanning functionality (test #1)
 */
void test_bluetooth_scanning_basic(void);

/**
 * @brief Test Bluetooth scan device details (test #1)
 */
void test_bluetooth_scan_device_details(void);

/**
 * @brief Test Bluetooth scan timeout handling (test #1)
 */
void test_bluetooth_scan_timeout(void);

/**
 * @brief Test Bluetooth scan stops early (test #1)
 */
void test_bluetooth_scan_stop_early(void);

/**
 * @brief Test connection by device name (test #2)
 */
void test_connect_by_name(void);

/**
 * @brief Test connection failure handling (test #2)
 */
void test_connection_failure_handling(void);

/**
 * @brief Test connection timeout (test #2)
 */
void test_connection_timeout(void);

/**
 * @brief Test connection status information (test #2)
 */
void test_connection_status_info(void);

/**
 * @brief Test automatic reconnection (test #2)
 */
void test_auto_reconnect(void);

/**
 * @brief Test connecting to an A2DP sink (test #2)
 */
void test_connect_to_a2dp_sink(void);

/**
 * @brief Test audio streaming start success (test #3)
 */
void test_audio_streaming_start_success(void);

/**
 * @brief Test audio streaming stop success (test #3)
 */
void test_audio_streaming_stop_success(void);

/**
 * @brief Test that streaming requires an active connection (test #3)
 */
void test_streaming_requires_connection(void);

/**
 * @brief Test pause and resume functionality during streaming (test #3)
 */
void test_streaming_pause_resume(void);

/**
 * @brief Test reporting of streaming state (test #3)
 */
void test_streaming_state_reporting(void);

#ifdef __cplusplus
}
#endif

#endif /* BT_A2DP_TEST_H */
