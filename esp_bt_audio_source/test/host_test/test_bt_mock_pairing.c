#include "unity.h"
#include "bt_mock_devices.h"
#include "bt_source.h"
#include "esp_err.h"

void setUp(void) {
    bt_mock_devices_init();
}

void tearDown(void) {
    bt_mock_devices_reset();
}

TEST_CASE("bt_mock SSP confirm flow succeeds and fails", "[bt_mock]") {
    const char *addr = "11:22:33:44:55:66";
    /* Ensure SSP is enabled */
    bt_mock_set_ssp_supported(true);

    /* Start pairing (SSP) and check that confirmation is requested */
    TEST_ASSERT_EQUAL(ESP_OK, bt_mock_start_pairing(addr));
    TEST_ASSERT_TRUE(bt_mock_is_ssp_confirm_requested());

    /* Get passkey */
    char passkey[16];
    TEST_ASSERT_EQUAL(ESP_OK, bt_mock_get_ssp_passkey(passkey, sizeof(passkey)));
    TEST_ASSERT_EQUAL_INT(6, strlen(passkey));

    /* Confirm -> success */
    TEST_ASSERT_EQUAL(ESP_OK, bt_mock_confirm_ssp(true));
    /* After confirm, not requested */
    TEST_ASSERT_FALSE(bt_mock_is_ssp_confirm_requested());

    /* Now simulate failure path: start pairing again and reject */
    TEST_ASSERT_EQUAL(ESP_OK, bt_mock_start_pairing(addr));
    TEST_ASSERT_TRUE(bt_mock_is_ssp_confirm_requested());
    TEST_ASSERT_EQUAL(ESP_OK, bt_mock_confirm_ssp(false));
}

TEST_CASE("bt_mock PIN flow success and simulated failure", "[bt_mock]") {
    const char *addr = "AA:BB:CC:DD:EE:FF";

    /* Force PIN method */
    bt_mock_set_ssp_supported(false);
    TEST_ASSERT_EQUAL(ESP_OK, bt_mock_start_pairing(addr));

    /* Send default PIN and expect success */
    TEST_ASSERT_EQUAL(ESP_OK, bt_mock_send_pin("0000"));

    /* Simulate failure and verify send_pin returns failure */
    bt_mock_simulate_pin_failure();
    esp_err_t r = bt_mock_send_pin("0000");
    TEST_ASSERT_NOT_EQUAL(ESP_OK, r);
}
