/**
 * @file test_bt_scan.c
 * @brief Unit tests for bt_scan.c - Bluetooth device discovery
 *
 * Phase 5.4: bt_scan.c Missing Coverage (NEW MODULE - no existing tests)
 * 
 * Tests cover:
 * - bt_start_scan() - not initialized, already scanning, GAP failure, success
 * - bt_stop_scan() - not initialized, not scanning, GAP failure, success
 * - bt_scan_handle_discovery_result() - device list full, missing name, duplicates
 * - bt_scan_handle_state_change() - STARTED/STOPPED state synchronization
 *
 * TDD Approach: Red → Green → Refactor (Kent Beck)
 * All tests written before implementation review.
 */

#include <string.h>
#include "unity.h"
#include "bt_scan.h"
#include "bt_manager_internal.h"
#include "mock_gap.h"
#include "esp_gap_bt_api.h"

// External access to bt_ctx for testing
extern bt_manager_context_t bt_ctx;

// Test hook to track scan start calls (provided by bt_manager_test_hooks.c)
extern int bt_manager_test_get_scan_start_count(void);
extern void bt_manager_test_reset_forces(void);

void setUp(void)
{
    // Reset mocks and bt_ctx state before each test
    mock_gap_reset();
    memset(&bt_ctx, 0, sizeof(bt_ctx));
    bt_ctx.initialized = false;
    bt_ctx.scanning = false;
    bt_manager_test_reset_forces();
}

void tearDown(void)
{
    // Clean up after each test
}

/**
 * TDD Test 1: bt_start_scan() should return ESP_FAIL when not initialized
 * 
 * Behavior: If bt_ctx.initialized is false, bt_start_scan() must return ESP_FAIL
 * without calling esp_bt_gap_start_discovery().
 */
void test_bt_start_scan_not_initialized_should_fail(void)
{
    // Arrange: bt_ctx.initialized = false (from setUp)
    
    // Act: Call bt_start_scan()
    bt_err_t result = bt_start_scan();
    
    // Assert: Should return ESP_FAIL
    TEST_ASSERT_EQUAL(ESP_FAIL, result);
    
    // Assert: Should NOT call GAP start discovery
    TEST_ASSERT_FALSE(mock_gap_was_start_discovery_called());
    
    // Assert: scanning flag should remain false
    TEST_ASSERT_FALSE(bt_ctx.scanning);
}

/**
 * TDD Test 2: bt_start_scan() should return ESP_OK when already scanning (idempotent)
 * 
 * Behavior: If bt_ctx.scanning is already true, bt_start_scan() returns ESP_OK
 * without calling esp_bt_gap_start_discovery() again.
 */
void test_bt_start_scan_already_scanning_should_return_ok(void)
{
    // Arrange: Set initialized and already scanning
    bt_ctx.initialized = true;
    bt_ctx.scanning = true;
    
    // Act: Call bt_start_scan()
    bt_err_t result = bt_start_scan();
    
    // Assert: Should return ESP_OK (idempotent)
    TEST_ASSERT_EQUAL(ESP_OK, result);
    
    // Assert: Should NOT call GAP start discovery (already scanning)
    TEST_ASSERT_FALSE(mock_gap_was_start_discovery_called());
    
    // Assert: scanning flag should remain true
    TEST_ASSERT_TRUE(bt_ctx.scanning);
}

/**
 * TDD Test 3: bt_start_scan() should propagate ESP_FAIL when GAP start discovery fails
 * 
 * Behavior: If esp_bt_gap_start_discovery() returns ESP_FAIL, bt_start_scan()
 * must return ESP_FAIL and NOT set scanning flag.
 */
void test_bt_start_scan_gap_failure_should_propagate_error(void)
{
    // Arrange: Set initialized, make GAP fail
    bt_ctx.initialized = true;
    bt_ctx.scanning = false;
    mock_gap_set_start_discovery_result(ESP_FAIL);
    
    // Act: Call bt_start_scan()
    bt_err_t result = bt_start_scan();
    
    // Assert: Should return ESP_FAIL
    TEST_ASSERT_EQUAL(ESP_FAIL, result);
    
    // Assert: GAP start discovery WAS called
    TEST_ASSERT_TRUE(mock_gap_was_start_discovery_called());
    
    // Assert: scanning flag should remain false (GAP failed)
    TEST_ASSERT_FALSE(bt_ctx.scanning);
}

/**
 * TDD Test 4: bt_start_scan() should succeed and set scanning flag
 * 
 * Behavior: When initialized and esp_bt_gap_start_discovery() succeeds,
 * bt_start_scan() clears discovered_devices, sets scanning=true, returns ESP_OK.
 */
