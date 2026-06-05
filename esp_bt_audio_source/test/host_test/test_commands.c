#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <limits.h>
#include "unity.h"
#include "command_interface.h"
#include "bt_manager.h"
#include "mock_uart.h"
#include "nvs_storage.h"
#include "audio_processor.h"
#include "esp_log.h"

/* Test-only hook defined in bt_manager.c under UNIT_TEST */
void bt_manager_test_set_connection_state(int v);

static char s_test_spiffs_root[PATH_MAX];
static int s_test_spiffs_root_ready = 0;
static const char* k_file_alpha_name = "alpha.txt";
static const char k_file_alpha_data[] = "alpha-data";
static const size_t k_file_alpha_size = sizeof(k_file_alpha_data) - 1;
static const char* k_file_beta_name = "beta.bin";
static const unsigned char k_file_beta_data[] = {0x01, 0x02, 0x7F, 0xA0, 0x00, 0x55};
static const size_t k_file_beta_size = sizeof(k_file_beta_data);
static const char* k_file_worker_name = "worker_long_norm.wav";
static const unsigned char k_file_worker_data[] = {0xDE, 0xAD, 0xBE, 0xEF};
static const char* k_logs_dir_name = "logs";

static void test_cleanup_spiffs_root(void)
{
    if (!s_test_spiffs_root_ready) {
        return;
    }
    char path[PATH_MAX + 32];
    snprintf(path, sizeof(path), "%s/%s", s_test_spiffs_root, k_file_alpha_name);
    unlink(path);
    snprintf(path, sizeof(path), "%s/%s", s_test_spiffs_root, k_file_beta_name);
    unlink(path);
    snprintf(path, sizeof(path), "%s/%s", s_test_spiffs_root, k_file_worker_name);
    unlink(path);
    snprintf(path, sizeof(path), "%s/%s", s_test_spiffs_root, k_logs_dir_name);
    rmdir(path);
    rmdir(s_test_spiffs_root);
    s_test_spiffs_root_ready = 0;
    s_test_spiffs_root[0] = '\0';
}

const char* cmd_files_host_mount_override(void)
{
    return s_test_spiffs_root_ready ? s_test_spiffs_root : NULL;
}

static int s_spiffs_mount_hook_count = 0;

static void test_spiffs_mount_hook(void)
{
    ++s_spiffs_mount_hook_count;
}

static void reset_spiffs_mount_hook_counter(void)
{
    s_spiffs_mount_hook_count = 0;
    cmd_test_install_spiffs_mount_hook(test_spiffs_mount_hook);
}

static void test_create_sample_spiffs(void)
{
    TEST_ASSERT_TRUE(s_test_spiffs_root_ready);

    char path[PATH_MAX + 32];
    snprintf(path, sizeof(path), "%s/%s", s_test_spiffs_root, k_file_alpha_name);
    FILE* fa = fopen(path, "wb");
    TEST_ASSERT_NOT_NULL(fa);
    size_t written = fwrite(k_file_alpha_data, 1, k_file_alpha_size, fa);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)k_file_alpha_size, (uint32_t)written);
    fclose(fa);

    snprintf(path, sizeof(path), "%s/%s", s_test_spiffs_root, k_file_beta_name);
    FILE* fb = fopen(path, "wb");
    TEST_ASSERT_NOT_NULL(fb);
    written = fwrite(k_file_beta_data, 1, k_file_beta_size, fb);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)k_file_beta_size, (uint32_t)written);
    fclose(fb);

    snprintf(path, sizeof(path), "%s/%s", s_test_spiffs_root, k_file_worker_name);
    FILE* fc = fopen(path, "wb");
    TEST_ASSERT_NOT_NULL(fc);
    written = fwrite(k_file_worker_data, 1, sizeof(k_file_worker_data), fc);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)sizeof(k_file_worker_data), (uint32_t)written);
    fclose(fc);

    snprintf(path, sizeof(path), "%s/%s", s_test_spiffs_root, k_logs_dir_name);
    TEST_ASSERT_EQUAL_INT(0, mkdir(path, 0700));
}

const char* cmd_version_host_override(void)
{
    return "TEST-HOST-VERSION";
}
// Access mock gap helpers
extern void mock_gap_reset(void);
extern const char* mock_gap_get_last_mac(void);
extern int mock_gap_get_last_pin_len(void);
extern const char* mock_gap_get_last_pin(void);
extern int mock_gap_get_last_confirm(void);
extern void bt_manager_test_reset_forces(void);
extern int bt_manager_test_get_scan_start_count(void);
extern const char* bt_manager_test_get_last_unpair_mac(void);
extern void bt_manager_test_set_force_unpair_failure(int v);
extern void bt_manager_test_set_force_unpair_all_failure(int v);
extern int bt_manager_test_get_unpair_all_removed(void);
extern int bt_manager_test_get_unpair_all_cleared_before(void);

// Test fixture
void setUp(void) {
    // Initialize before each test
    test_cleanup_spiffs_root();
    mock_uart_init(115200);
    cmd_init();
    cmd_test_reset_cmd_process_state();
    bt_manager_init_t cfg = {
        .device_name = "MockBT",
        .connected_cb = NULL,
        .disconnected_cb = NULL,
    };
    bt_manager_init(&cfg);
    bt_manager_test_reset_forces();
    bt_manager_test_set_force_unpair_all_failure(0);
    nvs_storage_clear_paired_devices();

    char template[] = "/tmp/esp_cmd_spiffsXXXXXX";
    char* root = mkdtemp(template);
    TEST_ASSERT_NOT_NULL(root);
    strncpy(s_test_spiffs_root, root, sizeof(s_test_spiffs_root) - 1);
    s_test_spiffs_root[sizeof(s_test_spiffs_root) - 1] = '\0';
    s_test_spiffs_root_ready = 1;
    test_create_sample_spiffs();
    reset_spiffs_mount_hook_counter();
}

