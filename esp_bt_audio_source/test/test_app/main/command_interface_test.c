#include "unity.h"
#include "esp_err.h"
#include "driver/uart.h"
#include "command_interface.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#define TEST_UART_PORT UART_NUM_1

static char s_cmd_response_buf[4096];
static size_t s_cmd_response_len;

void cmd_test_capture_response(const char *line)
{
    if (!line)
    {
        return;
    }
    size_t space = sizeof(s_cmd_response_buf) - s_cmd_response_len - 1;
    if (space == 0)
    {
        return;
    }
    size_t line_len = strlen(line);
    if (line_len > space)
    {
        line_len = space;
    }
    memcpy(s_cmd_response_buf + s_cmd_response_len, line, line_len);
    s_cmd_response_len += line_len;
    s_cmd_response_buf[s_cmd_response_len] = '\0';
}

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

void test_help_command_emits_on_cmd_uart(void)
{
    s_cmd_response_len = 0;
    s_cmd_response_buf[0] = '\0';

    /* Ensure command subsystem initialized so cmd_execute is valid. */
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_init());

    cmd_context_t ctx = {0};
    ctx.type = CMD_TYPE_HELP;
    ctx.param_count = 0;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    /* Must include HELP summary and DONE marker to prove emission on the command interface. */
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(s_cmd_response_buf, "INFO|HELP|SUMMARY|"), "HELP summary missing on command UART");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(s_cmd_response_buf, "OK|HELP|DONE|"), "HELP completion marker missing on command UART");
}

void run_command_interface_tests(void)
{
    RUN_TEST(test_uart_command_interface_setup);
    RUN_TEST(test_help_command_emits_on_cmd_uart);
}
