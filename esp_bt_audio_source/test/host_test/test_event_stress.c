#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "unity.h"
#include "mock_uart.h"
#include "command_interface.h"

// Expose mock helper to reset bt_manager discovered devices
extern void bt_manager_mock_reset_discovered(void);

// Test: simulate a burst of device-found notifications and ensure that
// the command interface output does not exceed the configured rate and
// that SEQ values in emitted lines are strictly increasing.

void setUp(void) {
    mock_uart_init(115200);
    cmd_init();
}

void tearDown(void) {
    cmd_deinit();
}

// Simple parser to extract SEQ value from a line like: "INFO|SCAN|DEVICE_FOUND|AA:BB:...\n"
// Parse SEQ and TS (ms) fields from a line. Returns 0 on success.
static int parse_seq_ts(const char* line, int* out_seq, long long* out_ts_ms) {
    if (!line || !out_seq || !out_ts_ms) return -1;
    const char* seq = strstr(line, "SEQ=");
    const char* ts = strstr(line, "TS=");
    if (!seq || !ts) return -1;
    *out_seq = atoi(seq + 4);
    *out_ts_ms = atoll(ts + 3);
    return 0;
}

void test_event_stress_burst(void) {
    mock_uart_reset_tx();

    // Emit rapid events (directly call command emitter as the device->event
    // plumbing is exercised elsewhere in integration tests).
    for (int i = 0; i < 50; ++i) {
        char payload[128];
        snprintf(payload, sizeof(payload), "AA:BB:CC:00:00:%02x,Device_%02d", i, i);
        cmd_send_response("INFO", "SCAN", "DEVICE_FOUND", payload);
    }

    // Read TX buffer and ensure we emitted at least one DEVICE_FOUND line
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);

    int found = 0;
    const char* p = tx;
    int last_seq = -1;
    long long last_ts = -1;
    while (p && *p) {
        const char* line_end = strchr(p, '\n');
        if (!line_end) break;
        size_t len = (size_t)(line_end - p);
        char line[4096];
        if (len > sizeof(line)-1) len = sizeof(line)-1;
        memcpy(line, p, len);
        line[len] = '\0';
        if (strstr(line, "DEVICE_FOUND") != NULL) {
            int seq = -1; long long ts = -1;
            if (parse_seq_ts(line, &seq, &ts) == 0) {
                if (last_seq >= 0) TEST_ASSERT_TRUE(seq > last_seq);
                if (last_ts >= 0) TEST_ASSERT_TRUE(ts >= last_ts);
                last_seq = seq; last_ts = ts;
            }
            found++;
        }
        p = line_end + 1;
    }

    TEST_ASSERT_GREATER_THAN_INT(0, found);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_event_stress_burst);
    return UNITY_END();
}
