/**
 * test_commands_helpers.c — unit tests for commands_helpers.c string utilities
 *
 * Covers:
 *   - cmd_safe_copy()            boundary conditions
 *   - cmd_safe_append()          full-buffer, empty suffix, NULL handling
 *   - copy_truncated_identifier() truncation with ellipsis
 *   - cmd_parse_log_level()      all levels, case, numeric, invalid
 *   - cmd_append_metadata()      key=value appending, overflow
 */

#include <string.h>
#include <stdio.h>
#include "unity.h"
#include "commands_priv.h"  /* cmd_safe_copy, cmd_safe_append, etc. */

void setUp(void) {}
void tearDown(void) {}

/* ── cmd_safe_copy ──────────────────────────────────────────────────────── */

void test_safe_copy_normal(void)
{
    char dst[16];
    cmd_safe_copy(dst, sizeof(dst), "hello");
    TEST_ASSERT_EQUAL_STRING("hello", dst);
}

void test_safe_copy_exact_fit(void)
{
    char dst[6];
    /* "hello" is 5 chars + null = 6 — fits exactly */
    cmd_safe_copy(dst, sizeof(dst), "hello");
    TEST_ASSERT_EQUAL_STRING("hello", dst);
}

void test_safe_copy_truncates_and_null_terminates(void)
{
    char dst[4];
    cmd_safe_copy(dst, sizeof(dst), "abcdefg");
    TEST_ASSERT_EQUAL_CHAR('\0', dst[3]);
    TEST_ASSERT_EQUAL_INT(3, (int)strlen(dst));  /* at most 3 chars */
}

void test_safe_copy_empty_source(void)
{
    char dst[8] = "JUNK";
    cmd_safe_copy(dst, sizeof(dst), "");
    TEST_ASSERT_EQUAL_CHAR('\0', dst[0]);
}

void test_safe_copy_null_source_no_crash(void)
{
    char dst[8] = "JUNK";
    /* Should not crash and should leave dst safe (null-terminated) */
    cmd_safe_copy(dst, sizeof(dst), NULL);
    TEST_ASSERT_EQUAL_CHAR('\0', dst[0]);
}

/* ── cmd_safe_append ────────────────────────────────────────────────────── */

void test_safe_append_to_empty(void)
{
    char dst[16] = "";
    cmd_safe_append(dst, sizeof(dst), "world");
    TEST_ASSERT_EQUAL_STRING("world", dst);
}

void test_safe_append_to_existing(void)
{
    char dst[16] = "hello";
    cmd_safe_append(dst, sizeof(dst), " world");
    TEST_ASSERT_EQUAL_STRING("hello world", dst);
}

void test_safe_append_truncates_at_capacity(void)
{
    char dst[8] = "hello";  /* 5 chars, 2 bytes left (1 char + null) */
    cmd_safe_append(dst, sizeof(dst), "!!!");
    /* dst should still be null-terminated and not overflow */
    TEST_ASSERT_EQUAL_CHAR('\0', dst[7]);
    TEST_ASSERT_TRUE(strlen(dst) <= 7);
}

void test_safe_append_full_buffer_no_change(void)
{
    char dst[6] = "hello";  /* exactly full: 5 chars + null */
    cmd_safe_append(dst, sizeof(dst), "more");
    /* Still "hello" — buffer was full, nothing added */
    TEST_ASSERT_EQUAL_STRING("hello", dst);
}

void test_safe_append_empty_suffix_no_change(void)
{
    char dst[16] = "hello";
    cmd_safe_append(dst, sizeof(dst), "");
    TEST_ASSERT_EQUAL_STRING("hello", dst);
}

void test_safe_append_null_suffix_no_crash(void)
{
    char dst[16] = "hello";
    cmd_safe_append(dst, sizeof(dst), NULL);
    TEST_ASSERT_EQUAL_STRING("hello", dst);
}

void test_safe_append_null_dst_no_crash(void)
{
    /* Must not crash when dst is NULL */
    cmd_safe_append(NULL, 16, "text");
}

void test_safe_append_zero_size_no_crash(void)
{
    char dst[4] = "hi";
    cmd_safe_append(dst, 0, "more");
    /* dst unchanged — no write occurred */
    TEST_ASSERT_EQUAL_STRING("hi", dst);
}

/* ── copy_truncated_identifier ───────────────────────────────────────────── */

void test_truncated_id_short_string_copied_verbatim(void)
{
    char dst[16];
    copy_truncated_identifier("hello", dst, sizeof(dst));
    TEST_ASSERT_EQUAL_STRING("hello", dst);
}

