#include "unity.h"
#include "esp_err.h"
#include "driver/uart.h"
#include "command_interface.h"
#include <string.h>

#define TEST_UART_PORT UART_NUM_1

void test_uart_command_interface_setup(void)
{
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };

    esp_err_t err = uart_param_config(TEST_UART_PORT, &uart_config);
    TEST_ASSERT_EQUAL_HEX32(ESP_OK, err);

    err = uart_set_pin(TEST_UART_PORT, 17, 16, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    TEST_ASSERT_EQUAL_HEX32(ESP_OK, err);

    err = uart_driver_install(TEST_UART_PORT, 256, 0, 0, NULL, 0);
    TEST_ASSERT_EQUAL_HEX32(ESP_OK, err);

    const char *test_str = "CMD_TEST\n";
    int tx_bytes = uart_write_bytes(TEST_UART_PORT, test_str, strlen(test_str));
    TEST_ASSERT_EQUAL(strlen(test_str), tx_bytes);

    uart_driver_delete(TEST_UART_PORT);
}

void run_command_interface_tests(void)
{
    RUN_TEST(test_uart_command_interface_setup);
}
