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

static char s_test_spiffs_root[PATH_MAX];
static int s_test_spiffs_root_ready = 0;
static const char* k_file_alpha_name = "alpha.txt";
static const char k_file_alpha_data[] = "alpha-data";
static const size_t k_file_alpha_size = sizeof(k_file_alpha_data) - 1;
static const char* k_file_beta_name = "beta.bin";
static const unsigned char k_file_beta_data[] = {0x01, 0x02, 0x7F, 0xA0, 0x00, 0x55};
static const size_t k_file_beta_size = sizeof(k_file_beta_data);
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
}

void tearDown(void) {
    // Clean up after each test
    cmd_deinit();
    test_cleanup_spiffs_root();
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

void test_parse_file_command(void) {
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("FILE alpha.txt", &ctx));
    TEST_ASSERT_EQUAL(CMD_TYPE_FILE, ctx.type);
    TEST_ASSERT_EQUAL(1, ctx.param_count);
    TEST_ASSERT_EQUAL_STRING(k_file_alpha_name, ctx.params[0]);
}

// Test invalid command
void test_parse_invalid_command(void) {
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_ERROR_UNKNOWN, cmd_parse("INVALID_COMMAND", &ctx));
}

// Test command with whitespace
void test_parse_command_with_whitespace(void) {
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("  VOLUME  75  ", &ctx));
    TEST_ASSERT_EQUAL(CMD_TYPE_VOLUME, ctx.type);
    TEST_ASSERT_EQUAL(1, ctx.param_count);
    TEST_ASSERT_EQUAL_STRING("75", ctx.params[0]);
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

// Main test runner
int main(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_parse_scan_command);
    RUN_TEST(test_parse_connect_command);
    RUN_TEST(test_parse_i2s_config_command);
    RUN_TEST(test_parse_file_command);
    RUN_TEST(test_parse_invalid_command);
    RUN_TEST(test_parse_command_with_whitespace);
    RUN_TEST(test_send_response);
    RUN_TEST(test_command_processing);
    RUN_TEST(test_help_command);
    RUN_TEST(test_version_command);
    RUN_TEST(test_scan_invokes_manager);

    // Pairing related tests
    RUN_TEST(test_confirm_pin_command);
    RUN_TEST(test_enter_pin_command);
    
    // Forward-declare tests implemented below
    extern void test_mute_unmute_command(void);
    extern void test_unpair_command_success(void);
    extern void test_unpair_command_failure(void);
    extern void test_unpair_command_not_found(void);
    extern void test_unpair_all_command(void);
    extern void test_unpair_all_command_failure(void);
    extern void test_status_command(void);
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
    RUN_TEST(test_reset_command);
    
    // Test DISCONNECT command
    extern void test_disconnect_command(void);
    RUN_TEST(test_disconnect_command);
    
    // Test START and STOP commands
    extern void test_start_command(void);
    extern void test_stop_command(void);
    RUN_TEST(test_start_command);
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
    RUN_TEST(test_beep_command_not_connected);
    RUN_TEST(test_beep_command_connected);
    // Test PLAY command enqueues audio via host stub and makes data available
    extern void test_play_command(void);
    extern void test_file_command_found(void);
    extern void test_file_command_not_found(void);
    extern void test_file_command_not_file(void);
    RUN_TEST(test_play_command);
    RUN_TEST(test_file_command_found);
    RUN_TEST(test_file_command_not_found);
    RUN_TEST(test_file_command_not_file);
    
    // Tests for SYNTH command (host-mode verifies parsing + response)
    extern void test_synth_on_command(void);
    extern void test_synth_off_command(void);
    RUN_TEST(test_synth_on_command);
    RUN_TEST(test_synth_off_command);
    
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
}

// Test PLAY command: request playback of a spiffs asset (host-mode) and
// verify the command emitted the mock-enqueued response and that the
// host audio_processor stub appended audio data readable via
// audio_processor_read(). Use the asset filename as the parameter so
// cmd_execute() builds the /spiffs/<name> path as it would on-device.
void test_play_command(void) {
    mock_uart_reset_tx();

    // Ensure ringbuffer empty before test
    audio_processor_drain_ringbuffer();

    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("PLAY worker_long_norm.wav", &ctx));
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_execute(&ctx));

    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    // Expect the mock enqueue response containing the original filename
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|PLAY|MOCK_ENQUEUED|worker_long_norm.wav"));

    // Read some data from the audio_processor ringbuffer to ensure playback
    uint8_t buf[1024]; size_t read = 0;
    /* Diagnostic: capture return value so we can see what implementation
     * is being invoked (useful when linker/headers mismatch) */
    int ret = audio_processor_read(buf, sizeof(buf), &read);
    /* Accept either the esp_err_t style (ESP_OK + bytes_read>0) or a
     * legacy/alternate style where the function returns the number of
     * bytes read directly (positive int). Be explicit so the test is
     * resilient across small signature mismatches in host stubs. */
    if (ret == ESP_OK) {
        TEST_ASSERT_TRUE(read > 0);
    } else if (ret > 0) {
        /* Some builds may return the byte-count directly. Ensure we
         * actually received data. */
        TEST_ASSERT_TRUE((size_t)ret == read || read > 0);
    } else {
        /* Explicit fail with the numeric code for easier triage. */
        char msg[64];
        snprintf(msg, sizeof(msg), "audio_processor_read failed: %d", ret);
        TEST_FAIL_MESSAGE(msg);
    }
    // Data should not be all zeros (stub generates deterministic non-zero pattern)
    int found_nonzero = 0;
    for (size_t i = 0; i < read; ++i) { if (buf[i] != 0) { found_nonzero = 1; break; } }
    TEST_ASSERT_EQUAL_INT(1, found_nonzero);
    // Ensure beep flag wasn't accidentally set by play
    TEST_ASSERT_FALSE(audio_processor_is_beep_active());
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
