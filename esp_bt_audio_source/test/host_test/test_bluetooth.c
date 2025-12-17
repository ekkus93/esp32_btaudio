#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "bt_manager.h"
#include "esp_err.h"
#include "mock_i2s.h"
#include "esp_bt.h"

// Test-hook setters from mocks/bt_manager_test_hooks.c
void bt_manager_test_set_force_disconnect_failure(int v);
void bt_manager_test_set_force_start_failure(int v);
void bt_manager_test_set_force_stop_failure(int v);
void bt_manager_test_reset_forces(void);
int bt_manager_test_get_scan_start_count(void);
// Manager wrapper prototypes (not exposed via header)
int bt_manager_disconnect(void);
int bt_manager_start_audio(void);
int bt_manager_stop_audio(void);

// Pairing pending helpers (UNIT_TEST only)
void bt_manager_test_reset_pending(void);
bool bt_manager_test_gap_pin_request(const char* mac);
bool bt_manager_test_gap_ssp_confirm(const char* mac, uint32_t passkey);
void bt_manager_test_gap_auth_complete(const char* mac, bool success);
bool bt_pairing_get_pending_request(bt_pairing_request_info_t* info);

// Autostart helper (UNIT_TEST only)
bool bt_manager_test_autostart_on_connect(void);
void bt_manager_set_autostart_enabled(bool enable);
bool bt_manager_is_autostart_enabled(void);

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
    
    TEST_ASSERT_EQUAL(ESP_OK, bt_manager_init(&config));
}

void tearDown(void) {
    // Clean up BT manager
    TEST_ASSERT_EQUAL(ESP_OK, bt_manager_deinit());
}

// Test basic initialization and deinitialization
void test_bt_init_deinit(void) {
    // Already initialized in setUp()
    TEST_ASSERT_EQUAL(ESP_OK, bt_manager_deinit());
    
    // Initialize again with different config
    bt_manager_init_t config = {
        .device_name = "ESP32_TEST2",
        .connected_cb = test_bt_connected_cb,
        .disconnected_cb = test_bt_disconnected_cb
    };
    
    TEST_ASSERT_EQUAL(ESP_OK, bt_manager_init(&config));
}

// Test device scanning
void test_bt_scanning(void) {
    // Start scan
    TEST_ASSERT_EQUAL(ESP_OK, bt_start_scan());
    
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
    TEST_ASSERT_EQUAL(ESP_OK, bt_stop_scan());
    
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
    TEST_ASSERT_EQUAL(ESP_OK, bt_connect(test_mac));
    
    // Simulate connection established
    bt_manager_mock_connection_established(test_mac, test_name);
    
    // Verify callback was called
    TEST_ASSERT_TRUE(bt_connected_callback_called);
    TEST_ASSERT_EQUAL_STRING(test_mac, bt_connected_mac);
    TEST_ASSERT_EQUAL_STRING(test_name, bt_connected_name);
    
    // Disconnect
    TEST_ASSERT_EQUAL(ESP_OK, bt_disconnect());
    
    // Simulate disconnection
    bt_manager_mock_connection_closed(test_mac);
    
    // Verify callback was called
    TEST_ASSERT_TRUE(bt_disconnected_callback_called);
    TEST_ASSERT_EQUAL_STRING(test_mac, bt_disconnected_mac);
}

// Test connection by name
void test_bt_connect_by_name(void) {
    // Start scan
    TEST_ASSERT_EQUAL(ESP_OK, bt_start_scan());
    
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
    TEST_ASSERT_EQUAL(ESP_OK, bt_stop_scan());
    
    // Connect by name
    TEST_ASSERT_EQUAL(ESP_OK, bt_connect_by_name("Living Room Speaker"));
    
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

    // Test starting audio without connection - should fail
    int ret = bt_start_audio();
    printf("TEST: bt_start_audio() without connection returned %d (expected failure)\n", ret);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ret);
    
        // Test stopping audio when not playing - should succeed
    ret = bt_stop_audio();
    printf("TEST: bt_stop_audio() when not playing returned %d (expected success)\n", ret);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    // Connect
    TEST_ASSERT_EQUAL(ESP_OK, bt_connect(test_mac));
    bt_manager_mock_connection_established(test_mac, test_name);

#ifdef UNIT_TEST
    bt_manager_force_initialized(true);
    bt_manager_debug_print();
