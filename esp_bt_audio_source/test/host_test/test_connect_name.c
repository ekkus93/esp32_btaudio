#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "command_interface.h"
#include "mock_uart.h"
#include "bt_manager.h"
#include "esp_err.h"

void setUp(void) {
    mock_uart_init(115200);
    // Initialize components used by commands.c
    cmd_init();
    // Initialize bt_manager for host tests
    bt_manager_init_t config = {
        .device_name = "UNIT_TEST",
        .connected_cb = NULL,
        .disconnected_cb = NULL
    };
    TEST_ASSERT_EQUAL(ESP_OK, bt_manager_init(&config));
}

void tearDown(void) {
    cmd_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, bt_manager_deinit());
}

// Test connecting by name: ensures CONNECT_NAME triggers name-based connect
void test_connect_name_command(void) {
    // Start scan and inject mock devices
    TEST_ASSERT_EQUAL(ESP_OK, bt_start_scan());
    bt_device_t mock1 = { .mac = "AA:BB:CC:11:22:33", .name = "Kitchen Speaker", .rssi = -70 };
    bt_device_t mock2 = { .mac = "DD:EE:FF:44:55:66", .name = "Living Room Speaker", .rssi = -60 };
    bt_manager_mock_device_found(&mock1);
    bt_manager_mock_device_found(&mock2);
    TEST_ASSERT_EQUAL(ESP_OK, bt_stop_scan());

    // Prepare cmd context and execute CONNECT_NAME "Living Room Speaker"
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("CONNECT_NAME Living Room Speaker", &ctx));
    TEST_ASSERT_EQUAL(CMD_TYPE_CONNECT_NAME, ctx.type);

    // Reset mock UART tx buffer
    mock_uart_reset_tx();

    // Execute - should invoke bt_connect_by_name and therefore return success
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    // Expect a response indicating we attempted to connect by name
    TEST_ASSERT_TRUE(strstr(tx, "CONNECT_NAME") != NULL);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_connect_name_command);
    return UNITY_END();
}
