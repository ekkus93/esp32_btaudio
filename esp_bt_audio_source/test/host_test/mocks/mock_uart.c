#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>  // Add this include for uint8_t and uint32_t types

// Mock UART driver for testing

// Buffer sizes
// Increased buffer size for stress tests to reduce truncation warnings
#define MOCK_UART_BUFFER_SIZE 32768
#define MOCK_UART_MAX_PORTS 2

// UART ports
typedef enum {
    UART_NUM_0 = 0,
    UART_NUM_1 = 1,
    UART_NUM_MAX,
} uart_port_t;

// Status codes 
typedef enum {
    ESP_OK = 0,
    ESP_FAIL = -1,
    ESP_ERR_NO_MEM = -100,
    ESP_ERR_INVALID_ARG = -101,
    ESP_ERR_INVALID_STATE = -102,
} esp_err_t;

// Define FreeRTOS tick type for mocking
typedef uint32_t TickType_t;  // Add this definition for TickType_t

// Mock state for each UART port
static struct {
    bool initialized;
    int baud_rate;
    char rx_buffer[MOCK_UART_BUFFER_SIZE]; // Data received by UART (to be read by application)
    size_t rx_write_pos;  // Position to write to
    size_t rx_read_pos;   // Position to read from
    size_t rx_available;  // Bytes available to read
    
    char tx_buffer[MOCK_UART_BUFFER_SIZE]; // Data transmitted via UART (written by application)
    size_t tx_pos;
} uart_mock_state[MOCK_UART_MAX_PORTS] = {
    { false, 0, {0}, 0, 0, 0, {0}, 0 },
    { false, 0, {0}, 0, 0, 0, {0}, 0 }
};

// Initialize a specific mock UART port
void mock_uart_init_port(int uart_num, int baud_rate) {
    if (uart_num < 0 || uart_num >= MOCK_UART_MAX_PORTS) {
        return;
    }
    memset(&uart_mock_state[uart_num], 0, sizeof(uart_mock_state[0]));
    uart_mock_state[uart_num].initialized = true;
    uart_mock_state[uart_num].baud_rate = baud_rate;
    printf("Mock UART %d initialized with baud rate: %d\n", uart_num, baud_rate);
}

// Initialize mock UART (legacy default: UART1, the primary command port)
void mock_uart_init(int baud_rate) {
    mock_uart_init_port(UART_NUM_1, baud_rate);
}

// Reset TX buffer of a specific port
void mock_uart_reset_tx_port(int uart_num) {
    if (uart_num < 0 || uart_num >= MOCK_UART_MAX_PORTS) {
        return;
    }
    memset(uart_mock_state[uart_num].tx_buffer, 0, sizeof(uart_mock_state[uart_num].tx_buffer));
    uart_mock_state[uart_num].tx_pos = 0;
}

// Reset TX buffer (legacy default: UART1)
void mock_uart_reset_tx(void) {
    mock_uart_reset_tx_port(UART_NUM_1);
}

// Inject data into a specific port's RX buffer
void mock_uart_inject_rx_data_port(int port, const char* data, size_t len) {
    if (port < 0 || port >= MOCK_UART_MAX_PORTS) {
        return;
    }
    uart_port_t uart_num = (uart_port_t)port;
    
    if (!uart_mock_state[uart_num].initialized) {
        printf("Error: UART %d not initialized\n", uart_num);
        return;
    }
    
    if (uart_mock_state[uart_num].rx_available + len > MOCK_UART_BUFFER_SIZE) {
        printf("Error: Mock UART RX buffer overflow\n");
        return;
    }
    
    // If buffer wrapped around, reorganize it
    if (uart_mock_state[uart_num].rx_write_pos + len > MOCK_UART_BUFFER_SIZE &&
        uart_mock_state[uart_num].rx_available > 0) {
        
        // Move existing data to beginning of buffer
        memmove(uart_mock_state[uart_num].rx_buffer, 
                uart_mock_state[uart_num].rx_buffer + uart_mock_state[uart_num].rx_read_pos,
                uart_mock_state[uart_num].rx_available);
                
        uart_mock_state[uart_num].rx_write_pos = uart_mock_state[uart_num].rx_available;
        uart_mock_state[uart_num].rx_read_pos = 0;
    }
    
    // Copy new data to buffer
    memcpy(uart_mock_state[uart_num].rx_buffer + uart_mock_state[uart_num].rx_write_pos, data, len);
    uart_mock_state[uart_num].rx_write_pos += len;
    uart_mock_state[uart_num].rx_available += len;
    
    printf("Mock UART %d: Injected %zu bytes, available: %zu\n", 
           uart_num, len, uart_mock_state[uart_num].rx_available);
}

// Inject data (legacy default: UART1)
void mock_uart_inject_rx_data(const char* data, size_t len) {
    mock_uart_inject_rx_data_port(UART_NUM_1, data, len);
}

