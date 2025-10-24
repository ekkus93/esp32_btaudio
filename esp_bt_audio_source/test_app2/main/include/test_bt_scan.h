#ifndef TEST_BT_SCAN_H
#define TEST_BT_SCAN_H

#include "unity.h"
#include "bt_source.h"  // Include the real BT source header
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Function declarations for device scanning tests
void test_bt_scan_basic(void);
void test_bt_scan_filtered(void);
void test_bt_scan_get_results(void);
void test_bt_scan_timeout(void);
void test_bt_scan_stop(void);

// Mock functions - these should match signatures in bt_source.h
// Note: In a real test environment, these would be implemented as 
// test stubs or mocks using a framework like cmock

#endif // TEST_BT_SCAN_H
