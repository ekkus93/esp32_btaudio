#ifndef MOCK_UART_H
#define MOCK_UART_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    UART_NUM_0 = 0,
    UART_NUM_1 = 1,
    UART_NUM_MAX
} uart_port_t;

typedef uint32_t TickType_t;

void mock_uart_init(int baud_rate);
void mock_uart_reset_tx(void);
void mock_uart_inject_rx_data(const char* data, size_t len);
const char* mock_uart_get_tx_data(void);
bool mock_uart_is_initialized(uart_port_t uart_num);
size_t mock_uart_get_available_bytes(uart_port_t uart_num);

esp_err_t uart_driver_install(uart_port_t uart_num, int rx_buffer_size, int tx_buffer_size,
                             int queue_size, void* uart_queue, int intr_alloc_flags);
esp_err_t uart_driver_delete(uart_port_t uart_num);
bool uart_is_driver_installed(uart_port_t uart_num);
int uart_read_bytes(uart_port_t uart_num, uint8_t* buf, uint32_t length, TickType_t ticks_to_wait);
int uart_write_bytes(uart_port_t uart_num, const char* src, size_t size);

#endif /* MOCK_UART_H */
