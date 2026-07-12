/* test_cmd_handlers_debug.c — UT-3
 *
 * Drives every DEBUG subcommand through cmd_handle_debug() and asserts the emitted
 * response/event. cmd_handlers_debug.c had NO host test (38.5% func coverage: 8 of 13
 * handlers never invoked).
 *
 * Harness: links the real cmd_handlers_debug.c + commands_helpers.c (real cmd_safe_copy /
 * cmd_parse_log_level) and provides capture stubs for the weak cmd_send_response and for
 * cmd_send_event_pair, plus the g_cmd_mock_* globals (normally in cmd_handlers_bt.c, which
 * we deliberately do NOT link). No ESP_PLATFORM: the audio/beep/worker/probe handler bodies
 * are #ifdef'd out on host and compile only their #else ("UNSUPPORTED") branch — the tests
 * assert exactly that host-visible behavior. */
#include "unity.h"
#include "command_interface.h"
#include "cmd_handlers.h"
#include <string.h>
#include <stdbool.h>

/* --- globals normally owned by cmd_handlers_bt.c --- */
bool g_cmd_mock_enabled = false;
char g_cmd_mock_pairing_addr[32] = {0};
char g_cmd_mock_passkey[16] = {0};

/* --- capture the last response --- */
static char g_status[16];
static char g_cmd[16];
static char g_result[80];
static char g_data[160];
static bool g_data_was_null;
static int g_resp_count;

static char g_ev_sub[32];
static char g_ev_data[160];
static int g_ev_count;

cmd_status_t cmd_send_response(const char *status, const char *command,
                              const char *result, const char *data)
{
    snprintf(g_status, sizeof(g_status), "%s", status ? status : "");
    snprintf(g_cmd, sizeof(g_cmd), "%s", command ? command : "");
    snprintf(g_result, sizeof(g_result), "%s", result ? result : "");
    g_data_was_null = (data == NULL);
    snprintf(g_data, sizeof(g_data), "%s", data ? data : "");
    g_resp_count++;
    return CMD_SUCCESS;
}

cmd_status_t cmd_send_event_pair(const char *subtype, const char *data)
{
    snprintf(g_ev_sub, sizeof(g_ev_sub), "%s", subtype ? subtype : "");
    snprintf(g_ev_data, sizeof(g_ev_data), "%s", data ? data : "");
    g_ev_count++;
    return CMD_SUCCESS;
}

/* --- helpers --- */
static cmd_context_t g_ctx;

static void reset_capture(void)
{
    memset(g_status, 0, sizeof(g_status));
    memset(g_cmd, 0, sizeof(g_cmd));
    memset(g_result, 0, sizeof(g_result));
    memset(g_data, 0, sizeof(g_data));
    g_data_was_null = false;
    g_resp_count = 0;
    memset(g_ev_sub, 0, sizeof(g_ev_sub));
    memset(g_ev_data, 0, sizeof(g_ev_data));
    g_ev_count = 0;
}

static void set_params(int n, const char *a, const char *b, const char *c)
{
    memset(&g_ctx, 0, sizeof(g_ctx));
    g_ctx.param_count = n;
    if (n > 0 && a) snprintf(g_ctx.params[0], CMD_MAX_PARAM_LEN, "%s", a);
    if (n > 1 && b) snprintf(g_ctx.params[1], CMD_MAX_PARAM_LEN, "%s", b);
    if (n > 2 && c) snprintf(g_ctx.params[2], CMD_MAX_PARAM_LEN, "%s", c);
}

static void assert_resp(const char *status, const char *result)
{
    TEST_ASSERT_EQUAL_STRING(status, g_status);
    TEST_ASSERT_EQUAL_STRING("DEBUG", g_cmd);
    TEST_ASSERT_EQUAL_STRING(result, g_result);
}

void setUp(void)
{
    reset_capture();
    g_cmd_mock_enabled = false;
    g_cmd_mock_pairing_addr[0] = '\0';
    g_cmd_mock_passkey[0] = '\0';
}

