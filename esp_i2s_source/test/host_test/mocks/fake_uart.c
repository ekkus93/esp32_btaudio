/* UART driver mock for host tests.
 * All operations succeed. uart_read_bytes returns 0 (no data).
 * uart_write_bytes records output for verification.
 */
#include "driver/uart.h"

#include <string.h>
#include <stdlib.h>

static uint8_t s_uart_tx_buf[4][512];
static size_t  s_uart_tx_len[4];

esp_err_t uart_driver_install(uint8_t uart_num, int rx_buf_size, int tx_buf_size, int rx_circ_buf, int tx_circ_buf, int queue_size)
{
    (void)rx_buf_size; (void)tx_buf_size; (void)rx_circ_buf; (void)tx_circ_buf; (void)queue_size;
    return 0; /* ESP_OK */
}

esp_err_t uart_driver_delete(uint8_t uart_num)
{
    (void)uart_num;
    return 0;
}

esp_err_t uart_param_config(uint8_t uart_num, const uart_config_t *cfg)
{
    (void)uart_num; (void)cfg;
    return 0;
}

esp_err_t uart_set_pin(uint8_t uart_num, uint8_t tx_pin, uint8_t rx_pin, uint8_t rts_pin, uint8_t cts_pin)
{
    (void)uart_num; (void)tx_pin; (void)rx_pin; (void)rts_pin; (void)cts_pin;
    return 0;
}

esp_err_t uart_flush_input(uint8_t uart_num)
{
    (void)uart_num;
    return 0;
}

esp_err_t uart_flush_output(uint8_t uart_num)
{
    (void)uart_num;
    return 0;
}

int uart_write_bytes(uint8_t uart_num, const void *data, size_t len)
{
    if (uart_num > 3) return -1;
    if (len > sizeof(s_uart_tx_buf[uart_num])) {
        len = sizeof(s_uart_tx_buf[uart_num]);
    }
    memcpy(s_uart_tx_buf[uart_num], data, len);
    s_uart_tx_len[uart_num] = len;
    return (int)len;
}

int uart_read_bytes(uint8_t uart_num, uint8_t *buf, size_t length, TickType_t wait)
{
    (void)uart_num; (void)buf; (void)length; (void)wait;
    return 0; /* No data available */
}