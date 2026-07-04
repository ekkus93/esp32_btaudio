/* Unity tests for serial pairing commands: PAIR, CONFIRM_PIN, ENTER_PIN */
#include "unity.h"
#include "test_utils.h"
#include <ctype.h>
#include <string.h>
#include <stdbool.h>

// Helper: normalize an EVENT string by removing any trailing ",SEQ=..." suffix
// so tests tolerate the host-side annotated form `...,SEQ=<n>,TS=<ms>`.
static void normalize_event(const char *in, char *out, size_t out_len) {
    if (!in || !out || out_len == 0) return;
    /*
     * Tolerate host-side annotations that inject a sequence token into the
     * event payload, for example:
     *   "EVENT|PAIR|PIN_REQUEST|seq=12,11:22:33:44:55:66"
     * or
     *   "EVENT|PAIR|PIN_REQUEST|SEQ=12,11:22:33:44:55:66"
     *
     * We remove the "seq=..," (or "SEQ=..,") portion so tests can assert
     * on the canonical EVENT payload without losing the diagnostic token in
     * logs (which remains available to tools that parse raw output).
     */
    /* If the annotation is appended *after* the canonical event payload as
     * a comma-separated suffix (for example: "...,SEQ=0,TS=123456" or
     * "...,TS=123456"), it's simplest and safest to truncate the string at
     * the first occurrence of ",SEQ=" or ",TS=" so tests compare the
     * canonical payload only.
     */
    const char *suffix_seq = strstr(in, ",SEQ=");
    const char *suffix_seq_lc = strstr(in, ",seq=");
    const char *suffix_ts = strstr(in, ",TS=");
    const char *suffix_ts_lc = strstr(in, ",ts=");
    const char *suffix = NULL;
    if (suffix_seq && (!suffix || suffix_seq < suffix)) suffix = suffix_seq;
    if (suffix_seq_lc && (!suffix || suffix_seq_lc < suffix)) suffix = suffix_seq_lc;
    if (suffix_ts && (!suffix || suffix_ts < suffix)) suffix = suffix_ts;
    if (suffix_ts_lc && (!suffix || suffix_ts_lc < suffix)) suffix = suffix_ts_lc;

    if (suffix) {
        /* copy up to the comma that starts the annotation */
        size_t copy_len = (size_t)(suffix - in);
        if (copy_len >= out_len) copy_len = out_len - 1;
        memcpy(out, in, copy_len);
        out[copy_len] = '\0';
        return;
    }

    /*
     * Robust approach: find a canonical MAC address substring (format
     * "xx:xx:xx:xx:xx:xx") in the input and truncate the event to end
     * at the MAC. This handles cases where diagnostic tokens such as
     * "seq=.." or "TS=.." are inserted before or after the MAC.
     */
    const char *s = in;
    size_t in_len = strlen(in);
    const char *mac_end = NULL;

    for (size_t i = 0; i + 17 <= in_len; ++i) {
        /* check pattern XX:XX:XX:XX:XX:XX (17 chars) */
        bool ok = true;
        for (int part = 0; part < 6 && ok; ++part) {
            size_t pos = i + part * 3;
            /* two hex digits */
            if (!isxdigit((unsigned char)s[pos]) || !isxdigit((unsigned char)s[pos + 1])) {
                ok = false;
                break;
            }
            if (part < 5) {
                if (s[pos + 2] != ':') ok = false;
            }
        }
        if (ok) {
            mac_end = s + i + 17; /* position after the MAC */
            break;
        }
    }

    if (mac_end) {
        /*
         * The MAC may be preceded by diagnostic tokens inserted into the
         * last field, for example:
         *   "EVENT|PAIR|PIN_REQUEST|seq=12,11:22:33:44:55:66"
         * In that case copying from the start up to mac_end will include the
         * "seq=12," token. Instead, find the '|' character that begins the
         * last field and copy the header up to that pipe, then append the
         * canonical MAC. This yields the canonical payload
         *   "EVENT|PAIR|PIN_REQUEST|11:22:33:44:55:66"
         */
        const char *mac_start = mac_end - 17; /* start of MAC */
        /* find the last '|' before the MAC start */
        const char *last_pipe = NULL;
        for (const char *p = mac_start; p >= in; --p) {
            if (*p == '|') { last_pipe = p; break; }
            if (p == in) break; /* avoid underflow */
        }

        if (last_pipe && last_pipe >= in) {
            size_t header_len = (size_t)(last_pipe - in) + 1; /* include '|' */
            size_t mac_len = 17;
            /* If there's an immediate comma+digits after the MAC (PIN), include it. */
            const char *pin_start = mac_end; /* points right after MAC */
            const char *pin_end = pin_start;
            if (*pin_start == ',' && isdigit((unsigned char)*(pin_start + 1))) {
                /* consume comma and following digits */
                pin_end = pin_start + 1;
                while (pin_end < in + in_len && isdigit((unsigned char)*pin_end)) pin_end++;
            }
            size_t extra_len = (size_t)(pin_end - mac_end);
            size_t total = header_len + mac_len + extra_len;
            if (total >= out_len) {
                /* truncate appropriately */
                if (out_len == 0) return;
                size_t to_copy_header = (header_len < out_len - 1) ? header_len : out_len - 1;
                memcpy(out, in, to_copy_header);
                /* try to append as much of the MAC as fits */
                size_t remain = out_len - 1 - to_copy_header;
                if (remain > 0) {
                    /* copy from mac_start as much as fits (MAC + optional PIN) */
                    size_t to_copy_mac = (remain < (mac_len + extra_len)) ? remain : (mac_len + extra_len);
                    memcpy(out + to_copy_header, mac_start, to_copy_mac);
                }
                out[out_len - 1] = '\0';
                return;
            }
            /* copy header and canonical MAC */
            memcpy(out, in, header_len);
            memcpy(out + header_len, mac_start, mac_len);
            if (extra_len > 0) memcpy(out + header_len + mac_len, mac_end, extra_len);
            out[total] = '\0';
            return;
        }
        /* fallback: copy up to the end of the MAC address */
        size_t copy_len = (size_t)(mac_end - in);
        if (copy_len >= out_len) copy_len = out_len - 1;
        memcpy(out, in, copy_len);
        out[copy_len] = '\0';
        return;
    }

    /* fallback: no MAC found, copy whole string */
    strncpy(out, in, out_len - 1);
    out[out_len - 1] = '\0';
    return;
}

