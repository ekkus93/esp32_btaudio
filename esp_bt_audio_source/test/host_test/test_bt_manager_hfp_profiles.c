#include "unity.h"

#include <stddef.h>
#include <string.h>

#include "bt_duplex_state.h"
#include "bt_hfp_ag.h"
#include "bt_manager.h"
#include "mock_a2dp.h"
#include "mock_avrc.h"

extern esp_err_t bt_manager_test_init_profiles(void);
extern void mock_gap_reset(void);

#define MAX_CALLS 16

static const char *s_calls[MAX_CALLS];
static int s_call_count;
static esp_err_t s_hfp_register_result;
static esp_err_t s_hfp_init_result;
static bool s_hfp_init_callback_failure;
static esp_err_t s_hfp_deinit_result;
static int s_hfp_register_calls;
static int s_hfp_init_calls;
static int s_hfp_deinit_calls;

void bt_manager_test_log_call(const char *name)
{
    if (s_call_count < MAX_CALLS) {
        s_calls[s_call_count++] = name;
    }
}

static esp_err_t mock_hfp_register(void)
{
    bt_manager_test_log_call("bt_hfp_ag_register_callback");
    s_hfp_register_calls++;
    return s_hfp_register_result;
}

static esp_err_t mock_hfp_init(void)
{
    bt_manager_test_log_call("bt_hfp_ag_init");
    s_hfp_init_calls++;
    if (s_hfp_init_result != ESP_OK) {
        return s_hfp_init_result;
    }
    bt_hfp_ag_handle_profile_result(
        s_hfp_init_callback_failure
            ? BT_HFP_AG_PROFILE_INIT_FAILED
            : BT_HFP_AG_PROFILE_INIT_SUCCESS);
    return ESP_OK;
}

static esp_err_t mock_hfp_deinit(void)
{
    bt_manager_test_log_call("bt_hfp_ag_deinit");
    s_hfp_deinit_calls++;
    if (s_hfp_deinit_result != ESP_OK) {
        return s_hfp_deinit_result;
    }
    bt_hfp_ag_handle_profile_result(BT_HFP_AG_PROFILE_DEINIT_SUCCESS);
    return ESP_OK;
}

static esp_err_t mock_unknown_at(const char *peer_mac)
{
    (void)peer_mac;
    return ESP_OK;
}

void setUp(void)
{
    memset(s_calls, 0, sizeof(s_calls));
    s_call_count = 0;
    s_hfp_register_result = ESP_OK;
    s_hfp_init_result = ESP_OK;
    s_hfp_init_callback_failure = false;
    s_hfp_deinit_result = ESP_OK;
    s_hfp_register_calls = 0;
    s_hfp_init_calls = 0;
    s_hfp_deinit_calls = 0;

    mock_avrc_reset();
    mock_a2dp_reset();
    mock_gap_reset();
    bt_hfp_ag_force_cleanup_after_stack_shutdown();
    bt_duplex_state_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, bt_duplex_state_init());

    const bt_hfp_ag_platform_ops_t ops = {
        .register_callback = mock_hfp_register,
        .profile_init = mock_hfp_init,
        .profile_deinit = mock_hfp_deinit,
        .unknown_at_error = mock_unknown_at,
    };
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_ag_test_set_platform_ops(&ops));
}

void tearDown(void)
{
    bt_hfp_ag_force_cleanup_after_stack_shutdown();
    bt_duplex_state_deinit();
}

static void assert_base_init_order(void)
{
    static const char *const expected[] = {
        "esp_avrc_ct_init",
        "esp_avrc_ct_register_callback",
        "esp_a2d_source_init",
        "esp_a2d_register_callback",
        "esp_a2d_source_register_data_callback",
        "bt_hfp_ag_register_callback",
        "bt_hfp_ag_init",
    };
    TEST_ASSERT_EQUAL_INT((int)(sizeof(expected) / sizeof(expected[0])),
                          s_call_count);
    for (int i = 0; i < s_call_count; ++i) {
        TEST_ASSERT_EQUAL_STRING(expected[i], s_calls[i]);
    }
}

void test_manager_profile_init_includes_confirmed_hfp(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, bt_manager_test_init_profiles());
    assert_base_init_order();
    TEST_ASSERT_EQUAL_INT(1, s_hfp_register_calls);
    TEST_ASSERT_EQUAL_INT(1, s_hfp_init_calls);
    TEST_ASSERT_EQUAL_INT(0, s_hfp_deinit_calls);
}

void test_manager_hfp_registration_failure_rolls_back_previous_profiles(void)
{
    s_hfp_register_result = ESP_ERR_NO_MEM;
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, bt_manager_test_init_profiles());
    TEST_ASSERT_EQUAL_INT(1, s_hfp_register_calls);
    TEST_ASSERT_EQUAL_INT(0, s_hfp_init_calls);
    TEST_ASSERT_EQUAL_INT(1, s_hfp_deinit_calls);
    TEST_ASSERT_TRUE(mock_a2dp_deinit_was_called());
    TEST_ASSERT_TRUE(mock_avrc_deinit_was_called());
}

void test_manager_hfp_immediate_init_failure_preserves_error_and_rolls_back(void)
{
    s_hfp_init_result = ESP_ERR_NO_MEM;
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, bt_manager_test_init_profiles());
    TEST_ASSERT_EQUAL_INT(1, s_hfp_register_calls);
    TEST_ASSERT_EQUAL_INT(1, s_hfp_init_calls);
    TEST_ASSERT_EQUAL_INT(1, s_hfp_deinit_calls);
    TEST_ASSERT_TRUE(mock_a2dp_deinit_was_called());
    TEST_ASSERT_TRUE(mock_avrc_deinit_was_called());
}

void test_manager_hfp_callback_failure_rolls_back_previous_profiles(void)
{
    s_hfp_init_callback_failure = true;
    TEST_ASSERT_EQUAL(ESP_FAIL, bt_manager_test_init_profiles());
    TEST_ASSERT_EQUAL_INT(1, s_hfp_deinit_calls);
    TEST_ASSERT_TRUE(mock_a2dp_deinit_was_called());
    TEST_ASSERT_TRUE(mock_avrc_deinit_was_called());
}

void test_manager_a2dp_failure_does_not_touch_hfp(void)
{
    mock_a2dp_set_data_callback_result(ESP_ERR_NO_MEM);
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, bt_manager_test_init_profiles());
    TEST_ASSERT_EQUAL_INT(0, s_hfp_register_calls);
    TEST_ASSERT_EQUAL_INT(0, s_hfp_init_calls);
    TEST_ASSERT_EQUAL_INT(0, s_hfp_deinit_calls);
    TEST_ASSERT_TRUE(mock_a2dp_deinit_was_called());
    TEST_ASSERT_TRUE(mock_avrc_deinit_was_called());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_manager_profile_init_includes_confirmed_hfp);
    RUN_TEST(test_manager_hfp_registration_failure_rolls_back_previous_profiles);
    RUN_TEST(test_manager_hfp_immediate_init_failure_preserves_error_and_rolls_back);
    RUN_TEST(test_manager_hfp_callback_failure_rolls_back_previous_profiles);
    RUN_TEST(test_manager_a2dp_failure_does_not_touch_hfp);
    return UNITY_END();
}