void tearDown(void) {}

/* --- dispatcher --- */

void test_debug_missing_param(void)
{
    set_params(0, NULL, NULL, NULL);
    TEST_ASSERT_EQUAL_INT(CMD_SUCCESS, cmd_handle_debug(&g_ctx));
    assert_resp("ERR", "MISSING_PARAM");
}

void test_debug_unknown_subcmd(void)
{
    set_params(1, "NOPE", NULL, NULL);
    cmd_handle_debug(&g_ctx);
    assert_resp("ERR", "UNKNOWN_SUBCMD");
    TEST_ASSERT_EQUAL_STRING("NOPE", g_data);
}

void test_debug_dispatch_is_case_insensitive(void)
{
    set_params(1, "mock_on", NULL, NULL); /* lowercase → strcasecmp match */
    cmd_handle_debug(&g_ctx);
    assert_resp("OK", "MOCK_ON");
}

/* --- MOCK_ON / MOCK_ADD --- */

void test_debug_mock_on_sets_flag(void)
{
    set_params(1, "MOCK_ON", NULL, NULL);
    cmd_handle_debug(&g_ctx);
    assert_resp("OK", "MOCK_ON");
    TEST_ASSERT_TRUE(g_cmd_mock_enabled);
    TEST_ASSERT_TRUE(g_data_was_null);
}

void test_debug_mock_add_missing_arg(void)
{
    set_params(1, "MOCK_ADD", NULL, NULL);
    cmd_handle_debug(&g_ctx);
    assert_resp("ERR", "MOCK_ADD_MISSING");
    TEST_ASSERT_EQUAL_INT(0, g_ev_count);
}

void test_debug_mock_add_records_mac_and_emits_event(void)
{
    set_params(2, "MOCK_ADD", "AA:BB:CC:DD:EE:FF", NULL);
    cmd_handle_debug(&g_ctx);
    assert_resp("OK", "MOCK_ADD");
    TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FF", g_cmd_mock_pairing_addr);
    TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FF", g_data);
    /* an ADDED pair-event was emitted */
    TEST_ASSERT_EQUAL_INT(1, g_ev_count);
    TEST_ASSERT_EQUAL_STRING("ADDED", g_ev_sub);
    TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FF", g_ev_data);
}

void test_debug_mock_add_splits_mac_from_extra_params(void)
{
    /* extra params are joined with commas into payload; mac = text before first comma */
    set_params(3, "MOCK_ADD", "11:22:33:44:55:66", "SomeName");
    cmd_handle_debug(&g_ctx);
    assert_resp("OK", "MOCK_ADD");
    TEST_ASSERT_EQUAL_STRING("11:22:33:44:55:66", g_cmd_mock_pairing_addr);
}

/* --- MOCK_PAIR (host takes the #else MOCKED branch) --- */

void test_debug_mock_pair_missing_arg(void)
{
    set_params(1, "MOCK_PAIR", NULL, NULL);
    cmd_handle_debug(&g_ctx);
    assert_resp("ERR", "MOCK_PAIR_MISSING");
}

void test_debug_mock_pair_host_is_mocked(void)
{
    set_params(2, "MOCK_PAIR", "AA:BB:CC:DD:EE:FF", NULL);
    cmd_handle_debug(&g_ctx);
    assert_resp("OK", "MOCK_PAIR_MOCKED");
    TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FF", g_data);
}

/* --- ESP_PLATFORM-only handlers → UNSUPPORTED on host --- */

void test_debug_esp_only_handlers_unsupported_on_host(void)
{
    /* Each handler's #else response tags itself by name in the data field. */
    const char *subs[] = {"BEEP_DIAG", "WORKER_DIAG", "AUDIO_DIAG",
                          "AUDIO_DIAG_SUMMARY", "AUDIO_DIAG_PROBE",
                          "FORCE_BEEP", "DRAIN_QUEUE"};
    for (unsigned i = 0; i < sizeof(subs) / sizeof(subs[0]); ++i) {
        reset_capture();
        set_params(2, subs[i], "ON", NULL); /* arg ignored on host */
        cmd_handle_debug(&g_ctx);
        assert_resp("ERR", "UNSUPPORTED");
        TEST_ASSERT_EQUAL_STRING(subs[i], g_data);
    }
}

