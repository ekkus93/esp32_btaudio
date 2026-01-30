#include "unity.h"
#include "esp_err.h"
#include "esp_avrc_api.h"
#include "mock_a2dp.h"

// Production under test (test-only hook defined in bt_manager.c when UNIT_TEST)
extern esp_err_t bt_manager_test_init_profiles(void);

// Mock helpers
void mock_avrc_reset(void);

static const char* s_call_log[8];
static int s_call_log_len;

// Hook consumed by mock_a2dp.c and mock_avrc.c to record call order
void mock_bt_call_log(const char* tag)
{
    if (s_call_log_len < (int)(sizeof(s_call_log) / sizeof(s_call_log[0]))) {
        s_call_log[s_call_log_len++] = tag;
    }
}

void setUp(void)
{
    s_call_log_len = 0;
    for (size_t i = 0; i < sizeof(s_call_log) / sizeof(s_call_log[0]); ++i) {
        s_call_log[i] = NULL;
    }
    mock_a2dp_reset();
    mock_avrc_reset();
}

void tearDown(void)
{
}

static void assert_ordered_calls(void)
{
    static const char* expected[] = {
        "esp_avrc_ct_init",
        "esp_avrc_ct_register_callback",
        "esp_a2d_source_init",
        "esp_a2d_register_callback",
        "esp_a2d_source_register_data_callback",
    };

    TEST_ASSERT_EQUAL_INT((int)(sizeof(expected) / sizeof(expected[0])), s_call_log_len);
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i) {
        TEST_ASSERT_NOT_NULL_MESSAGE(s_call_log[i], "missing call entry");
        TEST_ASSERT_EQUAL_STRING(expected[i], s_call_log[i]);
    }
}

void test_bt_manager_profiles_init_order(void)
{
    esp_err_t ret = bt_manager_test_init_profiles();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    assert_ordered_calls();
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_bt_manager_profiles_init_order);
    return UNITY_END();
}