// Reuse helper that sends a serial command into the command parser and
// captures emitted EVENT lines injected into the test harness. Existing
// test_app provides `test_send_serial_cmd()` and `test_capture_event()`.

// The test helpers are implemented by the test harness; declare the
// prototypes here so this translation unit compiles even if the shared
// header is not available.
void test_utils_reset_state(void);
bool test_send_serial_cmd(const char *cmd);
bool test_capture_event(char *out_buf, size_t out_len);

// Per-suite setup registration: this project provides a global
// setUp/tearDown in `unity_config.c` and allows suites to register
// their own setup/teardown callbacks using unity_set_setup_function().
// Register a small wrapper that calls the test utils reset.
// Test-only function to reset sequence counter
extern void cmd_reset_event_sequence(void);

static void pairing_tests_setup(void) {
    test_utils_reset_state();
    cmd_reset_event_sequence();
}

__attribute__((constructor)) static void register_pairing_tests_setup(void) {
    // unity_set_setup_function is declared in unity_config.h
    extern void unity_set_setup_function(void (*setup)(void));
    unity_set_setup_function(pairing_tests_setup);
}

void test_pairing_commands_happy_path(void) {
    test_utils_reset_state();
    // Start pairing (device 11:22:33:44:55:66) — the stubs will simulate PIN request
    test_send_serial_cmd("PAIR 11:22:33:44:55:66\r\n");
    // Expect a PIN_REQUEST event
    char ev[128];
    TEST_ASSERT_TRUE(test_capture_event(ev, sizeof(ev)));
    char norm[256];
    normalize_event(ev, norm, sizeof(norm));
    TEST_ASSERT_EQUAL_STRING("EVENT|PAIR|PIN_REQUEST|11:22:33:44:55:66", norm);

    // Submit PIN
    test_send_serial_cmd("ENTER_PIN 11:22:33:44:55:66 123456\r\n");
    // Production event contract (bt_pairing_store.c): a PIN-flow pairing
    // emits PIN_REQUEST then SUCCESS on auth-complete. CONFIRM is the SSP
    // numeric-comparison event and never appears in the PIN flow (the old
    // CONFIRM|mac,pin expectation came from the retired emulation adapter).
    TEST_ASSERT_TRUE(test_capture_event(ev, sizeof(ev)));
    normalize_event(ev, norm, sizeof(norm));
    TEST_ASSERT_EQUAL_STRING("EVENT|PAIR|SUCCESS|11:22:33:44:55:66", norm);
}

void test_enter_pin_uses_default_when_missing(void) {
    test_utils_reset_state();
    // Simulate PAIR that requests PIN
    test_send_serial_cmd("PAIR 22:33:44:55:66:77\r\n");
    char ev[128];
    char norm[256];
    TEST_ASSERT_TRUE(test_capture_event(ev, sizeof(ev)));
    normalize_event(ev, norm, sizeof(norm));
    TEST_ASSERT_EQUAL_STRING("EVENT|PAIR|PIN_REQUEST|22:33:44:55:66:77", norm);

    // Call ENTER_PIN without providing a PIN — command handler should use NVS default PIN
    test_send_serial_cmd("ENTER_PIN 22:33:44:55:66:77\r\n");
    // Production contract: successful PIN submission ends in SUCCESS (no
    // CONFIRM in the PIN flow — see note in happy_path). SUCCESS still
    // proves the default PIN was resolved: with no PIN available the
    // handler errors out before submitting and no event is emitted.
    TEST_ASSERT_TRUE(test_capture_event(ev, sizeof(ev)));
    normalize_event(ev, norm, sizeof(norm));
    TEST_ASSERT_EQUAL_STRING("EVENT|PAIR|SUCCESS|22:33:44:55:66:77", norm);
}

void test_confirm_pin_without_pending_request_returns_error_event(void) {
    test_utils_reset_state();
    // Confirm for a device with no pending pairing should emit FAILED or no-op
    test_send_serial_cmd("CONFIRM_PIN AA:BB:CC:DD:EE:FF 123456\r\n");
    char ev[128];
    char norm[256];
    // Depending on implementation, either no event or FAILED
    bool got = test_capture_event(ev, sizeof(ev));
    if (got) {
        // If an event was produced, ensure it's a FAIL/FAILED event for that addr
        normalize_event(ev, norm, sizeof(norm));
        TEST_ASSERT_TRUE(strstr(norm, "FAILED") != NULL || strstr(norm, "FAIL") != NULL);
    } else {
        TEST_PASS_MESSAGE("No event produced (acceptable for no pending request)");
    }
}

// Register tests in test group (existing test harness picks up tests in files automatically)
// If explicit registration is needed, the existing main test runner will call these via Unity.
