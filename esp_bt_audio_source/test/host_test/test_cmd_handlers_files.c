/**
 * @file test_cmd_handlers_files.c
 * @brief Unit tests for file/partition command handlers (TDD Red-Green-Refactor)
 * 
 * Tests focus on error paths, parameter validation, and edge cases for:
 * - cmd_handle_file()
 * - cmd_handle_files()
 * - cmd_handle_parts()
 */

#include "unity.h"
#include "cmd_handlers.h"
#include "command_interface.h"
#include "mock_uart.h"
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>

// Test file root for SPIFFS emulation
static char s_test_root[256] = {0};
static int s_test_root_ready = 0;

// Override function for cmd_files_get_root() on host
const char* cmd_files_host_mount_override(void) {
    // When s_test_root_ready is 0, return empty string to simulate "no root"
    // (NULL would fall back to "/spiffs", but "" makes get_root return "")
    return s_test_root_ready ? s_test_root : "";
}

void setUp(void) {
    mock_uart_init(115200);
    cmd_init();
    cmd_test_reset_cmd_process_state();
    
    // Create temporary directory for file tests
    char template[] = "/tmp/esp_files_test_XXXXXX";
    char* root = mkdtemp(template);
    if (root != NULL) {
        strncpy(s_test_root, root, sizeof(s_test_root) - 1);
        s_test_root[sizeof(s_test_root) - 1] = '\0';
        s_test_root_ready = 1;
    }
}

void tearDown(void) {
    // Cleanup test directory
    if (s_test_root_ready && s_test_root[0] != '\0') {
        // Remove files in directory
        char cleanup_cmd[512];
        snprintf(cleanup_cmd, sizeof(cleanup_cmd), "rm -rf %s", s_test_root);
        system(cleanup_cmd);
        s_test_root[0] = '\0';
        s_test_root_ready = 0;
    }
    
    cmd_deinit();
}

// ============================================================================
// Test 1: cmd_handle_file() should reject missing parameter
// ============================================================================
void test_cmd_file_should_reject_missing_param(void) {
    // Arrange
    cmd_context_t ctx = {
        .type = CMD_TYPE_FILE,
        .param_count = 0
    };
    
    // Act
    cmd_status_t result = cmd_handle_file(&ctx);
    
    // Assert
    TEST_ASSERT_EQUAL(CMD_SUCCESS, result);
    
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "ERR|FILE|MISSING_PARAM"));
}

// ============================================================================
// Test 2: cmd_handle_file() should report file not found
// ============================================================================
void test_cmd_file_should_report_not_found(void) {
    // Arrange
    cmd_context_t ctx = {
        .type = CMD_TYPE_FILE,
        .param_count = 1
    };
    strcpy(ctx.params[0], "nonexistent.txt");
    
    // Act
    cmd_status_t result = cmd_handle_file(&ctx);
    
    // Assert
    TEST_ASSERT_EQUAL(CMD_SUCCESS, result);
    
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "ERR|FILE|NOT_FOUND"));
}

// ============================================================================
// Test 3: cmd_handle_file() should reject path too long
// ============================================================================
void test_cmd_file_should_reject_path_too_long(void) {
    // Arrange - Make root very long so combined path exceeds 256 bytes
    // fullpath buffer in cmd_handle_file is 256 bytes
    // Set root to 240 chars + "/" + filename (20 chars) = 261 total > 256
    char long_root[245];
    memset(long_root, 'x', sizeof(long_root) - 1);
    long_root[sizeof(long_root) - 1] = '\0';
    
    // Temporarily replace the test root with our long path
    strncpy(s_test_root, long_root, sizeof(s_test_root) - 1);
    s_test_root[sizeof(s_test_root) - 1] = '\0';
    s_test_root_ready = 1;
    
    cmd_context_t ctx = {
        .type = CMD_TYPE_FILE,
        .param_count = 1
    };
    
    // Use a filename that will cause overflow: 240 + 1 + 20 = 261 > 256
    strncpy(ctx.params[0], "very_long_filename12", CMD_MAX_PARAM_LEN - 1);
    ctx.params[0][CMD_MAX_PARAM_LEN - 1] = '\0';
    
    // Act
    cmd_status_t result = cmd_handle_file(&ctx);
    
    // Assert
    TEST_ASSERT_EQUAL(CMD_SUCCESS, result);
    
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "ERR|FILE|PATH_TOO_LONG"));
}

