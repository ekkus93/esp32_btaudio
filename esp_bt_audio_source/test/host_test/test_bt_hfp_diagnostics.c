#include "unity.h"

#include <string.h>

#include "bt_app_core.h"
#include "bt_hfp_audio.h"
#include "bt_hfp_manager.h"
#include "hfp_i2s_output.h"

static esp_err_t s_status_result;
static esp_err_t s_audio_result;
static bt_hfp_audio_snapshot_t s_audio_snapshot;
static size_t s_free_internal;
static size_t s_minimum_free;
static size_t s_largest_internal;
static esp_err_t s_hfp_stack_result;
static size_t s_hfp_stack_value;
static esp_err_t s_i2s_stack_result;
static size_t s_i2s_stack_value;

esp_err_t bt_manager_hfp_get_status(bt_hfp_manager_status_t *out)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    if (s_status_result == ESP_OK) {
        memset(out, 0, sizeof(*out));
        out->manager_initialized = true;
    }
    return s_status_result;
}

esp_err_t bt_hfp_audio_get_snapshot(bt_hfp_audio_snapshot_t *out)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    if (s_audio_result == ESP_OK) *out = s_audio_snapshot;
    return s_audio_result;
}

esp_err_t bt_app_task_get_stack_high_water_mark(size_t *bytes_out)
{
    (void)bytes_out;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t hfp_i2s_output_get_stack_high_water_mark(size_t *bytes_out)
{
    (void)bytes_out;
    return ESP_ERR_NOT_SUPPORTED;
}

static size_t get_free_internal(void) { return s_free_internal; }
static size_t get_minimum_free(void) { return s_minimum_free; }
static size_t get_largest_internal(void) { return s_largest_internal; }

static esp_err_t get_hfp_stack(size_t *out)
{
    if (s_hfp_stack_result == ESP_OK) *out = s_hfp_stack_value;
    return s_hfp_stack_result;
}

static esp_err_t get_i2s_stack(size_t *out)
{
    if (s_i2s_stack_result == ESP_OK) *out = s_i2s_stack_value;
    return s_i2s_stack_result;
}

void setUp(void)
{
    bt_manager_hfp_fd13_test_reset_platform_ops();
    s_status_result = ESP_OK;
    s_audio_result = ESP_OK;
    memset(&s_audio_snapshot, 0, sizeof(s_audio_snapshot));
    s_audio_snapshot.initialized = true;
    s_audio_snapshot.callback_last_us = 123U;
    s_audio_snapshot.callback_max_us = 456U;
    s_audio_snapshot.callback_over_budget = 7U;
    s_free_internal = 123456U;
    s_minimum_free = 65432U;
    s_largest_internal = 54321U;
    s_hfp_stack_result = ESP_OK;
    s_hfp_stack_value = 4096U;
    s_i2s_stack_result = ESP_OK;
    s_i2s_stack_value = 2048U;

    const bt_hfp_manager_diagnostics_platform_ops_t ops = {
        .free_internal_bytes = get_free_internal,
        .minimum_free_heap_bytes_lifetime = get_minimum_free,
        .largest_internal_free_block_bytes = get_largest_internal,
        .hfp_app_task_min_free_stack_bytes_lifetime = get_hfp_stack,
        .i2s_writer_task_min_free_stack_bytes_lifetime = get_i2s_stack,
    };
    TEST_ASSERT_EQUAL(ESP_OK,
                      bt_manager_hfp_fd13_test_set_platform_ops(&ops));
}

void tearDown(void)
{
    bt_manager_hfp_fd13_test_reset_platform_ops();
}

static void assert_bytes_unchanged(const void *value, size_t size,
                                   unsigned char expected)
{
    const unsigned char *bytes = value;
    for (size_t index = 0U; index < size; ++index) {
        TEST_ASSERT_EQUAL_HEX8(expected, bytes[index]);
    }
}

static void test_invalid_output_is_rejected(void)
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      bt_manager_hfp_get_diagnostics(NULL));
}

