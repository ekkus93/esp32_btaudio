/* test_cmd_status_to_name.c — UT-9
 *
 * cmd_status_to_name() was the only non-weak function in command_interface.c and had
 * 14.3% function coverage. This micro-suite pins every enum value and the default. */
#include "unity.h"
#include "command_interface.h"

void setUp(void) {}
void tearDown(void) {}

void test_status_names_cover_all_enum_values(void)
{
    TEST_ASSERT_EQUAL_STRING("CMD_SUCCESS", cmd_status_to_name(CMD_SUCCESS));
    TEST_ASSERT_EQUAL_STRING("CMD_ERROR_INIT_FAILED", cmd_status_to_name(CMD_ERROR_INIT_FAILED));
    TEST_ASSERT_EQUAL_STRING("CMD_ERROR_INVALID_PARAM", cmd_status_to_name(CMD_ERROR_INVALID_PARAM));
    TEST_ASSERT_EQUAL_STRING("CMD_ERROR_UNKNOWN", cmd_status_to_name(CMD_ERROR_UNKNOWN));
    TEST_ASSERT_EQUAL_STRING("CMD_ERROR_NOT_INITIALIZED", cmd_status_to_name(CMD_ERROR_NOT_INITIALIZED));
    TEST_ASSERT_EQUAL_STRING("CMD_ERROR_TOO_MANY_PARAMS", cmd_status_to_name(CMD_ERROR_TOO_MANY_PARAMS));
}

void test_status_name_unknown_code_falls_through_to_default(void)
{
    TEST_ASSERT_EQUAL_STRING("CMD_ERROR_UNKNOWN_CODE", cmd_status_to_name((cmd_status_t)9999));
}

/* This suite links only command_interface.c (not commands.c), so the weak
 * link-safety fallbacks are the active definitions. Verify their contract:
 * they must fail closed with NOT_INITIALIZED rather than silently succeed. */
void test_weak_fallbacks_fail_closed(void)
{
    cmd_context_t ctx = {0};
    TEST_ASSERT_EQUAL_INT(CMD_ERROR_NOT_INITIALIZED, cmd_init());
    TEST_ASSERT_EQUAL_INT(CMD_ERROR_NOT_INITIALIZED, cmd_deinit());
    TEST_ASSERT_EQUAL_INT(CMD_ERROR_NOT_INITIALIZED, cmd_parse("STATUS", &ctx));
    TEST_ASSERT_EQUAL_INT(CMD_ERROR_NOT_INITIALIZED, cmd_execute(&ctx));
    TEST_ASSERT_EQUAL_INT(CMD_ERROR_NOT_INITIALIZED, cmd_send_response("OK", "X", "Y", NULL));
    TEST_ASSERT_EQUAL_INT(CMD_ERROR_NOT_INITIALIZED, cmd_process());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_status_names_cover_all_enum_values);
    RUN_TEST(test_status_name_unknown_code_falls_through_to_default);
    RUN_TEST(test_weak_fallbacks_fail_closed);
    return UNITY_END();
}