// ============================================================================
// Test 4: cmd_handle_file() should reject directory (not a file)
// ============================================================================
void test_cmd_file_should_reject_directory(void) {
    // Arrange - Create a subdirectory
    if (!s_test_root_ready) {
        TEST_FAIL_MESSAGE("Test root not initialized");
        return;
    }
    
    char subdir_path[300];
    snprintf(subdir_path, sizeof(subdir_path), "%s/testdir", s_test_root);
    mkdir(subdir_path, 0755);
    
    cmd_context_t ctx = {
        .type = CMD_TYPE_FILE,
        .param_count = 1
    };
    strcpy(ctx.params[0], "testdir");
    
    // Act
    cmd_status_t result = cmd_handle_file(&ctx);
    
    // Assert
    TEST_ASSERT_EQUAL(CMD_SUCCESS, result);
    
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "ERR|FILE|NOT_FILE"));
}

// ============================================================================
// Test 5: cmd_handle_file() should report success for valid file
// ============================================================================
void test_cmd_file_should_succeed_for_valid_file(void) {
    // Arrange - Create a test file
    if (!s_test_root_ready) {
        TEST_FAIL_MESSAGE("Test root not initialized");
        return;
    }
    
    char file_path[300];
    snprintf(file_path, sizeof(file_path), "%s/test.txt", s_test_root);
    FILE* f = fopen(file_path, "w");
    TEST_ASSERT_NOT_NULL(f);
    fprintf(f, "test content");
    fclose(f);
    
    cmd_context_t ctx = {
        .type = CMD_TYPE_FILE,
        .param_count = 1
    };
    strcpy(ctx.params[0], "test.txt");
    
    // Act
    cmd_status_t result = cmd_handle_file(&ctx);
    
    // Assert
    TEST_ASSERT_EQUAL(CMD_SUCCESS, result);
    
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|FILE|FOUND"));
}

// ============================================================================
// Test 6: cmd_handle_file() should fail when no root set
// ============================================================================
void test_cmd_file_should_fail_when_no_root(void) {
    // Arrange - Clear root by marking not ready
    s_test_root_ready = 0;
    
    cmd_context_t ctx = {
        .type = CMD_TYPE_FILE,
        .param_count = 1
    };
    strcpy(ctx.params[0], "test.txt");
    
    // Act
    cmd_status_t result = cmd_handle_file(&ctx);
    
    // Assert
    TEST_ASSERT_EQUAL(CMD_SUCCESS, result);
    
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "ERR|FILE|NO_ROOT"));
    
    // Restore for tearDown
    s_test_root_ready = 1;
}

// ============================================================================
// Test 7: cmd_handle_files() should fail when no root set
// ============================================================================
void test_cmd_files_should_fail_when_no_root(void) {
    // Arrange - Clear root by marking not ready
    s_test_root_ready = 0;
    
    cmd_context_t ctx = {
        .type = CMD_TYPE_FILES,
        .param_count = 0
    };
    
    // Act
    cmd_status_t result = cmd_handle_files(&ctx);
    
    // Assert
    TEST_ASSERT_EQUAL(CMD_SUCCESS, result);
    
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "ERR|FILES|NO_ROOT"));
    
    // Restore for tearDown
    s_test_root_ready = 1;
}

