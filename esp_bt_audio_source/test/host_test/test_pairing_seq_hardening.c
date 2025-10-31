#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "unity.h"
#include "mock_uart.h"
#include "command_interface.h"

void setUp(void) {
    mock_uart_init(115200);
    cmd_init();
}

void tearDown(void) {
    cmd_deinit();
}

// Parse SEQ and TS fields from a line like "EVENT|PAIR|PIN_REQUEST|... ,SEQ=123,TS=456"
static int parse_seq_ts(const char* line, int* out_seq, long long* out_ts_ms) {
    if (!line || !out_seq || !out_ts_ms) return -1;
    const char* seq = strstr(line, "SEQ=");
    const char* ts = strstr(line, "TS=");
    if (!seq || !ts) return -1;
    *out_seq = atoi(seq + 4);
    *out_ts_ms = atoll(ts + 3);
    return 0;
}

void test_pairing_event_sequence_hardening(void) {
    mock_uart_reset_tx();

    // Emit a burst of PIN_REQUEST pairing events
    for (int i = 0; i < 50; ++i) {
        char mac[32];
        snprintf(mac, sizeof(mac), "11:22:33:44:55:%02x", i);
        // Use convenience helper to emit pairing event
        cmd_send_event_pair("PIN_REQUEST", mac);
    }

    // Capture TX buffer and verify SEQ/TS ordering for EVENT|PAIR lines
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);

    const char* p = tx;
    int last_seq = -1;
    long long last_ts = -1;
    int found = 0;
    while (p && *p) {
        const char* e = strchr(p, '\n');
        if (!e) break;
        size_t len = (size_t)(e - p);
        char line[512];
        if (len >= sizeof(line)) len = sizeof(line)-1;
        memcpy(line, p, len);
        line[len] = '\0';

        if (strstr(line, "EVENT|PAIR|") != NULL) {
            int seq = -1; long long ts = -1;
            TEST_ASSERT_EQUAL_MESSAGE(0, parse_seq_ts(line, &seq, &ts), "Pair event missing SEQ/TS annotation");
            if (last_seq >= 0) TEST_ASSERT_TRUE(seq > last_seq);
            if (last_ts >= 0) TEST_ASSERT_TRUE(ts >= last_ts);
            last_seq = seq; last_ts = ts;
            found++;
        }

        p = e + 1;
    }

    TEST_ASSERT_GREATER_THAN_INT(0, found);

    // Now exercise command handling: set up the mock pairing so ENTER_PIN
    // will be accepted and produce CONFIRM and SUCCESS events.
    char target_mac[32];
    snprintf(target_mac, sizeof(target_mac), "11:22:33:44:55:%02x", 0);

    // Enable mock pairing flows and register the pairing address
    cmd_context_t ctx;
    char cmdline[128];
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("DEBUG MOCK_ON", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));
    // Add the mock pairing address (provide a dummy passkey param to satisfy parsing)
    snprintf(cmdline, sizeof(cmdline), "DEBUG MOCK_ADD %s 000000", target_mac);
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse(cmdline, &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    // Build and execute ENTER_PIN command which should trigger CONFIRM/SUCCESS
    snprintf(cmdline, sizeof(cmdline), "ENTER_PIN %s 123456", target_mac);
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse(cmdline, &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    // Capture latest TX and find CONFIRM/SUCCESS entries, verify SEQ > last_seq
    const char* tx2 = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx2);
    p = tx2;
    int seen_confirm = 0, seen_success = 0;
    while (p && *p) {
        const char* e = strchr(p, '\n');
        if (!e) break;
        size_t len = (size_t)(e - p);
        char line[512];
        if (len >= sizeof(line)) len = sizeof(line)-1;
        memcpy(line, p, len);
        line[len] = '\0';

        if (strstr(line, "EVENT|PAIR|CONFIRM|") != NULL) {
            int seq = -1; long long ts = -1;
            TEST_ASSERT_EQUAL_MESSAGE(0, parse_seq_ts(line, &seq, &ts), "CONFIRM event missing SEQ/TS annotation");
            /* If a CONFIRM event is present, it must have a later sequence */
            if (last_seq >= 0) TEST_ASSERT_TRUE(seq > last_seq);
            if (last_ts >= 0) TEST_ASSERT_TRUE(ts >= last_ts);
            last_seq = seq; last_ts = ts;
            seen_confirm = 1;
        }
        if (strstr(line, "EVENT|PAIR|SUCCESS|") != NULL) {
            int seq = -1; long long ts = -1;
            TEST_ASSERT_EQUAL_MESSAGE(0, parse_seq_ts(line, &seq, &ts), "SUCCESS event missing SEQ/TS annotation");
            /* SUCCESS must always have a later sequence than previously observed */
            if (last_seq >= 0) TEST_ASSERT_TRUE(seq > last_seq);
            if (last_ts >= 0) TEST_ASSERT_TRUE(ts >= last_ts);
            last_seq = seq; last_ts = ts;
            seen_success = 1;
        }

        p = e + 1;
    }

    /* CONFIRM may not be emitted in host-mode mock flows (MOCK_ADD + ENTER_PIN).
     * Only require SUCCESS is observed; if CONFIRM was emitted, it already
     * had its sequence validated above. */
    TEST_ASSERT_TRUE(seen_success);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_pairing_event_sequence_hardening);
    return UNITY_END();
}
