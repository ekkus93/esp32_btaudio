#include "mock_uart.h"

#include <stdbool.h>
#include <string.h>

#define HFP_COMMAND_UART_BUFFER_SIZE 32768U
#define HFP_COMMAND_UART_PORTS 2

typedef struct {
    bool initialized;
    char rx[HFP_COMMAND_UART_BUFFER_SIZE];
    size_t rx_length;
    char tx[HFP_COMMAND_UART_BUFFER_SIZE];
    size_t tx_length;
    bool force_write_result;
    int forced_write_result;
} command_uart_state_t;

static command_uart_state_t s_uart[HFP_COMMAND_UART_PORTS];

static bool valid_port(int port)
{
    return port >= 0 && port < HFP_COMMAND_UART_PORTS;
}

void mock_uart_init_port(int uart_num, int baud_rate)
{
    (void)baud_rate;
    if (!valid_port(uart_num)) return;
    memset(&s_uart[uart_num], 0, sizeof(s_uart[uart_num]));
    s_uart[uart_num].initialized = true;
}

void mock_uart_init(int baud_rate)
{
    mock_uart_init_port(UART_NUM_1, baud_rate);
}

void mock_uart_reset_tx_port(int uart_num)
{
    if (!valid_port(uart_num)) return;
    s_uart[uart_num].tx_length = 0U;
    s_uart[uart_num].tx[0] = '\0';
}

void mock_uart_reset_tx(void)
{
    mock_uart_reset_tx_port(UART_NUM_1);
}

void mock_uart_force_write_result_port(int uart_num, int result)
{
    if (!valid_port(uart_num)) return;
    s_uart[uart_num].force_write_result = true;
    s_uart[uart_num].forced_write_result = result;
}

void mock_uart_inject_rx_data_port(int port, const char *data, size_t len)
{
    if (!valid_port(port) || data == NULL ||
        len > sizeof(s_uart[port].rx)) return;
    memcpy(s_uart[port].rx, data, len);
    s_uart[port].rx_length = len;
}

void mock_uart_inject_rx_data(const char *data, size_t len)
{
    mock_uart_inject_rx_data_port(UART_NUM_1, data, len);
}

const char *mock_uart_get_tx_data_port(int uart_num)
{
    return valid_port(uart_num) ? s_uart[uart_num].tx : "";
}

const char *mock_uart_get_tx_data(void)
{
    return mock_uart_get_tx_data_port(UART_NUM_1);
}

esp_err_t uart_driver_install(uart_port_t uart_num, int rx_buffer_size,
                              int tx_buffer_size, int queue_size,
                              void *uart_queue, int intr_alloc_flags)
{
    (void)rx_buffer_size;
    (void)tx_buffer_size;
    (void)queue_size;
    (void)uart_queue;
    (void)intr_alloc_flags;
    if (!valid_port((int)uart_num)) return ESP_ERR_INVALID_ARG;
    s_uart[uart_num].initialized = true;
    return ESP_OK;
}

esp_err_t uart_driver_delete(uart_port_t uart_num)
{
    if (!valid_port((int)uart_num)) return ESP_ERR_INVALID_ARG;
    s_uart[uart_num].initialized = false;
    return ESP_OK;
}

int uart_read_bytes(uart_port_t uart_num, uint8_t *buf, uint32_t length,
                    TickType_t ticks_to_wait)
{
    (void)ticks_to_wait;
    if (!valid_port((int)uart_num) || buf == NULL ||
        !s_uart[uart_num].initialized) return -1;
    size_t count = s_uart[uart_num].rx_length;
    if (count > (size_t)length) count = (size_t)length;
    memcpy(buf, s_uart[uart_num].rx, count);
    if (count < s_uart[uart_num].rx_length) {
        memmove(s_uart[uart_num].rx, s_uart[uart_num].rx + count,
                s_uart[uart_num].rx_length - count);
    }
    s_uart[uart_num].rx_length -= count;
    return (int)count;
}

int uart_write_bytes(uart_port_t uart_num, const char *src, size_t size)
{
    if (!valid_port((int)uart_num) || src == NULL ||
        !s_uart[uart_num].initialized) return -1;
    if (s_uart[uart_num].force_write_result) {
        return s_uart[uart_num].forced_write_result;
    }
    size_t available = sizeof(s_uart[uart_num].tx) -
                       s_uart[uart_num].tx_length - 1U;
    if (size > available) size = available;
    memcpy(s_uart[uart_num].tx + s_uart[uart_num].tx_length, src, size);
    s_uart[uart_num].tx_length += size;
    s_uart[uart_num].tx[s_uart[uart_num].tx_length] = '\0';
    return (int)size;
}

bool uart_is_driver_installed(uart_port_t uart_num)
{
    return valid_port((int)uart_num) && s_uart[uart_num].initialized;
}

bool mock_uart_is_initialized(uart_port_t uart_num)
{
    return uart_is_driver_installed(uart_num);
}

size_t mock_uart_get_available_bytes(uart_port_t uart_num)
{
    return valid_port((int)uart_num) ? s_uart[uart_num].rx_length : 0U;
}