#endif

    // Test starting audio with connection - should succeed
    printf("TEST: about to call bt_start_audio()\n");
    ret = bt_start_audio();
    printf("TEST: bt_start_audio() returned %d\n", ret);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Simulate audio started
    bt_manager_mock_audio_state_changed(2); // Use integer 2 for STARTED state
    
    // Test starting audio again - should succeed (already playing)
    ret = bt_start_audio();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Set volume
    TEST_ASSERT_EQUAL(ESP_OK, bt_set_volume(75));
    
    // Stop audio
    TEST_ASSERT_EQUAL(ESP_OK, bt_stop_audio());
    
    // Simulate audio stopped
    bt_manager_mock_audio_state_changed(1); // Use integer 1 for STOPPED state
    
    // Test stopping audio again - should succeed (already stopped)
    ret = bt_stop_audio();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Disconnect
    TEST_ASSERT_EQUAL(ESP_OK, bt_disconnect());
}

// Ensure scan hook fires once per start, respects idempotence, and restarts after stop
void test_bt_scan_hook_counts(void) {
    bt_manager_test_reset_forces();

    TEST_ASSERT_EQUAL(ESP_OK, bt_start_scan());
    TEST_ASSERT_EQUAL(1, bt_manager_test_get_scan_start_count());

    // Starting again while already scanning should be idempotent
    TEST_ASSERT_EQUAL(ESP_OK, bt_start_scan());
    TEST_ASSERT_EQUAL(1, bt_manager_test_get_scan_start_count());

    TEST_ASSERT_EQUAL(ESP_OK, bt_stop_scan());
    TEST_ASSERT_EQUAL(ESP_OK, bt_start_scan());
    TEST_ASSERT_EQUAL(2, bt_manager_test_get_scan_start_count());
}

// Verify start_scan fails when manager not initialized
void test_bt_scan_requires_init(void) {
    bt_manager_test_reset_forces();
    bt_manager_force_initialized(false);
    TEST_ASSERT_EQUAL(ESP_FAIL, bt_start_scan());
    TEST_ASSERT_EQUAL(0, bt_manager_test_get_scan_start_count());

    // Restore for other tests
    bt_manager_force_initialized(true);
}

// Verify disconnect wrapper surfaces forced failure and leaves connection intact,
// then succeeds once the hook is cleared.
void test_bt_disconnect_failure_then_success(void) {
    const char* mac = "11:22:33:44:55:66";
    const char* name = "Retry Speaker";

    TEST_ASSERT_EQUAL(ESP_OK, bt_connect(mac));
    bt_manager_mock_connection_established(mac, name);
    TEST_ASSERT_EQUAL(1, bt_manager_is_connected());

    bt_manager_test_set_force_disconnect_failure(1);
    TEST_ASSERT_EQUAL(-1, bt_manager_disconnect());
    TEST_ASSERT_EQUAL(1, bt_manager_is_connected());

    bt_manager_test_set_force_disconnect_failure(0);
    TEST_ASSERT_EQUAL(0, bt_manager_disconnect());
    TEST_ASSERT_EQUAL(0, bt_manager_is_connected());
}

// Exercise start/stop wrappers under forced failure hooks and ensure recovery works.
void test_bt_start_stop_failure_recovery(void) {
    const char* mac = "22:33:44:55:66:77";
    const char* name = "A2DP Sink";

    TEST_ASSERT_EQUAL(ESP_OK, bt_connect(mac));
    bt_manager_mock_connection_established(mac, name);

    bt_manager_test_set_force_start_failure(1);
    TEST_ASSERT_EQUAL(-1, bt_manager_start_audio());
    // Not playing, so stop should still be a no-op success
    TEST_ASSERT_EQUAL(ESP_OK, bt_stop_audio());

    bt_manager_test_set_force_start_failure(0);
    TEST_ASSERT_EQUAL(0, bt_manager_start_audio());

    // Now simulate stop failure while playing
    bt_manager_test_set_force_stop_failure(1);
    TEST_ASSERT_EQUAL(-1, bt_manager_stop_audio());

    bt_manager_test_set_force_stop_failure(0);
    TEST_ASSERT_EQUAL(0, bt_manager_stop_audio());

    // Cleanly disconnect for good measure
    TEST_ASSERT_EQUAL(0, bt_manager_disconnect());
}

// Test pairing operations
void test_bt_pairing(void) {
    const char* test_mac = "BB:CC:DD:11:22:33";
    
    // Start pairing
    TEST_ASSERT_EQUAL(ESP_OK, bt_pair(test_mac));
    
    // Simulate pairing success
    bt_manager_mock_pairing_complete(test_mac, true);
    
    // Test unpair
    TEST_ASSERT_EQUAL(ESP_OK, bt_unpair(test_mac));
    
    // Test set PIN
    TEST_ASSERT_EQUAL(ESP_OK, bt_set_pin("1234"));
}