static void test_full_snapshot_reports_exact_values(void)
{
    bt_hfp_manager_diagnostics_t diagnostics;
    memset(&diagnostics, 0, sizeof(diagnostics));
    TEST_ASSERT_EQUAL(ESP_OK,
                      bt_manager_hfp_get_diagnostics(&diagnostics));
    TEST_ASSERT_TRUE(diagnostics.heap_available);
    TEST_ASSERT_EQUAL_UINT(123456U, diagnostics.free_internal_bytes);
    TEST_ASSERT_EQUAL_UINT(65432U,
                           diagnostics.minimum_free_heap_bytes_lifetime);
    TEST_ASSERT_EQUAL_UINT(54321U,
                           diagnostics.largest_internal_free_block_bytes);
    TEST_ASSERT_TRUE(diagnostics.hfp_app_task_stack_available);
    TEST_ASSERT_EQUAL_UINT(
        4096U, diagnostics.hfp_app_task_min_free_stack_bytes_lifetime);
    TEST_ASSERT_TRUE(diagnostics.i2s_writer_task_stack_available);
    TEST_ASSERT_EQUAL_UINT(
        2048U, diagnostics.i2s_writer_task_min_free_stack_bytes_lifetime);
    TEST_ASSERT_TRUE(diagnostics.incoming_callback_available);
    TEST_ASSERT_EQUAL_UINT32(BT_HFP_AUDIO_CALLBACK_BUDGET_US,
                             diagnostics.incoming_callback_budget_us);
    TEST_ASSERT_EQUAL_UINT32(123U, diagnostics.incoming_callback_last_us);
    TEST_ASSERT_EQUAL_UINT32(
        456U, diagnostics.incoming_callback_max_us_lifetime);
    TEST_ASSERT_EQUAL_UINT64(
        7U, diagnostics.incoming_callback_over_budget_lifetime);
}

static void test_expected_unavailable_sources_are_explicit(void)
{
    s_hfp_stack_result = ESP_ERR_NOT_FOUND;
    s_i2s_stack_result = ESP_ERR_NOT_SUPPORTED;
    s_audio_result = ESP_ERR_INVALID_STATE;

    bt_hfp_manager_diagnostics_t diagnostics;
    memset(&diagnostics, 0xA5, sizeof(diagnostics));
    TEST_ASSERT_EQUAL(ESP_OK,
                      bt_manager_hfp_get_diagnostics(&diagnostics));
    TEST_ASSERT_TRUE(diagnostics.heap_available);
    TEST_ASSERT_FALSE(diagnostics.hfp_app_task_stack_available);
    TEST_ASSERT_EQUAL_UINT(0U,
        diagnostics.hfp_app_task_min_free_stack_bytes_lifetime);
    TEST_ASSERT_FALSE(diagnostics.i2s_writer_task_stack_available);
    TEST_ASSERT_EQUAL_UINT(0U,
        diagnostics.i2s_writer_task_min_free_stack_bytes_lifetime);
    TEST_ASSERT_FALSE(diagnostics.incoming_callback_available);
    TEST_ASSERT_EQUAL_UINT32(0U, diagnostics.incoming_callback_budget_us);
}

static void test_manager_status_failure_is_exact_and_does_not_commit(void)
{
    s_status_result = ESP_ERR_INVALID_STATE;
    bt_hfp_manager_diagnostics_t diagnostics;
    memset(&diagnostics, 0xA5, sizeof(diagnostics));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      bt_manager_hfp_get_diagnostics(&diagnostics));
    assert_bytes_unchanged(&diagnostics, sizeof(diagnostics), 0xA5U);
}

static void test_unexpected_hfp_stack_failure_is_exact_and_not_committed(void)
{
    s_hfp_stack_result = ESP_ERR_TIMEOUT;
    bt_hfp_manager_diagnostics_t diagnostics;
    memset(&diagnostics, 0xA5, sizeof(diagnostics));
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT,
                      bt_manager_hfp_get_diagnostics(&diagnostics));
    assert_bytes_unchanged(&diagnostics, sizeof(diagnostics), 0xA5U);
}

static void test_unexpected_i2s_stack_failure_is_exact_and_not_committed(void)
{
    s_i2s_stack_result = ESP_FAIL;
    bt_hfp_manager_diagnostics_t diagnostics;
    memset(&diagnostics, 0xA5, sizeof(diagnostics));
    TEST_ASSERT_EQUAL(ESP_FAIL,
                      bt_manager_hfp_get_diagnostics(&diagnostics));
    assert_bytes_unchanged(&diagnostics, sizeof(diagnostics), 0xA5U);
}

static void test_callback_snapshot_failure_is_exact_and_not_committed(void)
{
    s_audio_result = ESP_ERR_TIMEOUT;
    bt_hfp_manager_diagnostics_t diagnostics;
    memset(&diagnostics, 0xA5, sizeof(diagnostics));
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT,
                      bt_manager_hfp_get_diagnostics(&diagnostics));
    assert_bytes_unchanged(&diagnostics, sizeof(diagnostics), 0xA5U);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_invalid_output_is_rejected);
    RUN_TEST(test_full_snapshot_reports_exact_values);
    RUN_TEST(test_expected_unavailable_sources_are_explicit);
    RUN_TEST(test_manager_status_failure_is_exact_and_does_not_commit);
    RUN_TEST(test_unexpected_hfp_stack_failure_is_exact_and_not_committed);
    RUN_TEST(test_unexpected_i2s_stack_failure_is_exact_and_not_committed);
    RUN_TEST(test_callback_snapshot_failure_is_exact_and_not_committed);
    return UNITY_END();
}