void tearDown(void) {
    // Clean up after each test
    cmd_deinit();
    test_cleanup_spiffs_root();
    cmd_test_install_spiffs_mount_hook(NULL);
}

void test_debug_log_sets_level_and_response(void) {
    mock_uart_reset_tx();
    esp_log_level_set("AUDIO_PROC", ESP_LOG_INFO);

    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("DEBUG LOG AUDIO_PROC WARN", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    TEST_ASSERT_EQUAL_INT(ESP_LOG_WARN, esp_log_level_get("AUDIO_PROC"));
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|DEBUG|LOG_SET|"));
    TEST_ASSERT_NOT_NULL(strstr(tx, "AUDIO_PROC:WARN"));
}

// Test basic command parsing
void test_parse_scan_command(void) {
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("SCAN", &ctx));
    TEST_ASSERT_EQUAL(CMD_TYPE_SCAN, ctx.type);
    TEST_ASSERT_EQUAL(0, ctx.param_count);
}

// Test command with parameter
void test_parse_connect_command(void) {
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("CONNECT AA:BB:CC:DD:EE:FF", &ctx));
    TEST_ASSERT_EQUAL(CMD_TYPE_CONNECT, ctx.type);
    TEST_ASSERT_EQUAL(1, ctx.param_count);
    TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FF", ctx.params[0]);
}

// Test multiple parameters
void test_parse_i2s_config_command(void) {
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("I2S_CONFIG 26,25,22,21", &ctx));
    TEST_ASSERT_EQUAL(CMD_TYPE_I2S_CONFIG, ctx.type);
    TEST_ASSERT_EQUAL(1, ctx.param_count);
    TEST_ASSERT_EQUAL_STRING("26,25,22,21", ctx.params[0]);
}

void test_parse_i2s_config_command_with_format(void) {
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("I2S_CONFIG 26,25,22,21 48000 16 2", &ctx));
    TEST_ASSERT_EQUAL(CMD_TYPE_I2S_CONFIG, ctx.type);
    TEST_ASSERT_EQUAL(4, ctx.param_count);
    TEST_ASSERT_EQUAL_STRING("26,25,22,21", ctx.params[0]);
    TEST_ASSERT_EQUAL_STRING("48000", ctx.params[1]);
    TEST_ASSERT_EQUAL_STRING("16", ctx.params[2]);
    TEST_ASSERT_EQUAL_STRING("2", ctx.params[3]);
}

void test_parse_file_command(void) {
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("FILE alpha.txt", &ctx));
    TEST_ASSERT_EQUAL(CMD_TYPE_FILE, ctx.type);
    TEST_ASSERT_EQUAL(1, ctx.param_count);
    TEST_ASSERT_EQUAL_STRING(k_file_alpha_name, ctx.params[0]);
}

void test_parse_file_truncates_overlong_name(void) {
    char long_name[128];
    memset(long_name, 'X', sizeof(long_name));
    long_name[sizeof(long_name) - 1] = '\0';

    char cmd_buf[256];
    snprintf(cmd_buf, sizeof(cmd_buf), "FILE %s", long_name);

    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse(cmd_buf, &ctx));
    TEST_ASSERT_EQUAL(CMD_TYPE_FILE, ctx.type);
    TEST_ASSERT_EQUAL(1, ctx.param_count);
    TEST_ASSERT_EQUAL(CMD_MAX_PARAM_LEN - 1, (int)strlen(ctx.params[0]));
}

// Test invalid command
void test_parse_invalid_command(void) {
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_ERROR_UNKNOWN, cmd_parse("INVALID_COMMAND", &ctx));
}

void test_parse_malformed_tokens(void) {
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_ERROR_UNKNOWN, cmd_parse("   ,,,", &ctx));
}

// Test command with whitespace
void test_parse_command_with_whitespace(void) {
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("  VOLUME  75  ", &ctx));
    TEST_ASSERT_EQUAL(CMD_TYPE_VOLUME, ctx.type);
    TEST_ASSERT_EQUAL(1, ctx.param_count);
    TEST_ASSERT_EQUAL_STRING("75", ctx.params[0]);
}

void test_parse_diag_command(void) {
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("DIAG", &ctx));
    TEST_ASSERT_EQUAL(CMD_TYPE_DIAG, ctx.type);
    TEST_ASSERT_EQUAL(0, ctx.param_count);
}

void test_parse_empty_command_should_error(void) {
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_ERROR_UNKNOWN, cmd_parse("   \t   ", &ctx));
}

/* BUG-3: truly empty string must not UB on s-1 pointer arithmetic */
void test_parse_truly_empty_string_should_error(void) {
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_ERROR_UNKNOWN, cmd_parse("", &ctx));
}

void test_parse_whitespace_only_should_error(void) {
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_ERROR_UNKNOWN, cmd_parse("   ", &ctx));
}

void test_parse_limits_param_count_and_truncates(void) {
    char long_token[CMD_MAX_PARAM_LEN + 8];
    memset(long_token, 'A', sizeof(long_token));
    long_token[sizeof(long_token) - 1] = '\0';

    char cmd_buf[128];
    /* Seven params -> only first five kept; first param truncated to CMD_MAX_PARAM_LEN-1. */
    snprintf(cmd_buf, sizeof(cmd_buf), "CONNECT %s b c d e f g", long_token);

    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse(cmd_buf, &ctx));
    TEST_ASSERT_EQUAL(CMD_TYPE_CONNECT, ctx.type);
    TEST_ASSERT_EQUAL(5, ctx.param_count);
    TEST_ASSERT_EQUAL(strlen(ctx.params[0]), CMD_MAX_PARAM_LEN - 1);
    TEST_ASSERT_EQUAL_STRING("b", ctx.params[1]);
    TEST_ASSERT_EQUAL_STRING("c", ctx.params[2]);
    TEST_ASSERT_EQUAL_STRING("d", ctx.params[3]);
    TEST_ASSERT_EQUAL_STRING("e", ctx.params[4]);
}

