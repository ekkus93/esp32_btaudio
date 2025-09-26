#ifndef MOCK_UART_H
#define MOCK_UART_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/**
 * @brief UART port numbers
 */
typedef enum {
    UART_NUM_0 = 0,  /*!< UART port 0 */
    UART_NUM_1 = 1,  /*!< UART port 1 */
    UART_NUM_MAX     /*!< Max UART port number */
} uart_port_t;

/**
 * @brief ESP error codes
 */

/**
 * @brief FreeRTOS tick type
 */
typedef uint32_t TickType_t;

/**
 * Initialize the mock UART
 *
 * @param baud_rate The baud rate to use
 */
void mock_uart_init(int baud_rate);

/**
 * Reset the TX buffer
 */
void mock_uart_reset_tx(void);

/**
 * Inject data into the RX buffer to simulate receiving data
 *
 * @param data Pointer to data to inject
 * @param len Length of data to inject
 */
void mock_uart_inject_rx_data(const char* data, size_t len);

/**
 * Get the transmitted data
 *
 * @return Pointer to transmitted data
 */
const char* mock_uart_get_tx_data(void);

/**
 * Check if UART is initialized
 *
 * @param uart_num UART port number
 * @return true if UART is initialized, false otherwise
 */
bool mock_uart_is_initialized(uart_port_t uart_num);

/**
 * Get available bytes in RX buffer
 *
 * @param uart_num UART port number
 * @return Number of available bytes
 */
size_t mock_uart_get_available_bytes(uart_port_t uart_num);

/**
 * ESP-IDF UART driver installation
 * 
 * @param uart_num UART port number
 * @param rx_buffer_size Size of RX buffer
 * @param tx_buffer_size Size of TX buffer
 * @param queue_size Size of event queue
 * @param uart_queue Queue handle
 * @param intr_alloc_flags Interrupt allocation flags
 * @return ESP_OK on success
 */
esp_err_t uart_driver_install(uart_port_t uart_num, int rx_buffer_size, int tx_buffer_size,
                             int queue_size, void* uart_queue, int intr_alloc_flags);

/**
 * ESP-IDF UART driver deletion
 * 
 * @param uart_num UART port number
 * @return ESP_OK on success
 */
esp_err_t uart_driver_delete(uart_port_t uart_num);

/**
 * ESP-IDF UART read bytes
 * 
 * @param uart_num UART port number
 * @param buf Buffer to store data
 * @param length Maximum bytes to read
 * @param ticks_to_wait Ticks to wait
 * @return Number of bytes read or -1 on error
 */
int uart_read_bytes(uart_port_t uart_num, uint8_t* buf, uint32_t length, TickType_t ticks_to_wait);

/**
 * ESP-IDF UART write bytes
 * 
 * @param uart_num UART port number
 * @param src Source data
 * @param size Data size
 * @return Number of bytes written or -1 on error
 */
int uart_write_bytes(uart_port_t uart_num, const char* src, size_t size);

#endif /* MOCK_UART_H */
