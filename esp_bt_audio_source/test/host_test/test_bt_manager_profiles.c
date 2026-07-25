#include "unity.h"
#include "esp_err.h"
#include "esp_avrc_api.h"
#include "mock_a2dp.h"
#include "bt_manager.h"
#include "bt_manager_internal.h"

#include <string.h>

// Production under test (test-only hook defined in bt_manager.c when UNIT_TEST)
extern esp_err_t bt_manager_test_init_profiles(void);
extern esp_err_t bt_manager_test_init_mutex(void);

// Mock helpers
void mock_avrc_reset(void);

static const char* s_call_log[8];
static int s_call_log_len;

// Hook consumed by mock_a2dp.c and mock_avrc.c to record call order
void mock_bt_call_log(const char* tag)
{
    if (s_call_log_len < (int)(sizeof(s_call_log) / sizeof(s_call_log[0]))) {
        s_call_log[s_call_log_len++] = tag;
    }
}

void setUp(void)
{
    s_call_log_len = 0;
    for (size_t i = 0; i < sizeof(s_call_log) / sizeof(s_call_log[0]); ++i) {
        s_call_log[i] = NULL;
    }
    mock_a2dp_reset();
    mock_avrc_reset();
    memset(&bt_ctx, 0, sizeof(bt_ctx));
    /* bt_pair()/bt_connect()/bt_get_*_snapshot() all take bt_ctx_lock(),
     * which fails with ESP_ERR_INVALID_STATE until s_bt_ctx_mutex exists --
     * this test file never calls bt_manager_init() (by convention, see
     * bt_manager.c's "Test hooks for host-mode unit tests" comment), so
     * create it via the dedicated test hook instead. */
    TEST_ASSERT_EQUAL_INT(ESP_OK, bt_manager_test_init_mutex());
}

void tearDown(void)
{
}

static void assert_ordered_calls(void)
{
    static const char* expected[] = {
        "esp_avrc_ct_init",
        "esp_avrc_ct_register_callback",
        "esp_a2d_source_init",
        "esp_a2d_register_callback",
        "esp_a2d_source_register_data_callback",
    };

    TEST_ASSERT_EQUAL_INT((int)(sizeof(expected) / sizeof(expected[0])), s_call_log_len);
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i) {
        TEST_ASSERT_NOT_NULL_MESSAGE(s_call_log[i], "missing call entry");
        TEST_ASSERT_EQUAL_STRING(expected[i], s_call_log[i]);
    }
}

void test_bt_manager_profiles_init_order(void)
{
    esp_err_t ret = bt_manager_test_init_profiles();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    assert_ordered_calls();
}

/* ================= BT-3 (docs/UNIT_TESTS2_TODO.md): bt_manager.c remaining gaps ================= */

/* ---- bt_manager_set_name ---- */

void test_bt_manager_set_name_returns_zero_on_host(void)
{
    /* Host (non-ESP_PLATFORM) branch is an unconditional no-op returning 0,
     * regardless of bt_ctx state or the name given. */
    TEST_ASSERT_EQUAL_INT(0, bt_manager_set_name("any-name"));
    TEST_ASSERT_EQUAL_INT(0, bt_manager_set_name(NULL));
}

/* ---- bt_manager_pair / bt_manager_connect (thin 0/-1 wrappers over bt_pair/bt_connect) ---- */

void test_bt_manager_pair_fails_when_not_initialized(void)
{
    bt_ctx.initialized = false;
    TEST_ASSERT_EQUAL_INT(-1, bt_manager_pair("AA:BB:CC:DD:EE:FF"));
}

void test_bt_manager_pair_succeeds_when_initialized_with_valid_mac(void)
{
    bt_ctx.initialized = true;
    TEST_ASSERT_EQUAL_INT(0, bt_manager_pair("AA:BB:CC:DD:EE:FF"));
}

void test_bt_manager_pair_fails_on_malformed_mac(void)
{
    bt_ctx.initialized = true;
    TEST_ASSERT_EQUAL_INT(-1, bt_manager_pair("not-a-mac"));
}

void test_bt_manager_connect_fails_when_not_initialized(void)
{
    bt_ctx.initialized = false;
    TEST_ASSERT_EQUAL_INT(-1, bt_manager_connect("AA:BB:CC:DD:EE:FF"));
}

void test_bt_manager_connect_succeeds_when_initialized(void)
{
    bt_ctx.initialized = true;
    bt_ctx.connected = false;
    TEST_ASSERT_EQUAL_INT(0, bt_manager_connect("AA:BB:CC:DD:EE:FF"));
    TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FF", bt_ctx.connected_mac);
}