// Ensure discovered devices are ignored when not scanning and after stop
void test_bt_scan_ignores_when_not_scanning(void) {
    bt_device_t mock_device = {
        .mac = "01:02:03:04:05:06",
        .name = "Ignored",
        .rssi = -30
    };

    /* Ensure we are not marked as scanning from prior tests. */
    TEST_ASSERT_EQUAL(ESP_OK, bt_stop_scan());

    int baseline = bt_get_device_list()->count;

    bt_manager_mock_device_found(&mock_device);
    TEST_ASSERT_EQUAL(baseline, bt_get_device_list()->count);

    TEST_ASSERT_EQUAL(ESP_OK, bt_start_scan());
    bt_manager_mock_device_found(&mock_device);
    TEST_ASSERT_EQUAL(baseline + 1, bt_get_device_list()->count);

    TEST_ASSERT_EQUAL(ESP_OK, bt_stop_scan());
    bt_manager_mock_device_found(&mock_device);
    TEST_ASSERT_EQUAL(baseline + 1, bt_get_device_list()->count);
}

// Verify pairing pending helpers handle out-of-order SSP/PIN and clear on auth complete
void test_bt_pairing_pending_out_of_order(void) {
    bt_pairing_request_info_t info;
    bt_manager_test_reset_pending();

    TEST_ASSERT_TRUE(bt_manager_test_gap_ssp_confirm("AA:BB:CC:11:22:33", 123456));
    TEST_ASSERT_TRUE(bt_pairing_get_pending_request(&info));
    TEST_ASSERT_FALSE(info.pin_request_pending);
    TEST_ASSERT_TRUE(info.ssp_confirm_pending);
    TEST_ASSERT_EQUAL_UINT32(123456, info.passkey);
    TEST_ASSERT_EQUAL_STRING("AA:BB:CC:11:22:33", info.mac);

    TEST_ASSERT_TRUE(bt_manager_test_gap_pin_request("00:11:22:33:44:55"));
    TEST_ASSERT_TRUE(bt_pairing_get_pending_request(&info));
    TEST_ASSERT_TRUE(info.pin_request_pending);
    TEST_ASSERT_FALSE(info.ssp_confirm_pending);
    TEST_ASSERT_EQUAL_UINT32(0, info.passkey);
    TEST_ASSERT_EQUAL_STRING("00:11:22:33:44:55", info.mac);

    bt_manager_test_gap_auth_complete("00:11:22:33:44:55", true);
    TEST_ASSERT_FALSE(bt_pairing_get_pending_request(&info));
}

// Autostart should not trigger when audio already playing and should honor disable flag
void test_bt_autostart_guard_when_playing(void) {
    bt_manager_set_autostart_enabled(true);
    bt_manager_mock_connection_established("10:20:30:40:50:60", "AutoStartSink");

    TEST_ASSERT_TRUE(bt_manager_test_autostart_on_connect());
    TEST_ASSERT_TRUE(bt_manager_is_autostart_enabled());
    TEST_ASSERT_EQUAL(1, bt_manager_is_connected());

    bt_manager_mock_audio_state_changed(2);
    TEST_ASSERT_FALSE(bt_manager_test_autostart_on_connect());

    bt_manager_set_autostart_enabled(false);
    bt_manager_mock_audio_state_changed(1);
    TEST_ASSERT_FALSE(bt_manager_test_autostart_on_connect());
}

// Main test runner
int main(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_bt_init_deinit);
    RUN_TEST(test_bt_scanning);
    RUN_TEST(test_bt_connect_disconnect);
    RUN_TEST(test_bt_connect_by_name);
    RUN_TEST(test_bt_audio_operations);
    RUN_TEST(test_bt_scan_hook_counts);
    RUN_TEST(test_bt_scan_requires_init);
    RUN_TEST(test_bt_pairing);
    RUN_TEST(test_bt_scan_ignores_when_not_scanning);
    RUN_TEST(test_bt_pairing_pending_out_of_order);
    RUN_TEST(test_bt_autostart_guard_when_playing);
    RUN_TEST(test_bt_disconnect_failure_then_success);
    RUN_TEST(test_bt_start_stop_failure_recovery);
    
    return UNITY_END();
}
