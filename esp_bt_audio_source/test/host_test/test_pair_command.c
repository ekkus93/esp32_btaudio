#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "unity.h"
#include "mock_uart.h"
#include "command_interface.h"
#include "bt_manager.h"

// Provide a strong test hook to observe when pairing is requested via
// the manager wrapper. This overrides the weak no-op in the manager
// so host-mode tests can deterministically assert the call occurred.
static int g_pair_start_called = 0;
static char g_pair_start_mac[32] = {0};

void bt_manager_test_record_pair_start(const char* mac) {
    g_pair_start_called++;
    if (mac) {
        strncpy(g_pair_start_mac, mac, sizeof(g_pair_start_mac)-1);
        g_pair_start_mac[sizeof(g_pair_start_mac)-1] = '\0';
    } else {
        g_pair_start_mac[0] = '\0';
    }
}

void setUp(void) {
    mock_uart_init(115200);
    cmd_init();
    g_pair_start_called = 0;
    g_pair_start_mac[0] = '\0';
}

void tearDown(void) {
    cmd_deinit();
}

void test_pair_mac_calls_manager_wrapper_and_hook(void) {
    mock_uart_reset_tx();

    const char* mac = "AA:BB:CC:DD:EE:01";
    char cmdline[64];
    snprintf(cmdline, sizeof(cmdline), "PAIR %s", mac);

    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse(cmdline, &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    // The manager wrapper should invoke our test hook exactly once
    TEST_ASSERT_EQUAL_INT(1, g_pair_start_called);
    TEST_ASSERT_EQUAL_STRING(mac, g_pair_start_mac);

    // Verify a response was emitted on the mock UART
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "PAIR"));
}

void test_pair_name_uses_connect_by_name_and_returns_ok(void) {
    mock_uart_reset_tx();

    // Prepare a mock device discovered by the manager so name lookup succeeds
    bt_device_t dev = {0};
    strncpy(dev.mac, "AA:BB:CC:DD:EE:02", sizeof(dev.mac)-1);
    strncpy(dev.name, "MY_SINK", sizeof(dev.name)-1);
    dev.rssi = -42;

    // Add to discovered list via mock helper
    bt_manager_mock_device_found(&dev);

    // Issue PAIR by name (should call connect_by_name path)
    char cmdline[64];
    snprintf(cmdline, sizeof(cmdline), "PAIR %s", "MY_SINK");

    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse(cmdline, &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    // Verify the mock UART output contains the expected mock-initiated response
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "MOCK_INITIATED_BY_NAME"));
    TEST_ASSERT_NOT_NULL(strstr(tx, "MY_SINK"));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_pair_mac_calls_manager_wrapper_and_hook);
    RUN_TEST(test_pair_name_uses_connect_by_name_and_returns_ok);
    return UNITY_END();
}