void test_truncated_id_exact_fit_no_ellipsis(void)
{
    char dst[6];
    /* "hello" is 5 chars, fits in 6-byte buffer without truncation */
    copy_truncated_identifier("hello", dst, sizeof(dst));
    TEST_ASSERT_EQUAL_STRING("hello", dst);
    TEST_ASSERT_NULL(strstr(dst, "..."));
}

void test_truncated_id_long_string_gets_ellipsis(void)
{
    char dst[8];
    copy_truncated_identifier("abcdefghijklmn", dst, sizeof(dst));
    /* Result must end with "..." and total length must be dst_size-1 */
    TEST_ASSERT_EQUAL_INT((int)(sizeof(dst) - 1), (int)strlen(dst));
    TEST_ASSERT_EQUAL_CHAR('.', dst[sizeof(dst) - 4]);
    TEST_ASSERT_EQUAL_CHAR('.', dst[sizeof(dst) - 3]);
    TEST_ASSERT_EQUAL_CHAR('.', dst[sizeof(dst) - 2]);
    TEST_ASSERT_EQUAL_CHAR('\0', dst[sizeof(dst) - 1]);
}

void test_truncated_id_null_source_gives_empty(void)
{
    char dst[8] = "JUNK";
    copy_truncated_identifier(NULL, dst, sizeof(dst));
    TEST_ASSERT_EQUAL_CHAR('\0', dst[0]);
}

void test_truncated_id_empty_source_gives_empty(void)
{
    char dst[8] = "JUNK";
    copy_truncated_identifier("", dst, sizeof(dst));
    TEST_ASSERT_EQUAL_CHAR('\0', dst[0]);
}

void test_truncated_id_zero_dst_size_no_crash(void)
{
    char dst[8] = "JUNK";
    copy_truncated_identifier("hello", dst, 0);
    /* dst[0] should be untouched (0-size means no write) */
    TEST_ASSERT_EQUAL_CHAR('J', dst[0]);
}

/* ── cmd_parse_log_level ─────────────────────────────────────────────────── */

void test_log_level_none(void)
{
    int level = -1;
    TEST_ASSERT_TRUE(cmd_parse_log_level("NONE", &level));
    TEST_ASSERT_EQUAL_INT(ESP_LOG_NONE, level);
}

void test_log_level_error(void)
{
    int level = -1;
    TEST_ASSERT_TRUE(cmd_parse_log_level("ERROR", &level));
    TEST_ASSERT_EQUAL_INT(ESP_LOG_ERROR, level);
}

void test_log_level_err_alias(void)
{
    int level = -1;
    TEST_ASSERT_TRUE(cmd_parse_log_level("ERR", &level));
    TEST_ASSERT_EQUAL_INT(ESP_LOG_ERROR, level);
}

void test_log_level_warn(void)
{
    int level = -1;
    TEST_ASSERT_TRUE(cmd_parse_log_level("WARN", &level));
    TEST_ASSERT_EQUAL_INT(ESP_LOG_WARN, level);
}

void test_log_level_info(void)
{
    int level = -1;
    TEST_ASSERT_TRUE(cmd_parse_log_level("INFO", &level));
    TEST_ASSERT_EQUAL_INT(ESP_LOG_INFO, level);
}

void test_log_level_debug(void)
{
    int level = -1;
    TEST_ASSERT_TRUE(cmd_parse_log_level("DEBUG", &level));
    TEST_ASSERT_EQUAL_INT(ESP_LOG_DEBUG, level);
}

void test_log_level_verbose(void)
{
    int level = -1;
    TEST_ASSERT_TRUE(cmd_parse_log_level("VERBOSE", &level));
    TEST_ASSERT_EQUAL_INT(ESP_LOG_VERBOSE, level);
}

void test_log_level_case_insensitive(void)
{
    int level = -1;
    TEST_ASSERT_TRUE(cmd_parse_log_level("warn", &level));
    TEST_ASSERT_EQUAL_INT(ESP_LOG_WARN, level);
}

void test_log_level_numeric_valid(void)
{
    int level = -1;
    /* ESP_LOG_INFO is typically 3 */
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", ESP_LOG_INFO);
    TEST_ASSERT_TRUE(cmd_parse_log_level(buf, &level));
    TEST_ASSERT_EQUAL_INT(ESP_LOG_INFO, level);
}

void test_log_level_unknown_string_returns_false(void)
{
    int level = ESP_LOG_INFO;
    TEST_ASSERT_FALSE(cmd_parse_log_level("BANANA", &level));
    /* output should be unchanged */
    TEST_ASSERT_EQUAL_INT(ESP_LOG_INFO, level);
}