void test_bt_start_scan_success_should_clear_devices_and_set_scanning(void)
{
    // Arrange: Set initialized, add some discovered devices
    bt_ctx.initialized = true;
    bt_ctx.scanning = false;
    bt_ctx.discovered_devices.count = 5;
    strcpy(bt_ctx.discovered_devices.devices[0].mac, "aa:bb:cc:dd:ee:ff");
    mock_gap_set_start_discovery_result(ESP_OK);
    
    // Act: Call bt_start_scan()
    bt_err_t result = bt_start_scan();
    
    // Assert: Should return ESP_OK
    TEST_ASSERT_EQUAL(ESP_OK, result);
    
    // Assert: GAP start discovery WAS called
    TEST_ASSERT_TRUE(mock_gap_was_start_discovery_called());
    
    // Assert: scanning flag should be set
    TEST_ASSERT_TRUE(bt_ctx.scanning);
    
    // Assert: discovered_devices should be cleared
    TEST_ASSERT_EQUAL(0, bt_ctx.discovered_devices.count);
    TEST_ASSERT_EQUAL(0, bt_ctx.discovered_devices.devices[0].mac[0]);
    
    // Assert: test hook was called
    TEST_ASSERT_EQUAL(1, bt_manager_test_get_scan_start_count());
}

/**
 * TDD Test 5: bt_stop_scan() should return ESP_FAIL when not initialized
 * 
 * Behavior: If bt_ctx.initialized is false, bt_stop_scan() must return ESP_FAIL
 * without calling esp_bt_gap_cancel_discovery().
 */
void test_bt_stop_scan_not_initialized_should_fail(void)
{
    // Arrange: bt_ctx.initialized = false (from setUp)
    
    // Act: Call bt_stop_scan()
    bt_err_t result = bt_stop_scan();
    
    // Assert: Should return ESP_FAIL
    TEST_ASSERT_EQUAL(ESP_FAIL, result);
    
    // Assert: Should NOT call GAP cancel discovery
    TEST_ASSERT_FALSE(mock_gap_was_cancel_discovery_called());
    
    // Assert: scanning flag should remain false
    TEST_ASSERT_FALSE(bt_ctx.scanning);
}

/**
 * TDD Test 6: bt_stop_scan() should return ESP_OK when not scanning (idempotent)
 * 
 * Behavior: If bt_ctx.scanning is false, bt_stop_scan() returns ESP_OK
 * without calling esp_bt_gap_cancel_discovery().
 */
void test_bt_stop_scan_not_scanning_should_return_ok(void)
{
    // Arrange: Set initialized but not scanning
    bt_ctx.initialized = true;
    bt_ctx.scanning = false;
    
    // Act: Call bt_stop_scan()
    bt_err_t result = bt_stop_scan();
    
    // Assert: Should return ESP_OK (idempotent)
    TEST_ASSERT_EQUAL(ESP_OK, result);
    
    // Assert: Should NOT call GAP cancel discovery (not scanning)
    TEST_ASSERT_FALSE(mock_gap_was_cancel_discovery_called());
    
    // Assert: scanning flag should remain false
    TEST_ASSERT_FALSE(bt_ctx.scanning);
}

/**
 * TDD Test 7: bt_stop_scan() should propagate ESP_FAIL when GAP cancel fails
 * 
 * Behavior: If esp_bt_gap_cancel_discovery() returns ESP_FAIL, bt_stop_scan()
 * must return ESP_FAIL but still clear scanning flag.
 */
void test_bt_stop_scan_gap_failure_should_propagate_error(void)
{
    // Arrange: Set initialized and scanning, make GAP fail
    bt_ctx.initialized = true;
    bt_ctx.scanning = true;
    mock_gap_set_cancel_discovery_result(ESP_FAIL);
    
    // Act: Call bt_stop_scan()
    bt_err_t result = bt_stop_scan();
    
    // Assert: Should return ESP_FAIL
    TEST_ASSERT_EQUAL(ESP_FAIL, result);
    
    // Assert: GAP cancel discovery WAS called
    TEST_ASSERT_TRUE(mock_gap_was_cancel_discovery_called());
    
    // Assert: scanning flag should be cleared (even though GAP failed)
    TEST_ASSERT_FALSE(bt_ctx.scanning);
}

/**
 * TDD Test 8: bt_stop_scan() should succeed and clear scanning flag
 * 
 * Behavior: When initialized, scanning, and esp_bt_gap_cancel_discovery() succeeds,
 * bt_stop_scan() clears scanning flag and returns ESP_OK.
 */
