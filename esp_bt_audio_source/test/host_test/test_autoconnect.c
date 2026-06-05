/**
 * test_autoconnect.c — PHASE-5c: boot-time auto-connect logic tests.
 *
 * Tests the individual building blocks of attempt_autoconnect_if_configured():
 *   - nvs_storage_get_last_connected_mac: returns ESP_OK or NOT_FOUND
 *   - bt_manager_is_autostart_enabled: returns true or false
 *   - bt_connect: called when both conditions met, skipped otherwise
 *
 * The helper itself lives in main.c (ESP_PLATFORM only) so we test the
 * dependencies and contract instead of the function directly.
 */

#include <string.h>
#include "unity.h"
#include "bt_manager.h"
#include "nvs_storage.h"
#include "platform_storage.h"

/* Provided by nvs_storage_mock.c */
extern void nvs_storage_mock_set_last_mac(const char *mac, esp_err_t get_result);

/* bt_connect call counter — provided by mock_audio_and_btstate.c or a local stub */
static int s_bt_connect_calls = 0;
static char s_bt_connect_last_mac[18] = {0};

/* Stubs for BT connection functions — bt_connection.c is excluded from this
 * test to avoid conflicts with the bt_connect counting stub above. */
esp_err_t bt_connect(const char *mac)
{
    s_bt_connect_calls++;
    if (mac) {
        strncpy(s_bt_connect_last_mac, mac, sizeof(s_bt_connect_last_mac) - 1);
        s_bt_connect_last_mac[sizeof(s_bt_connect_last_mac) - 1] = '\0';
    }
    return ESP_OK;
}

esp_err_t bt_disconnect(void)
{
    return ESP_OK;
}

void setUp(void)
{
    s_bt_connect_calls = 0;
    s_bt_connect_last_mac[0] = '\0';
    nvs_storage_mock_set_last_mac(NULL, PLATFORM_ERR_STORAGE_NOT_FOUND);
    bt_manager_set_autostart_enabled(true);
}

void tearDown(void)
{
    bt_manager_set_autostart_enabled(false);
}

/* ── dependency contract tests ──────────────────────────────────────────── */

void test_get_last_mac_returns_ok_when_stored(void)
{
    nvs_storage_mock_set_last_mac("AA:BB:CC:DD:EE:FF", ESP_OK);
    char buf[18] = {0};
    TEST_ASSERT_EQUAL(ESP_OK, nvs_storage_get_last_connected_mac(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FF", buf);
}

void test_get_last_mac_returns_not_found_when_absent(void)
{
    nvs_storage_mock_set_last_mac(NULL, PLATFORM_ERR_STORAGE_NOT_FOUND);
    char buf[18] = {0};
    esp_err_t err = nvs_storage_get_last_connected_mac(buf, sizeof(buf));
    TEST_ASSERT_NOT_EQUAL(ESP_OK, err);
}

void test_autostart_enabled_flag_reflects_setter(void)
{
    bt_manager_set_autostart_enabled(true);
    TEST_ASSERT_TRUE(bt_manager_is_autostart_enabled());

    bt_manager_set_autostart_enabled(false);
    TEST_ASSERT_FALSE(bt_manager_is_autostart_enabled());
}

/* ── auto-connect condition matrix ─────────────────────────────────────── */

void test_bt_connect_called_when_mac_stored_and_autostart_enabled(void)
{
    nvs_storage_mock_set_last_mac("AA:BB:CC:DD:EE:FF", ESP_OK);
    bt_manager_set_autostart_enabled(true);

    /* Simulate what attempt_autoconnect_if_configured() does */
    char last_mac[18] = {0};
    esp_err_t err = nvs_storage_get_last_connected_mac(last_mac, sizeof(last_mac));
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(bt_manager_is_autostart_enabled());
    bt_connect(last_mac);

    TEST_ASSERT_EQUAL(1, s_bt_connect_calls);
    TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FF", s_bt_connect_last_mac);
}

void test_bt_connect_skipped_when_no_mac_stored(void)
{
    nvs_storage_mock_set_last_mac(NULL, PLATFORM_ERR_STORAGE_NOT_FOUND);
    bt_manager_set_autostart_enabled(true);

    char last_mac[18] = {0};
    esp_err_t err = nvs_storage_get_last_connected_mac(last_mac, sizeof(last_mac));
    /* Should not call bt_connect when get returns error */
    if (err == ESP_OK) {
        bt_connect(last_mac);
    }

    TEST_ASSERT_EQUAL(0, s_bt_connect_calls);
}

void test_bt_connect_skipped_when_autostart_disabled(void)
{
    nvs_storage_mock_set_last_mac("BB:CC:DD:EE:FF:00", ESP_OK);
    bt_manager_set_autostart_enabled(false);

    char last_mac[18] = {0};
    esp_err_t err = nvs_storage_get_last_connected_mac(last_mac, sizeof(last_mac));
    /* Should not call bt_connect when autostart disabled */
    if (err == ESP_OK && bt_manager_is_autostart_enabled()) {
        bt_connect(last_mac);
    }

    TEST_ASSERT_EQUAL(0, s_bt_connect_calls);
}

int main(void)
{
    UNITY_BEGIN();

    /* Dependency contract */
    RUN_TEST(test_get_last_mac_returns_ok_when_stored);
    RUN_TEST(test_get_last_mac_returns_not_found_when_absent);
    RUN_TEST(test_autostart_enabled_flag_reflects_setter);

    /* Auto-connect condition matrix */
    RUN_TEST(test_bt_connect_called_when_mac_stored_and_autostart_enabled);
    RUN_TEST(test_bt_connect_skipped_when_no_mac_stored);
    RUN_TEST(test_bt_connect_skipped_when_autostart_disabled);

    return UNITY_END();
}