void test_log_level_null_input_returns_false(void)
{
    int level = ESP_LOG_INFO;
    TEST_ASSERT_FALSE(cmd_parse_log_level(NULL, &level));
}

void test_log_level_null_output_returns_false(void)
{
    TEST_ASSERT_FALSE(cmd_parse_log_level("INFO", NULL));
}

/* ── cmd_append_metadata ─────────────────────────────────────────────────── */

void test_append_metadata_first_entry_no_comma(void)
{
    char buf[64] = "";
    cmd_append_metadata(buf, sizeof(buf), "key", "value");
    TEST_ASSERT_EQUAL_STRING("key=value", buf);
}

void test_append_metadata_second_entry_has_comma(void)
{
    char buf[64] = "";
    cmd_append_metadata(buf, sizeof(buf), "a", "1");
    cmd_append_metadata(buf, sizeof(buf), "b", "2");
    TEST_ASSERT_EQUAL_STRING("a=1,b=2", buf);
}

void test_append_metadata_full_buffer_no_crash(void)
{
    char buf[10] = "abcdefgh";  /* 8 chars used, 1 remaining, 1 null */
    cmd_append_metadata(buf, sizeof(buf), "k", "v");
    /* Buffer too full to append — should not corrupt or crash */
    TEST_ASSERT_EQUAL_CHAR('\0', buf[9]);
}

void test_append_metadata_null_key_no_crash(void)
{
    char buf[32] = "";
    cmd_append_metadata(buf, sizeof(buf), NULL, "value");
    /* Nothing appended */
    TEST_ASSERT_EQUAL_CHAR('\0', buf[0]);
}

void test_append_metadata_empty_value_no_append(void)
{
    char buf[32] = "";
    cmd_append_metadata(buf, sizeof(buf), "key", "");
    /* Empty value treated as no-op */
    TEST_ASSERT_EQUAL_CHAR('\0', buf[0]);
}

int main(void)
{
    UNITY_BEGIN();

    /* cmd_safe_copy */
    RUN_TEST(test_safe_copy_normal);
    RUN_TEST(test_safe_copy_exact_fit);
    RUN_TEST(test_safe_copy_truncates_and_null_terminates);
    RUN_TEST(test_safe_copy_empty_source);
    RUN_TEST(test_safe_copy_null_source_no_crash);

    /* cmd_safe_append */
    RUN_TEST(test_safe_append_to_empty);
    RUN_TEST(test_safe_append_to_existing);
    RUN_TEST(test_safe_append_truncates_at_capacity);
    RUN_TEST(test_safe_append_full_buffer_no_change);
    RUN_TEST(test_safe_append_empty_suffix_no_change);
    RUN_TEST(test_safe_append_null_suffix_no_crash);
    RUN_TEST(test_safe_append_null_dst_no_crash);
    RUN_TEST(test_safe_append_zero_size_no_crash);

    /* copy_truncated_identifier */
    RUN_TEST(test_truncated_id_short_string_copied_verbatim);
    RUN_TEST(test_truncated_id_exact_fit_no_ellipsis);
    RUN_TEST(test_truncated_id_long_string_gets_ellipsis);
    RUN_TEST(test_truncated_id_null_source_gives_empty);
    RUN_TEST(test_truncated_id_empty_source_gives_empty);
    RUN_TEST(test_truncated_id_zero_dst_size_no_crash);

    /* cmd_parse_log_level */
    RUN_TEST(test_log_level_none);
    RUN_TEST(test_log_level_error);
    RUN_TEST(test_log_level_err_alias);
    RUN_TEST(test_log_level_warn);
    RUN_TEST(test_log_level_info);
    RUN_TEST(test_log_level_debug);
    RUN_TEST(test_log_level_verbose);
    RUN_TEST(test_log_level_case_insensitive);
    RUN_TEST(test_log_level_numeric_valid);
    RUN_TEST(test_log_level_unknown_string_returns_false);
    RUN_TEST(test_log_level_null_input_returns_false);
    RUN_TEST(test_log_level_null_output_returns_false);

    /* cmd_append_metadata */
    RUN_TEST(test_append_metadata_first_entry_no_comma);
    RUN_TEST(test_append_metadata_second_entry_has_comma);
    RUN_TEST(test_append_metadata_full_buffer_no_crash);
    RUN_TEST(test_append_metadata_null_key_no_crash);
    RUN_TEST(test_append_metadata_empty_value_no_append);

    return UNITY_END();
}