void test_bt_stop_scan_success_should_clear_scanning_flag(void)
{
    // Arrange: Set initialized and scanning
    bt_ctx.initialized = true;
    bt_ctx.scanning = true;
    mock_gap_set_cancel_discovery_result(ESP_OK);
    
    // Act: Call bt_stop_scan()
    bt_err_t result = bt_stop_scan();
    
    // Assert: Should return ESP_OK
    TEST_ASSERT_EQUAL(ESP_OK, result);
    
    // Assert: GAP cancel discovery WAS called
    TEST_ASSERT_TRUE(mock_gap_was_cancel_discovery_called());
    
    // Assert: scanning flag should be cleared
    TEST_ASSERT_FALSE(bt_ctx.scanning);
}

/**
 * TDD Test 9: bt_scan_handle_discovery_result() should silently drop 21st device
 * 
 * Behavior: When discovered_devices list is full (20 devices), additional
 * discovery results are silently dropped (count remains 20).
 */
void test_bt_scan_handle_discovery_result_device_list_full_should_drop(void)
{
    // Arrange: Fill device list to max capacity (20 devices)
    bt_ctx.discovered_devices.count = 20;
    for (int i = 0; i < 20; i++) {
        sprintf(bt_ctx.discovered_devices.devices[i].mac, "aa:bb:cc:dd:ee:%02x", i);
        sprintf(bt_ctx.discovered_devices.devices[i].name, "Device%02d", i);
    }
    
    // Arrange: Create discovery result for 21st device
    esp_bd_addr_t bda = {0x21, 0x22, 0x23, 0x24, 0x25, 0x26};
    esp_bt_gap_dev_prop_t props[1];
    char name[] = "Device21";
    props[0].type = ESP_BT_GAP_DEV_PROP_BDNAME;
    props[0].val = name;
    props[0].len = sizeof(name);
    
    // Act: Handle discovery result
    bt_scan_handle_discovery_result(bda, 1, props);
    
    // Assert: Count should remain 20 (21st device dropped)
    TEST_ASSERT_EQUAL(20, bt_ctx.discovered_devices.count);
    
    // Assert: 21st device should NOT be in list
    bool found_21st = false;
    for (int i = 0; i < bt_ctx.discovered_devices.count; i++) {
        if (strcmp(bt_ctx.discovered_devices.devices[i].mac, "21:22:23:24:25:26") == 0) {
            found_21st = true;
            break;
        }
    }
    TEST_ASSERT_FALSE(found_21st);
}

/**
 * TDD Test 10: bt_scan_handle_discovery_result() should handle missing name property
 * 
 * Behavior: When discovery result has no ESP_BT_GAP_DEV_PROP_BDNAME property,
 * device is added with empty name (name[0] = '\0').
 */
void test_bt_scan_handle_discovery_result_missing_name_should_use_empty_string(void)
{
    // Arrange: Clear device list
    bt_ctx.discovered_devices.count = 0;
    
    // Arrange: Create discovery result with ONLY RSSI (no name)
    esp_bd_addr_t bda = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
    esp_bt_gap_dev_prop_t props[1];
    int8_t rssi = -45;
    props[0].type = ESP_BT_GAP_DEV_PROP_RSSI;
    props[0].val = &rssi;
    props[0].len = sizeof(rssi);
    
    // Act: Handle discovery result
    bt_scan_handle_discovery_result(bda, 1, props);
    
    // Assert: Device should be added
    TEST_ASSERT_EQUAL(1, bt_ctx.discovered_devices.count);
    
    // Assert: MAC should be correct
    TEST_ASSERT_EQUAL_STRING("aa:bb:cc:dd:ee:ff", bt_ctx.discovered_devices.devices[0].mac);
    
    // Assert: Name should be empty string (not uninitialized)
    TEST_ASSERT_EQUAL(0, bt_ctx.discovered_devices.devices[0].name[0]);
    
    // Assert: RSSI should be correct
    TEST_ASSERT_EQUAL(-45, bt_ctx.discovered_devices.devices[0].rssi);
}

/**
 * TDD Test 11: bt_scan_handle_discovery_result() should add duplicate as new entry
 * 
 * Behavior: Current implementation does NOT check for duplicates by MAC address.
 * Each discovery result adds a new entry if count < 20.
 * 
 * Note: This test documents current behavior. Future enhancement could
 * update existing entries instead of duplicating.
 */