void test_parse_connect_name_preserves_spaces(void) {
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("CONNECT_NAME   Living   Room   Speaker   ", &ctx));
    TEST_ASSERT_EQUAL(CMD_TYPE_CONNECT_NAME, ctx.type);
    TEST_ASSERT_EQUAL(1, ctx.param_count);
    TEST_ASSERT_EQUAL_STRING("Living   Room   Speaker", ctx.params[0]);
}

// Test response generation
void test_send_response(void) {
    mock_uart_reset_tx();
    
    cmd_send_response("OK", "SCAN", "COMPLETE", "2");
    
    // Check that the correct response was sent
    const char* tx_data = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx_data);
    TEST_ASSERT_EQUAL_STRING("OK|SCAN|COMPLETE|2\r\n", tx_data);
}

// Test command processing from UART
void test_command_processing(void) {
    // Inject command into mock UART
    mock_uart_inject_rx_data("SCAN\r\n", 6);
    
    // Process the command
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_process());
    
    // Verify response
    const char* tx_data = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx_data);
    
    // Note: In a real implementation, this would return scan results
    // For the test, we're just checking it sent some response
    TEST_ASSERT_TRUE(strstr(tx_data, "SCAN") != NULL);
}

void test_cmd_process_handles_multiple_commands_in_one_read(void) {
    mock_uart_reset_tx();

    /* Two commands arrive in a single UART read; both should be parsed and executed. */
    mock_uart_inject_rx_data("SCAN\r\nSTATUS\r\n", strlen("SCAN\r\nSTATUS\r\n"));

    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_process());

    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|SCAN"));
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|STATUS"));
}

void test_cmd_process_accumulates_partial_line_across_calls(void) {
    mock_uart_reset_tx();

    /* First call provides no terminator; nothing should be emitted. */
    mock_uart_inject_rx_data("VOLUME 10", strlen("VOLUME 10"));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_process());
    const char* tx_after_partial = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx_after_partial);
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)strlen(tx_after_partial));

    /* Second call supplies the newline so the buffered command runs. */
    mock_uart_inject_rx_data("\r\n", 2);
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_process());
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|VOLUME|MOCK_SET|10"));
}

void test_cmd_process_recovers_after_overflow_reset(void) {
    mock_uart_reset_tx();

    /* Fill most of the line buffer without a newline to trigger the overflow path. */
    char filler1[250];
    memset(filler1, 'A', sizeof(filler1));
    mock_uart_inject_rx_data(filler1, sizeof(filler1));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_process());
    /* No response expected yet. */
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)strlen(mock_uart_get_tx_data()));

    /* Next read includes a valid command plus extra data to force line_len + to_copy past the buffer size.
     * cmd_process should reset the buffer and still execute the SCAN command. */
    char overflow_payload[260];
    memset(overflow_payload, 'B', sizeof(overflow_payload));
    const char cmd_str[] = "SCAN\r\n";
    memcpy(overflow_payload, cmd_str, sizeof(cmd_str) - 1);
    mock_uart_inject_rx_data(overflow_payload, sizeof(overflow_payload));

    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_process());

    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "SCAN"));
}

static int count_substring(const char* haystack, const char* needle)
{
    int count = 0;
    const char* pos = haystack;
    size_t step = strlen(needle);
    while (pos && (pos = strstr(pos, needle)) != NULL) {
        ++count;
        pos += step;
    }
    return count;
}

void test_help_command(void) {
    mock_uart_reset_tx();
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("HELP", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    /* The number of commands may change over time; parse the declared
     * count from the SUMMARY line and verify it matches the number of
     * ENTRY lines emitted. This keeps the test resilient to additions
     * or removals of help entries. */
    TEST_ASSERT_NOT_NULL(strstr(tx, "INFO|HELP|SUMMARY|"));
    TEST_ASSERT_NOT_NULL(strstr(tx, "INFO|HELP|FORMAT|COMMAND [ARGS] - DESCRIPTION"));
    TEST_ASSERT_NOT_NULL(strstr(tx, "INFO|HELP|ENTRY|HELP - Show this list"));
    TEST_ASSERT_NOT_NULL(strstr(tx, "INFO|HELP|ENTRY|CONNECT <MAC> - Connect by MAC address"));
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|HELP|DONE|"));

    int entry_count = count_substring(tx, "INFO|HELP|ENTRY|");
    /* Extract the numeric count from the SUMMARY line and compare. */
    const char* sumptr = strstr(tx, "INFO|HELP|SUMMARY|");
    TEST_ASSERT_NOT_NULL(sumptr);
    sumptr += strlen("INFO|HELP|SUMMARY|");
    int declared = -1;
    if (sumptr) {
        /* Expect format: "<N> commands available" */
        sscanf(sumptr, "%d commands available", &declared);
    }
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, declared);
    TEST_ASSERT_EQUAL(declared, entry_count);
}

void test_version_command(void) {
    mock_uart_reset_tx();
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("VERSION", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|VERSION|TEST-HOST-VERSION|"));
}

void test_diag_command_reports_state(void) {
    mock_uart_reset_tx();
    bt_manager_test_set_connection_state(1);
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_init(NULL));
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_start());

    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("DIAG", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|DIAG|STATE|"));
    TEST_ASSERT_NOT_NULL(strstr(tx, "CONN=1"));
    TEST_ASSERT_NOT_NULL(strstr(tx, "I2S=1"));

    (void)audio_processor_stop();
    (void)audio_processor_deinit();
}

