// Minimal UART driver stub for host tests
#ifndef MOCK_UART_H
#define MOCK_UART_H

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

typedef int uart_port_t;

/* UART port numbers */
#define UART_NUM_0  0
#define UART_NUM_1  1
#define UART_NUM_2  2

/* UART config */
typedef struct {
    int    baud_rate;
    int    data_bits;
    int    parity;
    int    stop_bits;
    int    flow_ctrl;
    int    source_clk;
} uart_config_t;

#define UART_DATA_8_BITS       0
#define UART_PARITY_DISABLE    0
#define UART_STOP_BITS_1       0
#define UART_HW_FLOWCTRL_DISABLE  0
#define UART_SCLK_DEFAULT      0

/* Pin constants */
#define UART_PIN_NO_CHANGE  0xFF

/* Functions */
esp_err_t uart_driver_install(uart_port_t uart_num, int rx_buffer_size, int tx_buffer_size, int queue_size, QueueHandle_t *uart_queue, int intr_alloc_flags);
esp_err_t uart_driver_delete(uint8_t uart_num);
esp_err_t uart_param_config(uint8_t uart_num, const uart_config_t *cfg);
esp_err_t uart_set_pin(uint8_t uart_num, uint8_t tx_pin, uint8_t rx_pin, uint8_t rts_pin, uint8_t cts_pin);
esp_err_t uart_flush_input(uint8_t uart_num);
esp_err_t uart_flush_output(uint8_t uart_num);
int uart_write_bytes(uint8_t uart_num, const void *data, size_t len);
int uart_read_bytes(uint8_t uart_num, uint8_t *buf, size_t length, TickType_t wait);

#endif /* MOCK_UART_H */