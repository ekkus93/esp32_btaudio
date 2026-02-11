/* Device-side Unity test: pairing event sequence hardening
 * - Emits many PAIR requests to generate PIN_REQUEST events and verifies
 *   the `seq=` field is strictly increasing.
 * - Then performs a pairing/enter-pin flow and asserts a CONFIRM (if
 *   emitted) and SUCCESS are produced with sequence numbers larger than
 *   the previous burst.
 */
#include "unity.h"
#include "test_utils.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

extern void cmd_reset_event_sequence(void);
bool test_send_serial_cmd(const char *cmd);
bool test_capture_event(char *out_buf, size_t out_len);
void test_utils_reset_state(void);

static int parse_seq(const char *ev)
{
    if (!ev) return -1;
    const char *s = strcasestr(ev, "seq=");
    if (!s) s = strcasestr(ev, "SEQ=");
    if (!s) return -1;
    s += 4;
    // parse integer until non-digit
    int v = 0;
    while (*s && (*s < '0' || *s > '9')) ++s; // skip non-digits if any
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); ++s; }
    return v;
}

void test_pairing_device_seq_hardening(void)
{
    test_utils_reset_state();
    cmd_reset_event_sequence();

    const int N = 50;
    char ev[256];
    int last_seq = -1;
    int found = 0;

    // Issue N PAIR commands to generate PIN_REQUEST events
    for (int i = 0; i < N; ++i) {
        char mac[32];
        snprintf(mac, sizeof(mac), "11:22:33:44:55:%02x", i);
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "PAIR %s\r\n", mac);
        TEST_ASSERT_TRUE(test_send_serial_cmd(cmd));
    }

    // Collect N PIN_REQUEST events (with a small retry bound)
    int attempts = 0;
    while (found < N && attempts < N * 10) {
        if (test_capture_event(ev, sizeof(ev))) {
            if (strstr(ev, "PIN_REQUEST") != NULL) {
                int seq = parse_seq(ev);
                TEST_ASSERT_NOT_EQUAL(-1, seq);
                if (last_seq >= 0) TEST_ASSERT_TRUE(seq > last_seq);
                last_seq = seq;
                found++;
            }
        } else {
            // small delay to allow events to be produced in integration tests
            vTaskDelay(pdMS_TO_TICKS(10));
            attempts++;
        }
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(N, found, "Did not observe expected number of PIN_REQUEST events");

    // Now trigger a fresh pairing flow for a target device and submit PIN
    const char *target_mac = "11:22:33:44:55:00";
    char cmd[128];
    // Ensure there's a pending request for target_mac by issuing PAIR
    snprintf(cmd, sizeof(cmd), "PAIR %s\r\n", target_mac);
    TEST_ASSERT_TRUE(test_send_serial_cmd(cmd));

    // Expect the PIN_REQUEST for this target (drop any other events until seen)
    bool got_pin_request = false;
    attempts = 0;
    while (!got_pin_request && attempts < 50) {
        if (test_capture_event(ev, sizeof(ev))) {
            if (strstr(ev, "PIN_REQUEST") && strstr(ev, target_mac)) {
                got_pin_request = true;
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
            attempts++;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(got_pin_request, "Did not observe PIN_REQUEST for target device");

    // Submit the PIN which on-device should produce CONFIRM then SUCCESS
    snprintf(cmd, sizeof(cmd), "ENTER_PIN %s 123456\r\n", target_mac);
    TEST_ASSERT_TRUE(test_send_serial_cmd(cmd));

    bool saw_confirm = false, saw_success = false;
    int confirm_seq = -1, success_seq = -1;
    attempts = 0;
    while (!(saw_success) && attempts < 100) {
        if (test_capture_event(ev, sizeof(ev))) {
            if (strstr(ev, "CONFIRM") && strstr(ev, target_mac)) {
                confirm_seq = parse_seq(ev);
                if (confirm_seq != -1) {
                    TEST_ASSERT_TRUE_MESSAGE(confirm_seq > last_seq, "CONFIRM seq not greater than prior events");
                }
                saw_confirm = true;
            } else if (strstr(ev, "SUCCESS") && strstr(ev, target_mac)) {
                success_seq = parse_seq(ev);
                TEST_ASSERT_NOT_EQUAL_MESSAGE(-1, success_seq, "Could not parse SUCCESS seq");
                TEST_ASSERT_TRUE_MESSAGE(success_seq > last_seq, "SUCCESS seq not greater than prior events");
                saw_success = true;
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
            attempts++;
        }
    }

    TEST_ASSERT_TRUE_MESSAGE(saw_success, "Did not observe SUCCESS event after ENTER_PIN");
}

/* Register the test via constructor so the existing device test runner picks it up.
 * The test harness in this project discovers tests in translation units and
 * runs them via Unity; if explicit registration is required, adjust accordingly.
 */
__attribute__((constructor)) static void register_pairing_seq_test(void)
{
    extern void unity_register_test(const char *name, void (*f)(void));
    /* Some harnesses don't expose unity_register_test; if absent rely on
     * the default discovery mechanism. Attempt to register if available. */
    if (&unity_register_test) unity_register_test("test_pairing_device_seq_hardening", test_pairing_device_seq_hardening);
}