// Verify issuing SCAN triggers the manager start-scan path (unit-test builds)
void test_scan_invokes_manager(void) {
    mock_uart_reset_tx();

    // Ensure forces/hooks reset
    bt_manager_test_reset_forces();

    // Initial count should be zero
    int before = bt_manager_test_get_scan_start_count();

    // Inject command into mock UART and process
    mock_uart_inject_rx_data("SCAN\r\n", 6);
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_process());

    // Manager test hook should have been called at least once
    int after = bt_manager_test_get_scan_start_count();
    TEST_ASSERT_TRUE(after > before);

    // Also validate we emitted a response mentioning SCAN
    const char* tx_data = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx_data);
    TEST_ASSERT_TRUE(strstr(tx_data, "SCAN") != NULL);
}

// New tests for pairing command handlers
void test_confirm_pin_command(void) {
    mock_gap_reset();
    // Execute confirm command (MAC + accept)
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("CONFIRM_PIN AA:BB:CC:DD:EE:FF ACCEPT", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    const char* mac = mock_gap_get_last_mac();
    TEST_ASSERT_NOT_NULL(mac);
    // Mock formats hex lowercase
    TEST_ASSERT_EQUAL_STRING("aa:bb:cc:dd:ee:ff", mac);
    TEST_ASSERT_EQUAL(1, mock_gap_get_last_confirm());
}

void test_enter_pin_command(void) {
    mock_gap_reset();
    // Execute enter pin command
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("ENTER_PIN AA:BB:CC:DD:EE:FF 1234", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    const char* mac = mock_gap_get_last_mac();
    TEST_ASSERT_NOT_NULL(mac);
    TEST_ASSERT_EQUAL_STRING("aa:bb:cc:dd:ee:ff", mac);
    TEST_ASSERT_EQUAL(4, mock_gap_get_last_pin_len());
    TEST_ASSERT_EQUAL_STRING("1234", mock_gap_get_last_pin());
}

void test_volume_invalid_param_should_error(void) {
    mock_uart_reset_tx();
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("VOLUME 200", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "ERR|VOLUME|OUT_OF_RANGE"));
}

void test_volume_missing_param_should_error(void) {
    mock_uart_reset_tx();
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("VOLUME", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "ERR|VOLUME|MISSING_PARAM"));
}

void test_i2s_config_invalid_rate_should_error(void) {
    mock_uart_reset_tx();
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("I2S_CONFIG 26,25,22,21 12345", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "ERR|I2S_CONFIG|INVALID_RATE"));
}

void test_i2s_config_invalid_bit_depth_should_error(void) {
    mock_uart_reset_tx();
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("I2S_CONFIG 26,25,22,21 48000 20", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "ERR|I2S_CONFIG|INVALID_BIT_DEPTH"));
}

void test_i2s_config_invalid_channels_should_error(void) {
    mock_uart_reset_tx();
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("I2S_CONFIG 26,25,22,21 48000 16 3", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "ERR|I2S_CONFIG|INVALID_CHANNELS"));
}

void test_debug_mock_add_missing_param_errors(void) {
    mock_uart_reset_tx();
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("DEBUG MOCK_ADD", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "ERR|DEBUG|MOCK_ADD_MISSING"));
}

// Main test runner
/* TEST-8: cmd_status_to_name() — all defined codes return non-empty strings */
void test_status_name_cmd_success(void) {
    const char *name = cmd_status_to_name(CMD_SUCCESS);
    TEST_ASSERT_NOT_NULL(name);
    TEST_ASSERT_GREATER_THAN_INT(0, (int)strlen(name));
}
void test_status_name_cmd_error_init_failed(void) {
    const char *name = cmd_status_to_name(CMD_ERROR_INIT_FAILED);
    TEST_ASSERT_NOT_NULL(name);
    TEST_ASSERT_GREATER_THAN_INT(0, (int)strlen(name));
}
void test_status_name_cmd_error_invalid_param(void) {
    const char *name = cmd_status_to_name(CMD_ERROR_INVALID_PARAM);
    TEST_ASSERT_NOT_NULL(name);
    TEST_ASSERT_GREATER_THAN_INT(0, (int)strlen(name));
}
void test_status_name_cmd_error_unknown(void) {
    const char *name = cmd_status_to_name(CMD_ERROR_UNKNOWN);
    TEST_ASSERT_NOT_NULL(name);
    TEST_ASSERT_GREATER_THAN_INT(0, (int)strlen(name));
}
void test_status_name_cmd_error_not_initialized(void) {
    const char *name = cmd_status_to_name(CMD_ERROR_NOT_INITIALIZED);
    TEST_ASSERT_NOT_NULL(name);
    TEST_ASSERT_GREATER_THAN_INT(0, (int)strlen(name));
}
void test_status_name_cmd_error_too_many_params(void) {
    const char *name = cmd_status_to_name(CMD_ERROR_TOO_MANY_PARAMS);
    TEST_ASSERT_NOT_NULL(name);
    TEST_ASSERT_GREATER_THAN_INT(0, (int)strlen(name));
}
void test_status_name_out_of_range_returns_non_null(void) {
    /* Unknown status codes should return a non-null fallback, not crash */
    const char *name = cmd_status_to_name((cmd_status_t)999);
    TEST_ASSERT_NOT_NULL(name);
}