void test_debug_drain_ring_alias_maps_to_drain_queue(void)
{
    /* DRAIN_RING is a dispatch-table alias for the same handler as DRAIN_QUEUE,
     * so its host #else response reports "DRAIN_QUEUE". */
    set_params(1, "DRAIN_RING", NULL, NULL);
    cmd_handle_debug(&g_ctx);
    assert_resp("ERR", "UNSUPPORTED");
    TEST_ASSERT_EQUAL_STRING("DRAIN_QUEUE", g_data);
}

/* --- LOG (no ESP_PLATFORM guard; real cmd_parse_log_level) --- */

void test_debug_log_missing_args(void)
{
    set_params(2, "LOG", "TAG", NULL); /* need 3 params */
    cmd_handle_debug(&g_ctx);
    assert_resp("ERR", "LOG_MISSING");
}

void test_debug_log_bad_level(void)
{
    set_params(3, "LOG", "TAG", "LOUD");
    cmd_handle_debug(&g_ctx);
    assert_resp("ERR", "LOG_BAD_LEVEL");
    TEST_ASSERT_EQUAL_STRING("LOUD", g_data);
}

void test_debug_log_sets_level(void)
{
    set_params(3, "LOG", "myTag", "INFO");
    cmd_handle_debug(&g_ctx);
    assert_resp("OK", "LOG_SET");
    TEST_ASSERT_EQUAL_STRING("myTag:INFO", g_data);
}

/* --- DRAM (both branches host-visible) --- */

void test_debug_dram_missing_param(void)
{
    set_params(1, "DRAM", NULL, NULL);
    cmd_handle_debug(&g_ctx);
    assert_resp("ERR", "DRAM_MISSING_PARAM");
}

void test_debug_dram_on_off_mock(void)
{
    set_params(2, "DRAM", "ON", NULL);
    cmd_handle_debug(&g_ctx);
    assert_resp("OK", "DRAM_ON_MOCK");

    reset_capture();
    set_params(2, "DRAM", "0", NULL); /* "0" == OFF */
    cmd_handle_debug(&g_ctx);
    assert_resp("OK", "DRAM_OFF_MOCK");
}

void test_debug_dram_bad_param(void)
{
    set_params(2, "DRAM", "sideways", NULL);
    cmd_handle_debug(&g_ctx);
    assert_resp("ERR", "DRAM_BAD_PARAM");
    TEST_ASSERT_EQUAL_STRING("sideways", g_data);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_debug_missing_param);
    RUN_TEST(test_debug_unknown_subcmd);
    RUN_TEST(test_debug_dispatch_is_case_insensitive);
    RUN_TEST(test_debug_mock_on_sets_flag);
    RUN_TEST(test_debug_mock_add_missing_arg);
    RUN_TEST(test_debug_mock_add_records_mac_and_emits_event);
    RUN_TEST(test_debug_mock_add_splits_mac_from_extra_params);
    RUN_TEST(test_debug_mock_pair_missing_arg);
    RUN_TEST(test_debug_mock_pair_host_is_mocked);
    RUN_TEST(test_debug_esp_only_handlers_unsupported_on_host);
    RUN_TEST(test_debug_drain_ring_alias_maps_to_drain_queue);
    RUN_TEST(test_debug_log_missing_args);
    RUN_TEST(test_debug_log_bad_level);
    RUN_TEST(test_debug_log_sets_level);
    RUN_TEST(test_debug_dram_missing_param);
    RUN_TEST(test_debug_dram_on_off_mock);
    RUN_TEST(test_debug_dram_bad_param);
    return UNITY_END();
}