void test_bt_manager_connect_fails_when_already_connected(void)
{
    bt_ctx.initialized = true;
    bt_ctx.connected = true;
    TEST_ASSERT_EQUAL_INT(-1, bt_manager_connect("AA:BB:CC:DD:EE:FF"));
}

/* ---- bt_get_device_list_snapshot / bt_get_paired_devices_snapshot ---- */

void test_get_device_list_snapshot_rejects_null_out(void)
{
    bt_ctx.initialized = true;
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, bt_get_device_list_snapshot(NULL));
}

void test_get_device_list_snapshot_rejects_when_not_initialized(void)
{
    bt_ctx.initialized = false;
    bt_device_list_t out;
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_STATE, bt_get_device_list_snapshot(&out));
    /* Call again to prove the lock wasn't left held on the failure path --
     * a leaked lock would hang this second call forever. */
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_STATE, bt_get_device_list_snapshot(&out));
}

void test_get_device_list_snapshot_copies_discovered_devices(void)
{
    bt_ctx.initialized = true;
    bt_ctx.discovered_devices.count = 1;
    util_safe_copy_str(bt_ctx.discovered_devices.devices[0].mac,
                       sizeof(bt_ctx.discovered_devices.devices[0].mac), "11:22:33:44:55:66");
    util_safe_copy_str(bt_ctx.discovered_devices.devices[0].name,
                       sizeof(bt_ctx.discovered_devices.devices[0].name), "Test Device");
    bt_ctx.discovered_devices.devices[0].rssi = -42;

    bt_device_list_t out = {0};
    TEST_ASSERT_EQUAL_INT(ESP_OK, bt_get_device_list_snapshot(&out));
    TEST_ASSERT_EQUAL_INT(1, out.count);
    TEST_ASSERT_EQUAL_STRING("11:22:33:44:55:66", out.devices[0].mac);
    TEST_ASSERT_EQUAL_STRING("Test Device", out.devices[0].name);
    TEST_ASSERT_EQUAL_INT(-42, out.devices[0].rssi);
}

void test_get_paired_devices_snapshot_rejects_null_out(void)
{
    bt_ctx.initialized = true;
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, bt_get_paired_devices_snapshot(NULL));
}

void test_get_paired_devices_snapshot_rejects_when_not_initialized(void)
{
    bt_ctx.initialized = false;
    bt_device_list_t out;
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_STATE, bt_get_paired_devices_snapshot(&out));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_STATE, bt_get_paired_devices_snapshot(&out));
}

void test_get_paired_devices_snapshot_copies_paired_devices(void)
{
    bt_ctx.initialized = true;
    bt_ctx.paired_devices.count = 1;
    util_safe_copy_str(bt_ctx.paired_devices.devices[0].mac,
                       sizeof(bt_ctx.paired_devices.devices[0].mac), "AA:AA:AA:AA:AA:AA");
    util_safe_copy_str(bt_ctx.paired_devices.devices[0].name,
                       sizeof(bt_ctx.paired_devices.devices[0].name), "Paired Speaker");

    bt_device_list_t out = {0};
    TEST_ASSERT_EQUAL_INT(ESP_OK, bt_get_paired_devices_snapshot(&out));
    TEST_ASSERT_EQUAL_INT(1, out.count);
    TEST_ASSERT_EQUAL_STRING("AA:AA:AA:AA:AA:AA", out.devices[0].mac);
    TEST_ASSERT_EQUAL_STRING("Paired Speaker", out.devices[0].name);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_bt_manager_profiles_init_order);

    /* BT-3 (docs/UNIT_TESTS2_TODO.md) */
    RUN_TEST(test_bt_manager_set_name_returns_zero_on_host);
    RUN_TEST(test_bt_manager_pair_fails_when_not_initialized);
    RUN_TEST(test_bt_manager_pair_succeeds_when_initialized_with_valid_mac);
    RUN_TEST(test_bt_manager_pair_fails_on_malformed_mac);
    RUN_TEST(test_bt_manager_connect_fails_when_not_initialized);
    RUN_TEST(test_bt_manager_connect_succeeds_when_initialized);
    RUN_TEST(test_bt_manager_connect_fails_when_already_connected);
    RUN_TEST(test_get_device_list_snapshot_rejects_null_out);
    RUN_TEST(test_get_device_list_snapshot_rejects_when_not_initialized);
    RUN_TEST(test_get_device_list_snapshot_copies_discovered_devices);
    RUN_TEST(test_get_paired_devices_snapshot_rejects_null_out);
    RUN_TEST(test_get_paired_devices_snapshot_rejects_when_not_initialized);
    RUN_TEST(test_get_paired_devices_snapshot_copies_paired_devices);
    return UNITY_END();
}