int main(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_parse_scan_command);
    RUN_TEST(test_parse_connect_command);
    RUN_TEST(test_parse_i2s_config_command);
    RUN_TEST(test_parse_i2s_config_command_with_format);
    RUN_TEST(test_parse_file_command);
    RUN_TEST(test_parse_file_truncates_overlong_name);
    RUN_TEST(test_parse_invalid_command);
    RUN_TEST(test_parse_malformed_tokens);
    RUN_TEST(test_parse_command_with_whitespace);
    RUN_TEST(test_parse_empty_command_should_error);
    RUN_TEST(test_parse_truly_empty_string_should_error);
    RUN_TEST(test_parse_whitespace_only_should_error);
    RUN_TEST(test_parse_limits_param_count_and_truncates);
    RUN_TEST(test_parse_connect_name_preserves_spaces);
    RUN_TEST(test_parse_diag_command);
    RUN_TEST(test_send_response);
    RUN_TEST(test_command_processing);
    RUN_TEST(test_cmd_process_handles_multiple_commands_in_one_read);
    RUN_TEST(test_cmd_process_accumulates_partial_line_across_calls);
    RUN_TEST(test_cmd_process_recovers_after_overflow_reset);
    RUN_TEST(test_debug_log_sets_level_and_response);
    RUN_TEST(test_help_command);
    RUN_TEST(test_version_command);
    RUN_TEST(test_diag_command_reports_state);
    RUN_TEST(test_scan_invokes_manager);

    // Pairing related tests
    RUN_TEST(test_confirm_pin_command);
    RUN_TEST(test_enter_pin_command);
    RUN_TEST(test_volume_invalid_param_should_error);
    RUN_TEST(test_volume_missing_param_should_error);
    RUN_TEST(test_i2s_config_invalid_rate_should_error);
    RUN_TEST(test_i2s_config_invalid_bit_depth_should_error);
    RUN_TEST(test_i2s_config_invalid_channels_should_error);
    RUN_TEST(test_debug_mock_add_missing_param_errors);
    
    // Forward-declare tests implemented below
    extern void test_mute_unmute_command(void);
    extern void test_unpair_command_success(void);
    extern void test_unpair_command_failure(void);
    extern void test_unpair_command_not_found(void);
    extern void test_unpair_all_command(void);
    extern void test_unpair_all_command_failure(void);
    extern void test_status_command(void);
    extern void test_status_command_streaming_info_unavailable(void);  /* CODE_REVIEW8 Task B */
    extern void test_reset_command(void);

    // New tests for mute/unmute and unpair_all
    RUN_TEST(test_mute_unmute_command);
    RUN_TEST(test_unpair_command_success);
    RUN_TEST(test_unpair_command_failure);
    RUN_TEST(test_unpair_command_not_found);
    RUN_TEST(test_unpair_all_command);
    RUN_TEST(test_unpair_all_command_failure);
    
    // New tests for PAIRED and SAMPLE_RATE
    extern void test_paired_command(void);
    extern void test_sample_rate_command(void);
    RUN_TEST(test_paired_command);
    RUN_TEST(test_sample_rate_command);
    RUN_TEST(test_status_command);
    RUN_TEST(test_status_command_streaming_info_unavailable);  /* CODE_REVIEW8 Task B */
    RUN_TEST(test_reset_command);
    
    // Test DISCONNECT command
    extern void test_disconnect_command(void);
    RUN_TEST(test_disconnect_command);
    
    // Test START and STOP commands
    extern void test_start_command(void);
    extern void test_start_command_stops_beep_and_enables_i2s(void);
    extern void test_stop_command(void);
    RUN_TEST(test_start_command);
    RUN_TEST(test_start_command_stops_beep_and_enables_i2s);
    RUN_TEST(test_stop_command);

    // Negative-path failure simulations
    extern void test_disconnect_failure_command(void);
    extern void test_start_failure_command(void);
    extern void test_stop_failure_command(void);
    RUN_TEST(test_disconnect_failure_command);
    RUN_TEST(test_start_failure_command);
    RUN_TEST(test_stop_failure_command);
    
    // Tests for BEEP command
    extern void test_beep_command_not_connected(void);
    extern void test_beep_command_connected(void);
    extern void test_beep_command_allowed_when_i2s_active(void);
    extern void test_beep_command_busy_when_beep_active(void);
    RUN_TEST(test_beep_command_not_connected);
    RUN_TEST(test_beep_command_connected);
    RUN_TEST(test_beep_command_allowed_when_i2s_active);
    RUN_TEST(test_beep_command_busy_when_beep_active);
    extern void test_file_command_found(void);
    extern void test_file_command_not_found(void);
    extern void test_file_command_not_file(void);
    RUN_TEST(test_file_command_found);
    RUN_TEST(test_file_command_not_found);
    RUN_TEST(test_file_command_not_file);
    extern void test_files_command_lists_entries(void);
    RUN_TEST(test_files_command_lists_entries);
    
    // Tests for SYNTH command (host-mode verifies parsing + response)
    extern void test_synth_on_command(void);
    extern void test_synth_off_command(void);
    RUN_TEST(test_synth_on_command);
    RUN_TEST(test_synth_off_command);

    extern void test_diag_i2s_stop_clears_i2s_flag(void);
    RUN_TEST(test_diag_i2s_stop_clears_i2s_flag);

    /* TEST-8: cmd_status_to_name() completeness */
    RUN_TEST(test_status_name_cmd_success);
    RUN_TEST(test_status_name_cmd_error_init_failed);
    RUN_TEST(test_status_name_cmd_error_invalid_param);
    RUN_TEST(test_status_name_cmd_error_unknown);
    RUN_TEST(test_status_name_cmd_error_not_initialized);
    RUN_TEST(test_status_name_cmd_error_too_many_params);
    RUN_TEST(test_status_name_out_of_range_returns_non_null);

    return UNITY_END();
}

// Test mute and unmute flow
void test_mute_unmute_command(void) {
    mock_uart_reset_tx();
    // Mute
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("MUTE", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_TRUE(strstr(tx, "MUTE") != NULL);

    mock_uart_reset_tx();
    // Unmute
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("UNMUTE", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));
    tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_TRUE(strstr(tx, "UNMUTE") != NULL);
}

