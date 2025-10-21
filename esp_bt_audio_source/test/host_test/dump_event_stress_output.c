#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "mock_uart.h"
#include "command_interface.h"

// Reuse the same sequence as test_event_stress
int main(int argc, char** argv) {
    int count = 1000;
    int delay_us = 1000; // 1 ms
    if (argc > 1) count = atoi(argv[1]);
    if (argc > 2) delay_us = atoi(argv[2]);

    mock_uart_init(115200);
    cmd_init();

    for (int i = 0; i < count; ++i) {
        char payload[128];
        snprintf(payload, sizeof(payload), "AA:BB:CC:00:00:%02x,Device_%04d", i & 0xff, i);
        cmd_send_response("INFO", "SCAN", "DEVICE_FOUND", payload);

        const char* tx = mock_uart_get_tx_data();
        if (tx) {
            // Print the current TX buffer contents (one event)
            printf("%s\n", tx);
        }

        // Reset tx buffer so we don't overflow MOCK_UART_BUFFER_SIZE
        mock_uart_reset_tx();

        usleep(delay_us);
    }

    cmd_deinit();
    return 0;
}
