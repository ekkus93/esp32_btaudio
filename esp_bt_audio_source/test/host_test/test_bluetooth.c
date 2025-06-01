#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "bt_manager.h"
#include "mock_i2s.h"
#include "esp_bt.h"

#ifdef UNIT_TEST
void bt_manager_force_initialized(bool value);
void bt_manager_debug_print(void);
#endif

// Mock variables
static bool bt_connected_callback_called = false;
static bool bt_disconnected_callback_called = false;
static char bt_connected_mac[18] = {0};
static char bt_connected_name[32] = {0};
static char bt_disconnected_mac[18] = {0};

// Mock callbacks for BT events
static void test_bt_connected_cb(const char* mac, const char* name) {
    bt_connected_callback_called = true;
    strncpy(bt_connected_mac, mac, sizeof(bt_connected_mac) - 1);
    strncpy(bt_connected_name, name, sizeof(bt_connected_name) - 1);
    printf("BT connected callback: %s, %s\n", mac, name);
}

static void test_bt_disconnected_cb(const char* mac) {
    bt_disconnected_callback_called = true;
    strncpy(bt_disconnected_mac, mac, sizeof(bt_disconnected_mac) - 1);
    printf("BT disconnected callback: %s\n", mac);
}

// Test setup and teardown
void setUp(void) {
    // Reset mock state
    bt_connected_callback_called = false;
    bt_disconnected_callback_called = false;
    memset(bt_connected_mac, 0, sizeof(bt_connected_mac));
    memset(bt_connected_name, 0, sizeof(bt_connected_name));
    memset(bt_disconnected_mac, 0, sizeof(bt_disconnected_mac));
    
    // Initialize the BT manager
    bt_manager_init_t config = {
        .device_name = "ESP32_TEST",
        .connected_cb = test_bt_connected_cb,
        .disconnected_cb = test_bt_disconnected_cb
    };
    
    TEST_ASSERT_EQUAL(BT_SUCCESS, bt_manager_init(&config));
}

void tearDown(void) {
    // Clean up BT manager
    TEST_ASSERT_EQUAL(BT_SUCCESS, bt_manager_deinit());
}

// Test basic initialization and deinitialization
void test_bt_init_deinit(void) {
    // Already initialized in setUp()
    TEST_ASSERT_EQUAL(BT_SUCCESS, bt_manager_deinit());
    
    // Initialize again with different config
    bt_manager_init_t config = {
        .device_name = "ESP32_TEST2",
        .connected_cb = test_bt_connected_cb,
        .disconnected_cb = test_bt_disconnected_cb
    };
    
    TEST_ASSERT_EQUAL(BT_SUCCESS, bt_manager_init(&config));
}

// Test device scanning
void test_bt_scanning(void) {
    // Start scan
    TEST_ASSERT_EQUAL(BT_SUCCESS, bt_start_scan());
    
    // Simulate device discovery events
    bt_device_t mock_device1 = {
        .mac = "AA:BB:CC:11:22:33",
        .name = "Test Speaker 1",
        .rssi = -70
    };
    
    bt_device_t mock_device2 = {
        .mac = "DD:EE:FF:44:55:66",
        .name = "Test Speaker 2",
        .rssi = -60
    };
    
    bt_manager_mock_device_found(&mock_device1);
    bt_manager_mock_device_found(&mock_device2);
    
    // Stop scan
    TEST_ASSERT_EQUAL(BT_SUCCESS, bt_stop_scan());
    
    // Check discovered devices
    bt_device_list_t* devices = bt_get_device_list();
    TEST_ASSERT_NOT_NULL(devices);
    TEST_ASSERT_EQUAL(2, devices->count);
    
    TEST_ASSERT_EQUAL_STRING("AA:BB:CC:11:22:33", devices->devices[0].mac);
    TEST_ASSERT_EQUAL_STRING("Test Speaker 1", devices->devices[0].name);
    TEST_ASSERT_EQUAL(-70, devices->devices[0].rssi);
    
    TEST_ASSERT_EQUAL_STRING("DD:EE:FF:44:55:66", devices->devices[1].mac);
    TEST_ASSERT_EQUAL_STRING("Test Speaker 2", devices->devices[1].name);
    TEST_ASSERT_EQUAL(-60, devices->devices[1].rssi);
}