// ============================================================================
// Test 8: cmd_handle_files() should fail when directory doesn't exist
// ============================================================================
void test_cmd_files_should_fail_when_dir_missing(void) {
    // Arrange - Set root to non-existent directory by changing s_test_root
    char saved_root[256];
    strncpy(saved_root, s_test_root, sizeof(saved_root));
    strncpy(s_test_root, "/tmp/nonexistent_dir_12345", sizeof(s_test_root));
    
    cmd_context_t ctx = {
        .type = CMD_TYPE_FILES,
        .param_count = 0
    };
    
    // Act
    cmd_status_t result = cmd_handle_files(&ctx);
    
    // Assert
    TEST_ASSERT_EQUAL(CMD_SUCCESS, result);
    
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "ERR|FILES|OPEN_FAILED"));
    
    // Restore
    strncpy(s_test_root, saved_root, sizeof(s_test_root));
}

// ============================================================================
// Test 9: cmd_handle_files() should list empty directory
// ============================================================================
void test_cmd_files_should_list_empty_directory(void) {
    // Arrange - test root is already empty
    if (!s_test_root_ready) {
        TEST_FAIL_MESSAGE("Test root not initialized");
        return;
    }
    
    cmd_context_t ctx = {
        .type = CMD_TYPE_FILES,
        .param_count = 0
    };
    
    // Act
    cmd_status_t result = cmd_handle_files(&ctx);
    
    // Assert
    TEST_ASSERT_EQUAL(CMD_SUCCESS, result);
    
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "OK|FILES|SUMMARY"));
    TEST_ASSERT_NOT_NULL(strstr(tx, "COUNT=0"));
}

// ============================================================================
// Test 10: cmd_handle_files() should reject unexpected parameter
// ============================================================================
void test_cmd_files_should_reject_unexpected_param(void) {
    // Arrange
    cmd_context_t ctx = {
        .type = CMD_TYPE_FILES,
        .param_count = 1
    };
    strcpy(ctx.params[0], "unexpected");
    
    // Act
    cmd_status_t result = cmd_handle_files(&ctx);
    
    // Assert
    TEST_ASSERT_EQUAL(CMD_SUCCESS, result);
    
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "ERR|FILES|UNEXPECTED_PARAM"));
}

// ============================================================================
// Test 11: cmd_handle_parts() should report unsupported on host
// ============================================================================
void test_cmd_parts_should_report_unsupported_on_host(void) {
    // Arrange
    cmd_context_t ctx = {
        .type = CMD_TYPE_PARTS,
        .param_count = 0
    };
    
    // Act
    cmd_status_t result = cmd_handle_parts(&ctx);
    
    // Assert
    TEST_ASSERT_EQUAL(CMD_SUCCESS, result);
    
    const char* tx = mock_uart_get_tx_data();
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_NOT_NULL(strstr(tx, "ERR|PARTS|UNSUPPORTED"));
}

// ============================================================================
// Unity Test Runner
// ============================================================================
int main(void) {
    UNITY_BEGIN();
    
    // cmd_handle_file() tests
    RUN_TEST(test_cmd_file_should_reject_missing_param);
    RUN_TEST(test_cmd_file_should_report_not_found);
    RUN_TEST(test_cmd_file_should_reject_path_too_long);
    RUN_TEST(test_cmd_file_should_reject_directory);
    RUN_TEST(test_cmd_file_should_succeed_for_valid_file);
    RUN_TEST(test_cmd_file_should_fail_when_no_root);
    
    // cmd_handle_files() tests
    RUN_TEST(test_cmd_files_should_fail_when_no_root);
    RUN_TEST(test_cmd_files_should_fail_when_dir_missing);
    RUN_TEST(test_cmd_files_should_list_empty_directory);
    RUN_TEST(test_cmd_files_should_reject_unexpected_param);
    
    // cmd_handle_parts() test
    RUN_TEST(test_cmd_parts_should_report_unsupported_on_host);
    
    return UNITY_END();
}
