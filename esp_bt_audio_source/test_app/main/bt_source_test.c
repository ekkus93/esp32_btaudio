#include <string.h>
#include "unity.h"
#include "bt_source.h"
#include "esp_log.h"

void bt_deinit(void);
const char* bt_get_address_from_info(const bt_connection_info_t* info);

int bt_get_connection_quality(const bt_connection_info_t* info); // Add this prototype

static const char *TAG = "BT_TEST";

/**
 * @brief Test case for the Bluetooth scan filter functionality
 * 
 * This test verifies that the Bluetooth scan correctly filters devices by type.
 */
void test_bluetooth_scan_filters_by_device_type(void) {
    // 1. Initialize the Bluetooth stack
    esp_err_t ret = bt_init();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // 2. Start a scan with filter type 5
    ret = bt_scan_start_filtered(5);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // 3. Check if the filter has found any matches (this is what we're testing)
    bool has_matches = bt_filter_has_matches(5);
    TEST_ASSERT_TRUE(has_matches);
    
    // 4. Clean up
    bt_scan_stop();
    bt_deinit();
}

/**
 * @brief Test case for connecting to a device by address
 * 
 * This test verifies that we can connect to a device using its address
 * and that the connection info correctly reports the connected address.
 */
void test_connect_to_device_by_address(void) {
    // 1. Initialize the Bluetooth stack
    esp_err_t ret = bt_init();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // 2. Connect to a test device address
    const char* test_addr = "11:22:33:44:55:66";
    ret = bt_connect(test_addr);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // 3. Check connection status
    TEST_ASSERT_TRUE(bt_is_connected());
    
    // 4. Get connection info and verify the address
    bt_connection_info_t info;
    ret = bt_get_connection_info(&info);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // 5. Extract address from the connection info and verify it matches
    const char* connected_addr = bt_get_address_from_info(&info);
    TEST_ASSERT_EQUAL_STRING(test_addr, connected_addr);
    
    // 6. Clean up
    bt_disconnect();
    bt_deinit();
}

/**
 * @brief Test case for getting connection status information
 * 
 * This test verifies that the connection status information 
 * provides the expected data about an active connection.
 */
void test_get_connection_status_information(void) {
    // 1. Initialize the Bluetooth stack
    esp_err_t ret = bt_init();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // 2. Connect to a test device
    const char* test_addr = "11:22:33:44:55:66";
    ret = bt_connect(test_addr);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // 3. Get connection info
    bt_connection_info_t info;
    ret = bt_get_connection_info(&info);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // 4. Verify connection properties (we're checking if connection quality is > 0)
    TEST_ASSERT_TRUE(info.connected);
    TEST_ASSERT_GREATER_THAN(0, bt_get_connection_quality(&info));
    
    // 5. Clean up
    bt_disconnect();
    bt_deinit();
}
