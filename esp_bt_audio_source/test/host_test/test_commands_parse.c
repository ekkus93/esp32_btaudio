/* test_commands_parse.c — parse-group test bodies, split out of
 * test_commands.c; linked into the same test_commands executable. */
#include "test_commands_shared.h"

/* --- test_parse_scan_command --- */
void test_parse_scan_command(void) {
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("SCAN", &ctx));
    TEST_ASSERT_EQUAL(CMD_TYPE_SCAN, ctx.type);
    TEST_ASSERT_EQUAL(0, ctx.param_count);
}

/* --- test_parse_connect_command --- */
void test_parse_connect_command(void) {
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("CONNECT AA:BB:CC:DD:EE:FF", &ctx));
    TEST_ASSERT_EQUAL(CMD_TYPE_CONNECT, ctx.type);
    TEST_ASSERT_EQUAL(1, ctx.param_count);
    TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FF", ctx.params[0]);
}

/* --- test_parse_i2s_config_command --- */
void test_parse_i2s_config_command(void) {
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("I2S_CONFIG 26,25,22,21", &ctx));
    TEST_ASSERT_EQUAL(CMD_TYPE_I2S_CONFIG, ctx.type);
    TEST_ASSERT_EQUAL(1, ctx.param_count);
    TEST_ASSERT_EQUAL_STRING("26,25,22,21", ctx.params[0]);
}

/* --- test_parse_i2s_config_command_with_format --- */
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

/* --- test_parse_invalid_command --- */
void test_parse_invalid_command(void) {
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_ERROR_UNKNOWN, cmd_parse("INVALID_COMMAND", &ctx));
}

/* --- test_parse_malformed_tokens --- */
void test_parse_malformed_tokens(void) {
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_ERROR_UNKNOWN, cmd_parse("   ,,,", &ctx));
}

/* --- test_parse_command_with_whitespace --- */
void test_parse_command_with_whitespace(void) {
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("  VOLUME  75  ", &ctx));
    TEST_ASSERT_EQUAL(CMD_TYPE_VOLUME, ctx.type);
    TEST_ASSERT_EQUAL(1, ctx.param_count);
    TEST_ASSERT_EQUAL_STRING("75", ctx.params[0]);
}

/* --- test_parse_diag_command --- */
void test_parse_diag_command(void) {
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("DIAG", &ctx));
    TEST_ASSERT_EQUAL(CMD_TYPE_DIAG, ctx.type);
    TEST_ASSERT_EQUAL(0, ctx.param_count);
}

/* --- test_parse_empty_command_should_error --- */
void test_parse_empty_command_should_error(void) {
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_ERROR_UNKNOWN, cmd_parse("   \t   ", &ctx));
}

/* --- test_parse_truly_empty_string_should_error --- */
void test_parse_truly_empty_string_should_error(void) {
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_ERROR_UNKNOWN, cmd_parse("", &ctx));
}

/* --- test_parse_whitespace_only_should_error --- */
void test_parse_whitespace_only_should_error(void) {
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_ERROR_UNKNOWN, cmd_parse("   ", &ctx));
}

/* --- test_parse_limits_param_count_and_truncates --- */
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

/* --- test_parse_connect_name_preserves_spaces --- */
void test_parse_connect_name_preserves_spaces(void) {
    cmd_context_t ctx;
    TEST_ASSERT_EQUAL(CMD_SUCCESS, cmd_parse("CONNECT_NAME   Living   Room   Speaker   ", &ctx));
    TEST_ASSERT_EQUAL(CMD_TYPE_CONNECT_NAME, ctx.type);
    TEST_ASSERT_EQUAL(1, ctx.param_count);
    TEST_ASSERT_EQUAL_STRING("Living   Room   Speaker", ctx.params[0]);
}