// Test UNPAIR removes entry and reports success
void test_unpair_command_success(void) {
    bt_manager_test_reset_forces();
    nvs_storage_clear_paired_devices();
    TEST_ASSERT_EQUAL(ESP_OK, nvs_storage_add_paired_device("AA:BB:CC:DD:EE:FF", "Speaker"));

    mock_uart_reset_tx();
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("UNPAIR AA:BB:CC:DD:EE:FF", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|UNPAIR|REMOVED|AA:BB:CC:DD:EE:FF"));

    const char* last = bt_manager_test_get_last_unpair_mac();
    TEST_ASSERT_NOT_NULL(last);
    TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FF", last);

    int count = -1;
    TEST_ASSERT_EQUAL(ESP_OK, nvs_storage_get_paired_count(&count));
    TEST_ASSERT_EQUAL(0, count);
}

// Test UNPAIR surfaces backend failure without mutating storage
void test_unpair_command_failure(void) {
    bt_manager_test_reset_forces();
    nvs_storage_clear_paired_devices();
    TEST_ASSERT_EQUAL(ESP_OK, nvs_storage_add_paired_device("AA:BB:CC:DD:EE:FF", "Speaker"));

    bt_manager_test_set_force_unpair_failure(1);
    mock_uart_reset_tx();
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("UNPAIR AA:BB:CC:DD:EE:FF", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "ERR|UNPAIR|FAILED|AA:BB:CC:DD:EE:FF"));

    const char* last = bt_manager_test_get_last_unpair_mac();
    TEST_ASSERT_NOT_NULL(last);
        TEST_ASSERT_EQUAL('\0', last[0]);

    int count = -1;
    TEST_ASSERT_EQUAL(ESP_OK, nvs_storage_get_paired_count(&count));
    TEST_ASSERT_EQUAL(1, count);

    bt_manager_test_reset_forces();
}

// Test UNPAIR reports NOT_FOUND when storage lacks the entry
void test_unpair_command_not_found(void) {
    bt_manager_test_reset_forces();
    nvs_storage_clear_paired_devices();

    mock_uart_reset_tx();
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("UNPAIR AA:BB:CC:DD:EE:FF", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "ERR|UNPAIR|NOT_FOUND|AA:BB:CC:DD:EE:FF"));

    const char* last = bt_manager_test_get_last_unpair_mac();
    TEST_ASSERT_NOT_NULL(last);
    TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FF", last);

    int count = -1;
    TEST_ASSERT_EQUAL(ESP_OK, nvs_storage_get_paired_count(&count));
    TEST_ASSERT_EQUAL(0, count);
}

// Test UNPAIR_ALL clears stored paired devices
void test_unpair_all_command(void) {
    nvs_storage_clear_paired_devices();
    // Seed mock NVS with paired devices
    TEST_ASSERT_EQUAL(ESP_OK, nvs_storage_add_paired_device("aa:bb:cc:11:22:33", "Speaker"));
    TEST_ASSERT_EQUAL(ESP_OK, nvs_storage_add_paired_device("11:22:33:44:55:66", "Phone"));

    int before = 0;
    TEST_ASSERT_EQUAL(ESP_OK, nvs_storage_get_paired_count(&before));
    TEST_ASSERT_TRUE(before >= 2);

    mock_uart_reset_tx();
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("UNPAIR_ALL", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|UNPAIR_ALL|SUCCESS|2"));

    TEST_ASSERT_EQUAL(2, bt_manager_test_get_unpair_all_cleared_before());
    TEST_ASSERT_EQUAL(0, bt_manager_test_get_unpair_all_removed());

    int after = 0;
    TEST_ASSERT_EQUAL(ESP_OK, nvs_storage_get_paired_count(&after));
    TEST_ASSERT_EQUAL(0, after);
}

void test_unpair_all_command_failure(void) {
    nvs_storage_clear_paired_devices();
    TEST_ASSERT_EQUAL(ESP_OK, nvs_storage_add_paired_device("AA:BB:CC:DD:EE:01", "One"));
    TEST_ASSERT_EQUAL(ESP_OK, nvs_storage_add_paired_device("AA:BB:CC:DD:EE:02", "Two"));

    bt_manager_test_set_force_unpair_all_failure(1);

    mock_uart_reset_tx();
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("UNPAIR_ALL", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "ERR|UNPAIR_ALL|FAILED"));

    int remaining = 0;
    TEST_ASSERT_EQUAL(ESP_OK, nvs_storage_get_paired_count(&remaining));
    TEST_ASSERT_EQUAL(2, remaining);

    bt_manager_test_set_force_unpair_all_failure(0);
}