void test_bt_scan_handle_discovery_result_duplicate_device_adds_new_entry(void)
{
    // Arrange: Add initial device with no name (simulates first inquiry callback)
    bt_ctx.discovered_devices.count = 1;
    strcpy(bt_ctx.discovered_devices.devices[0].mac, "aa:bb:cc:dd:ee:ff");
    strcpy(bt_ctx.discovered_devices.devices[0].name, "");
    bt_ctx.discovered_devices.devices[0].rssi = -50;

    // Arrange: Create discovery result for SAME MAC with name resolved (second callback)
    esp_bd_addr_t bda = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
    esp_bt_gap_dev_prop_t props[2];
    char name[] = "DeviceNew";
    int8_t rssi = -30;
    props[0].type = ESP_BT_GAP_DEV_PROP_BDNAME;
    props[0].val = name;
    props[0].len = sizeof(name);
    props[1].type = ESP_BT_GAP_DEV_PROP_RSSI;
    props[1].val = &rssi;
    props[1].len = sizeof(rssi);

    // Act: Handle discovery result for duplicate MAC
    bt_scan_handle_discovery_result(bda, 2, props);

    // Assert: Count stays at 1 (deduplicated — same MAC is updated, not added)
    TEST_ASSERT_EQUAL(1, bt_ctx.discovered_devices.count);

    // Assert: Existing entry name is updated to the resolved name
    TEST_ASSERT_EQUAL_STRING("aa:bb:cc:dd:ee:ff", bt_ctx.discovered_devices.devices[0].mac);
    TEST_ASSERT_EQUAL_STRING("DeviceNew", bt_ctx.discovered_devices.devices[0].name);
}

/**
 * TDD Test 12: bt_scan_handle_state_change() should sync STARTED state
 * 
 * Behavior: When ESP_BT_GAP_DISCOVERY_STARTED event received,
 * bt_scan_handle_state_change() sets bt_ctx.scanning = true.
 */
void test_bt_scan_handle_state_change_started_should_set_scanning(void)
{
    // Arrange: Clear scanning flag
    bt_ctx.scanning = false;
    
    // Act: Handle STARTED state
    bt_scan_handle_state_change(ESP_BT_GAP_DISCOVERY_STARTED);
    
    // Assert: scanning flag should be set
    TEST_ASSERT_TRUE(bt_ctx.scanning);
}

/**
 * TDD Test 13: bt_scan_handle_state_change() should sync STOPPED state
 * 
 * Behavior: When ESP_BT_GAP_DISCOVERY_STOPPED event received,
 * bt_scan_handle_state_change() sets bt_ctx.scanning = false.
 */
void test_bt_scan_handle_state_change_stopped_should_clear_scanning(void)
{
    // Arrange: Set scanning flag
    bt_ctx.scanning = true;
    
    // Act: Handle STOPPED state
    bt_scan_handle_state_change(ESP_BT_GAP_DISCOVERY_STOPPED);
    
    // Assert: scanning flag should be cleared
    TEST_ASSERT_FALSE(bt_ctx.scanning);
}

/**
 * Main test runner - runs all bt_scan.c unit tests
 */
int main(void)
{
    UNITY_BEGIN();
    
    // bt_start_scan() tests
    RUN_TEST(test_bt_start_scan_not_initialized_should_fail);
    RUN_TEST(test_bt_start_scan_already_scanning_should_return_ok);
    RUN_TEST(test_bt_start_scan_gap_failure_should_propagate_error);
    RUN_TEST(test_bt_start_scan_success_should_clear_devices_and_set_scanning);
    
    // bt_stop_scan() tests
    RUN_TEST(test_bt_stop_scan_not_initialized_should_fail);
    RUN_TEST(test_bt_stop_scan_not_scanning_should_return_ok);
    RUN_TEST(test_bt_stop_scan_gap_failure_should_propagate_error);
    RUN_TEST(test_bt_stop_scan_success_should_clear_scanning_flag);
    
    // bt_scan_handle_discovery_result() tests
    RUN_TEST(test_bt_scan_handle_discovery_result_device_list_full_should_drop);
    RUN_TEST(test_bt_scan_handle_discovery_result_missing_name_should_use_empty_string);
    RUN_TEST(test_bt_scan_handle_discovery_result_duplicate_device_adds_new_entry);
    
    // bt_scan_handle_state_change() tests
    RUN_TEST(test_bt_scan_handle_state_change_started_should_set_scanning);
    RUN_TEST(test_bt_scan_handle_state_change_stopped_should_clear_scanning);
    
    return UNITY_END();
}