// Get transmitted data of a specific port
const char* mock_uart_get_tx_data_port(int uart_num) {
    if (uart_num < 0 || uart_num >= MOCK_UART_MAX_PORTS) {
        return "";
    }
    return uart_mock_state[uart_num].tx_buffer;
}

// Get the transmitted data (legacy default: UART1)
const char* mock_uart_get_tx_data(void) {
    return mock_uart_get_tx_data_port(UART_NUM_1);
}

// Driver implementation

esp_err_t uart_driver_install(uart_port_t uart_num, int rx_buffer_size, int tx_buffer_size, 
                             int queue_size, void* uart_queue, int intr_alloc_flags) {
    if (uart_num >= UART_NUM_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (uart_mock_state[uart_num].initialized) {
        return ESP_ERR_INVALID_STATE; // Already initialized
    }
    
    memset(&uart_mock_state[uart_num], 0, sizeof(uart_mock_state[0]));
    uart_mock_state[uart_num].initialized = true;
    
    printf("UART driver installed for port %d\n", uart_num);
    return ESP_OK;
}

esp_err_t uart_driver_delete(uart_port_t uart_num) {
    if (uart_num >= UART_NUM_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!uart_mock_state[uart_num].initialized) {
        return ESP_ERR_INVALID_STATE; // Not initialized
    }
    
    uart_mock_state[uart_num].initialized = false;
    
    printf("UART driver uninstalled for port %d\n", uart_num);
    return ESP_OK;
}

int uart_read_bytes(uart_port_t uart_num, uint8_t *buf, uint32_t length, TickType_t ticks_to_wait) {
    if (uart_num >= UART_NUM_MAX || buf == NULL) {
        return -1;
    }
    
    if (!uart_mock_state[uart_num].initialized) {
        return -1;
    }
    
    if (uart_mock_state[uart_num].rx_available == 0) {
        return 0; // No data
    }
    
    size_t bytes_to_read = (uart_mock_state[uart_num].rx_available < length) ? 
                           uart_mock_state[uart_num].rx_available : length;
    
    // Copy data from the buffer
    memcpy(buf, uart_mock_state[uart_num].rx_buffer + uart_mock_state[uart_num].rx_read_pos, bytes_to_read);
    
    // Update read position and available bytes
    uart_mock_state[uart_num].rx_read_pos += bytes_to_read;
    uart_mock_state[uart_num].rx_available -= bytes_to_read;
    
    // If all data has been read, reset positions
    if (uart_mock_state[uart_num].rx_available == 0) {
        uart_mock_state[uart_num].rx_read_pos = 0;
        uart_mock_state[uart_num].rx_write_pos = 0;
    }
    
    return bytes_to_read;
}

int uart_write_bytes(uart_port_t uart_num, const char* src, size_t size) {
    if (uart_num >= UART_NUM_MAX || src == NULL) {
        return -1;
    }
    
    if (!uart_mock_state[uart_num].initialized) {
        return -1;
    }
    
    if (uart_mock_state[uart_num].tx_pos + size > MOCK_UART_BUFFER_SIZE) {
        printf("Warning: Mock UART TX buffer overflow, truncating\n");
        size = MOCK_UART_BUFFER_SIZE - uart_mock_state[uart_num].tx_pos;
    }
    
    if (size > 0) {
        memcpy(uart_mock_state[uart_num].tx_buffer + uart_mock_state[uart_num].tx_pos, src, size);
        uart_mock_state[uart_num].tx_pos += size;
    }

    /* Always keep the TX buffer null-terminated so callers that treat
     * it as a C-string (strstr, printf, etc.) see defined behavior. If
     * we've filled the buffer exactly, ensure the last byte is '\0'. */
    if (uart_mock_state[uart_num].tx_pos < MOCK_UART_BUFFER_SIZE) {
        uart_mock_state[uart_num].tx_buffer[uart_mock_state[uart_num].tx_pos] = '\0';
    } else if (MOCK_UART_BUFFER_SIZE > 0) {
        uart_mock_state[uart_num].tx_buffer[MOCK_UART_BUFFER_SIZE - 1] = '\0';
    }

    return size;
}

// ESP-IDF API: Check if UART driver is installed
bool uart_is_driver_installed(uart_port_t uart_num) {
    if (uart_num >= UART_NUM_MAX) {
        return false;
    }
    return uart_mock_state[uart_num].initialized;
}

// Test helper functions
bool mock_uart_is_initialized(uart_port_t uart_num) {
    if (uart_num >= UART_NUM_MAX) {
        return false;
    }
    return uart_mock_state[uart_num].initialized;
}

size_t mock_uart_get_available_bytes(uart_port_t uart_num) {
    if (uart_num >= UART_NUM_MAX) {
        return 0;
    }
    return uart_mock_state[uart_num].rx_available;
}