// Test connection to device
void test_bt_connect_disconnect(void) {
    const char* test_mac = "AA:BB:CC:11:22:33";
    const char* test_name = "Test Speaker";
    
    // Start connection
    TEST_ASSERT_EQUAL(BT_SUCCESS, bt_connect(test_mac));
    
    // Simulate connection established
    bt_manager_mock_connection_established(test_mac, test_name);
    
    // Verify callback was called
    TEST_ASSERT_TRUE(bt_connected_callback_called);
    TEST_ASSERT_EQUAL_STRING(test_mac, bt_connected_mac);
    TEST_ASSERT_EQUAL_STRING(test_name, bt_connected_name);
    
    // Disconnect
    TEST_ASSERT_EQUAL(BT_SUCCESS, bt_disconnect());
    
    // Simulate disconnection
    bt_manager_mock_connection_closed(test_mac);
    
    // Verify callback was called
    TEST_ASSERT_TRUE(bt_disconnected_callback_called);
    TEST_ASSERT_EQUAL_STRING(test_mac, bt_disconnected_mac);
}

// Test connection by name
void test_bt_connect_by_name(void) {
    // Start scan
    TEST_ASSERT_EQUAL(BT_SUCCESS, bt_start_scan());
    
    // Simulate device discovery
    bt_device_t mock_device1 = {
        .mac = "AA:BB:CC:11:22:33",
        .name = "Kitchen Speaker",
        .rssi = -70
    };
    
    bt_device_t mock_device2 = {
        .mac = "DD:EE:FF:44:55:66",
        .name = "Living Room Speaker",
        .rssi = -60
    };
    
    bt_manager_mock_device_found(&mock_device1);
    bt_manager_mock_device_found(&mock_device2);
    
    // Stop scan
    TEST_ASSERT_EQUAL(BT_SUCCESS, bt_stop_scan());
    
    // Connect by name
    TEST_ASSERT_EQUAL(BT_SUCCESS, bt_connect_by_name("Living Room Speaker"));
    
    // Simulate connection established
    bt_manager_mock_connection_established("DD:EE:FF:44:55:66", "Living Room Speaker");
    
    // Verify callback was called with correct device
    TEST_ASSERT_TRUE(bt_connected_callback_called);
    TEST_ASSERT_EQUAL_STRING("DD:EE:FF:44:55:66", bt_connected_mac);
    TEST_ASSERT_EQUAL_STRING("Living Room Speaker", bt_connected_name);
}

// Test audio operations
void test_bt_audio_operations(void) {
    printf("TEST: test_bt_audio_operations running\n");
    const char* test_mac = "AA:BB:CC:11:22:33";
    const char* test_name = "Test Speaker";

    // Connect
    TEST_ASSERT_EQUAL(BT_SUCCESS, bt_connect(test_mac));
    bt_manager_mock_connection_established(test_mac, test_name);

#ifdef UNIT_TEST
    bt_manager_force_initialized(true);
    bt_manager_debug_print();
#endif

    printf("TEST: about to call bt_start_audio()\n");
    int ret = bt_start_audio();
    printf("TEST: bt_start_audio() returned %d\n", ret);
    TEST_ASSERT_EQUAL(BT_SUCCESS, ret);
    
    // Simulate audio started
    bt_manager_mock_audio_state_changed(2); // Use integer 2 for STARTED state
    
    // Set volume
    TEST_ASSERT_EQUAL(BT_SUCCESS, bt_set_volume(75));
    
    // Stop audio
    TEST_ASSERT_EQUAL(BT_SUCCESS, bt_stop_audio());
    
    // Simulate audio stopped
    bt_manager_mock_audio_state_changed(1); // Use integer 1 for STOPPED state
    
    // Disconnect
    TEST_ASSERT_EQUAL(BT_SUCCESS, bt_disconnect());
}

// Test pairing operations
void test_bt_pairing(void) {
    const char* test_mac = "BB:CC:DD:11:22:33";
    
    // Start pairing
    TEST_ASSERT_EQUAL(BT_SUCCESS, bt_pair(test_mac));
    
    // Simulate pairing success
    bt_manager_mock_pairing_complete(test_mac, true);
    
    // Test unpair
    TEST_ASSERT_EQUAL(BT_SUCCESS, bt_unpair(test_mac));
    
    // Test set PIN
    TEST_ASSERT_EQUAL(BT_SUCCESS, bt_set_pin("1234"));
}

// Main test runner
int main(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_bt_init_deinit);
    RUN_TEST(test_bt_scanning);
    RUN_TEST(test_bt_connect_disconnect);
    RUN_TEST(test_bt_connect_by_name);
    RUN_TEST(test_bt_audio_operations);
    RUN_TEST(test_bt_pairing);
    
    return UNITY_END();
}