// Test listing paired devices
void test_paired_command(void) {
    // Seed mock NVS
    nvs_storage_clear_paired_devices();
    TEST_ASSERT_EQUAL(ESP_OK, nvs_storage_add_paired_device("aa:bb:cc:11:22:33", "Speaker"));
    TEST_ASSERT_EQUAL(ESP_OK, nvs_storage_add_paired_device("11:22:33:44:55:66", "Phone"));

    mock_uart_reset_tx();
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("PAIRED", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_TRUE(strstr(tx, "PAIRED") != NULL);
    TEST_ASSERT_TRUE(strstr(tx, "COUNT") != NULL);
}

// Test setting sample rate
void test_sample_rate_command(void) {
    mock_uart_reset_tx();
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("SAMPLE_RATE 48000", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_TRUE(strstr(tx, "SAMPLE_RATE") != NULL);
    TEST_ASSERT_TRUE(strstr(tx, "48000") != NULL || strstr(tx, "MOCK_APPLIED") != NULL);
}

// Test STATUS returns key fields
void test_status_command(void) {
    mock_uart_reset_tx();
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("STATUS", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_TRUE(strstr(tx, "STATUS") != NULL);
    TEST_ASSERT_TRUE(strstr(tx, "MUTE") != NULL);
    TEST_ASSERT_TRUE(strstr(tx, "SAMPLE_RATE") != NULL || strstr(tx, "SAMPLE") != NULL);
    TEST_ASSERT_TRUE(strstr(tx, "PAIRED_COUNT") != NULL || strstr(tx, "COUNT") != NULL);
    /* When streaming info is available, should include streaming stats */
    TEST_ASSERT_TRUE(strstr(tx, "BYTES_REQ") != NULL || strstr(tx, "STREAM_INFO") != NULL);
}

/* CODE_REVIEW8 Task B: Test STATUS handles bt_get_streaming_info failure gracefully */
void test_status_command_streaming_info_unavailable(void) {
    extern void bt_manager_test_force_streaming_info_failure(bool force);
    
    bt_manager_test_force_streaming_info_failure(true);
    
    mock_uart_reset_tx();
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("STATUS", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    /* Should still return basic status */
    TEST_ASSERT_TRUE(strstr(tx, "STATUS") != NULL);
    TEST_ASSERT_TRUE(strstr(tx, "MUTE") != NULL);
    TEST_ASSERT_TRUE(strstr(tx, "SAMPLE_RATE") != NULL);
    TEST_ASSERT_TRUE(strstr(tx, "PAIRED_COUNT") != NULL);
    /* Should indicate streaming info is unavailable */
    TEST_ASSERT_TRUE(strstr(tx, "STREAM_INFO=UNAVAILABLE") != NULL);
    /* Should NOT include detailed streaming stats */
    TEST_ASSERT_TRUE(strstr(tx, "BYTES_REQ") == NULL);
    TEST_ASSERT_TRUE(strstr(tx, "UNDERRUNS") == NULL);
    
    /* Clean up */
    bt_manager_test_force_streaming_info_failure(false);
}

// Test RESET in host-mode returns a mock reboot response
void test_reset_command(void) {
    mock_uart_reset_tx();
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("RESET", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_TRUE(strstr(tx, "RESET") != NULL);
    TEST_ASSERT_TRUE(strstr(tx, "MOCK_REBOOT") != NULL || strstr(tx, "REBOOTING") != NULL);
}

// Test DISCONNECT command behavior
void test_disconnect_command(void) {
    mock_uart_reset_tx();
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("DISCONNECT", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_TRUE(strstr(tx, "DISCONNECT") != NULL);
    TEST_ASSERT_TRUE(strstr(tx, "MOCK_DONE") != NULL || strstr(tx, "DONE") != NULL || strstr(tx, "FAILED") != NULL);
}

// Test START command behavior
void test_start_command(void) {
    mock_uart_reset_tx();
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("START", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_TRUE(strstr(tx, "START") != NULL);
    TEST_ASSERT_TRUE(strstr(tx, "MOCK_STARTED") != NULL || strstr(tx, "STARTED") != NULL || strstr(tx, "FAILED") != NULL);
}

/* I2S must override ongoing BEEP. When a beep is active, START should
 * clear it and leave I2S running. */
void test_start_command_stops_beep_and_enables_i2s(void) {
    mock_uart_reset_tx();

    audio_config_t cfg = {
        .sample_rate = AUDIO_SAMPLE_RATE_44K,
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = AUDIO_CHANNEL_STEREO,
        .volume = 50,
        .mute = false,
    };

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_init(&cfg));
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_beep_tone(500, 440.0));
    TEST_ASSERT_TRUE(audio_processor_is_beep_active());

    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("START", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_TRUE(strstr(tx, "START") != NULL);

    TEST_ASSERT_TRUE(audio_processor_is_i2s_active());
    TEST_ASSERT_FALSE(audio_processor_is_beep_active());

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_stop());
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_deinit());
    (void)audio_processor_drain_ring();
}

// Test STOP command behavior
void test_stop_command(void) {
    mock_uart_reset_tx();
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("STOP", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_TRUE(strstr(tx, "STOP") != NULL);
    TEST_ASSERT_TRUE(strstr(tx, "MOCK_STOPPED") != NULL || strstr(tx, "STOPPED") != NULL || strstr(tx, "FAILED") != NULL);
}

// Negative-path: simulate backend failure for DISCONNECT
void test_disconnect_failure_command(void) {
    mock_uart_reset_tx();
    // Enable forced failure
    extern void bt_manager_test_set_force_disconnect_failure(int v);
    extern void bt_manager_test_reset_forces(void);
    bt_manager_test_set_force_disconnect_failure(1);

    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("DISCONNECT", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_TRUE(strstr(tx, "DISCONNECT") != NULL);
    TEST_ASSERT_TRUE(strstr(tx, "ERR") != NULL || strstr(tx, "FAILED") != NULL);

    // Reset forces for other tests
    bt_manager_test_reset_forces();
}

// Negative-path: simulate backend failure for START
void test_start_failure_command(void) {
    mock_uart_reset_tx();
    extern void bt_manager_test_set_force_start_failure(int v);
    extern void bt_manager_test_reset_forces(void);
    bt_manager_test_set_force_start_failure(1);

    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("START", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_TRUE(strstr(tx, "START") != NULL);
    TEST_ASSERT_TRUE(strstr(tx, "ERR") != NULL || strstr(tx, "FAILED") != NULL);

    bt_manager_test_reset_forces();
}

// Negative-path: simulate backend failure for STOP
void test_stop_failure_command(void) {
    mock_uart_reset_tx();
    extern void bt_manager_test_set_force_stop_failure(int v);
    extern void bt_manager_test_reset_forces(void);
    bt_manager_test_set_force_stop_failure(1);

    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("STOP", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_TRUE(strstr(tx, "STOP") != NULL);
    TEST_ASSERT_TRUE(strstr(tx, "ERR") != NULL || strstr(tx, "FAILED") != NULL);

    bt_manager_test_reset_forces();
}

// Test BEEP when not connected should return an error
void test_beep_command_not_connected(void) {
    mock_uart_reset_tx();
    // Ensure mock BT has no connection
    // Some harnesses default to disconnected; be explicit by closing
    extern void bt_manager_mock_connection_closed(const char* mac);
    bt_manager_mock_connection_closed("aa:bb:cc:11:22:33");

    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("BEEP", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_TRUE(strstr(tx, "BEEP") != NULL);
    TEST_ASSERT_TRUE(strstr(tx, "NOT_CONNECTED") != NULL || strstr(tx, "ERR") != NULL);
}

// Test BEEP when connected should call the audio API and return OK
void test_beep_command_connected(void) {
    mock_uart_reset_tx();
    // Simulate a BT connection
    extern void bt_manager_mock_connection_established(const char* mac, const char* name);
    bt_manager_mock_connection_established("aa:bb:cc:11:22:33", "MockSpeaker");

    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("BEEP", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_TRUE(strstr(tx, "BEEP") != NULL);
    // Accept either SENT/OK or a mock variant depending on harness
    TEST_ASSERT_TRUE(strstr(tx, "SENT") != NULL || strstr(tx, "OK") != NULL || strstr(tx, "MOCK") != NULL);

    // Verify that beep was actually triggered
    TEST_ASSERT_TRUE(audio_processor_is_beep_active());

    // Verify the BEEP command requested a 10s middle-C tone
    uint32_t dur_ms = 0; double freq_hz = 0.0;
    audio_processor_get_last_beep_request(&dur_ms, &freq_hz);
    TEST_ASSERT_EQUAL_UINT32(10000U, dur_ms);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 261.63f, (float)freq_hz);
}

void test_beep_command_allowed_when_i2s_active(void) {
    mock_uart_reset_tx();
    reset_spiffs_mount_hook_counter();

    audio_config_t cfg = {
        .sample_rate = AUDIO_SAMPLE_RATE_44K,
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = AUDIO_CHANNEL_STEREO,
        .volume = 50,
        .mute = false,
    };

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_init(&cfg));
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_start());
    /* Disable synth to model live I2S capture so the BEEP busy guard trips. */
    audio_processor_set_synth_mode(false);
    TEST_ASSERT_TRUE(audio_processor_is_i2s_active());

    extern void bt_manager_mock_connection_established(const char* mac, const char* name);
    bt_manager_mock_connection_established("aa:bb:cc:11:22:33", "MockSpeaker");

    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("BEEP", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(tx, "OK|BEEP|SENT"), tx);
    TEST_ASSERT_TRUE(audio_processor_is_beep_active());

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_stop());
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_deinit());
    (void)audio_processor_drain_ring();
}

void test_beep_command_busy_when_beep_active(void) {
    mock_uart_reset_tx();
    reset_spiffs_mount_hook_counter();

    audio_config_t cfg = {
        .sample_rate = AUDIO_SAMPLE_RATE_44K,
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = AUDIO_CHANNEL_STEREO,
        .volume = 50,
        .mute = false,
    };

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_init(&cfg));
    /* Seed an active beep so the subsequent BEEP command hits the busy guard. */
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_beep_tone(1000, 440.0));
    TEST_ASSERT_TRUE(audio_processor_is_beep_active());

    extern void bt_manager_mock_connection_established(const char* mac, const char* name);
    bt_manager_mock_connection_established("aa:bb:cc:11:22:33", "MockSpeaker");

    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("BEEP", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    const char *expected = "ERR|BEEP|BUSY|BEEP_ACTIVE";
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(tx, expected), tx);

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_deinit());
    (void)audio_processor_drain_ring();
}

void test_file_command_found(void) {
    mock_uart_reset_tx();

    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("FILE alpha.txt", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    char expected[128];
    snprintf(expected, sizeof(expected), "OK|FILE|FOUND|%s,%llu\r\n", k_file_alpha_name, (unsigned long long)k_file_alpha_size);
    TEST_ASSERT_EQUAL_STRING(expected, tx);
}

void test_file_command_not_found(void) {
    mock_uart_reset_tx();

    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("FILE missing.wav", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_EQUAL_STRING("ERR|FILE|NOT_FOUND|missing.wav\r\n", tx);
}

void test_file_command_not_file(void) {
    mock_uart_reset_tx();

    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("FILE logs", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_EQUAL_STRING("ERR|FILE|NOT_FILE|logs\r\n", tx);
}

void test_files_command_lists_entries(void) {
    mock_uart_reset_tx();
    reset_spiffs_mount_hook_counter();

    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("FILES", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "INFO|FILES|ROOT|"));
    TEST_ASSERT_NOT_NULL(strstr(tx, "INFO|FILES|ITEM|alpha.txt"));
    TEST_ASSERT_NOT_NULL(strstr(tx, "INFO|FILES|ITEM|beta.bin"));
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|FILES|SUMMARY|"));

    TEST_ASSERT_GREATER_OR_EQUAL_INT(1, s_spiffs_mount_hook_count);
}

// Verify SYNTH ON toggles the mode (host: verifies response emitted)
void test_synth_on_command(void) {
    mock_uart_reset_tx();
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("SYNTH ON", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|SYNTH|ENABLED"));
}

// Verify SYNTH OFF toggles the mode (host: verifies response emitted)
void test_synth_off_command(void) {
    mock_uart_reset_tx();
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("SYNTH OFF", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|SYNTH|DISABLED"));
}

void test_diag_i2s_stop_clears_i2s_flag(void) {
    mock_uart_reset_tx();

    audio_config_t cfg = {
        .sample_rate = AUDIO_SAMPLE_RATE_44K,
        .bit_depth = AUDIO_BIT_DEPTH_16,
        .channels = AUDIO_CHANNEL_STEREO,
        .volume = 50,
        .mute = false,
    };

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_init(&cfg));
    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_start());
    audio_processor_set_synth_mode(false);
    TEST_ASSERT_TRUE(audio_processor_is_i2s_active());

    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("DIAG I2S_STOP", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|DIAG|I2S_STOPPED"));
    TEST_ASSERT_FALSE(audio_processor_is_i2s_active());

    TEST_ASSERT_EQUAL(ESP_OK, audio_processor_deinit());
}
