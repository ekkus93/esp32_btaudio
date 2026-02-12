/**
 * @file test_bt_manager_edge_cases.c
 * @brief Unit tests for bt_manager.c edge cases and error paths
 *
 * Phase 5.1: BT Manager Missing Coverage
 * 
 * Tests cover:
 * - Profile initialization partial failures (5 steps can fail independently)
 * - Connection state machine edge cases (invalid MAC, already connected)
 * - Pairing/unpairing validation (invalid MAC, NVS mismatches)
 */

#include <string.h>
#include "unity.h"
#include "bt_manager.h"
#include "mock_a2dp.h"
#include "mock_avrc.h"

// Test hook to access internal profile init function
extern esp_err_t bt_manager_test_init_profiles(void);

void setUp(void)
{
    // Reset all mocks before each test
    mock_a2dp_reset();
    mock_avrc_reset();
}

void tearDown(void)
{
    // Clean up after each test
}

/**
 * TDD Test 1: AVRC init failure should propagate ESP_FAIL
 * 
 * Behavior: If esp_avrc_ct_init() fails, bt_manager_init_profiles() 
 * must return ESP_FAIL without attempting remaining steps.
 */
void test_bt_manager_profiles_init_avrc_init_fails(void)
{
    // Arrange: Make AVRC init fail
    mock_avrc_set_init_result(ESP_FAIL);
    
    // Act: Call profile init
    esp_err_t result = bt_manager_test_init_profiles();
    
    // Assert: Should return ESP_FAIL
    TEST_ASSERT_EQUAL(ESP_FAIL, result);
    
    // Assert: AVRC init was attempted
    TEST_ASSERT_TRUE(mock_avrc_was_init_called());
    
    // Assert: A2DP init should NOT have been attempted (early return)
    TEST_ASSERT_FALSE(mock_a2dp_was_init_called());
}

/**
 * TDD Test 2: AVRC callback registration failure should propagate ESP_FAIL
 * 
 * Behavior: If esp_avrc_ct_register_callback() fails after successful init,
 * bt_manager_init_profiles() must return ESP_FAIL.
 */
void test_bt_manager_profiles_init_avrc_callback_fails(void)
{
    // Arrange: AVRC init succeeds, but callback registration fails
    mock_avrc_set_init_result(ESP_OK);
    mock_avrc_set_callback_result(ESP_FAIL);
    
    // Act: Call profile init
    esp_err_t result = bt_manager_test_init_profiles();
    
    // Assert: Should return ESP_FAIL
    TEST_ASSERT_EQUAL(ESP_FAIL, result);
    
    // Assert: AVRC was initialized
    TEST_ASSERT_TRUE(mock_avrc_was_init_called());
    
    // Assert: A2DP init should NOT have been attempted (early return)
    TEST_ASSERT_FALSE(mock_a2dp_was_init_called());
}

/**
 * TDD Test 3: A2DP init failure should propagate ESP_FAIL
 * 
 * Behavior: If esp_a2d_source_init() fails after AVRC succeeds,
 * bt_manager_init_profiles() must return ESP_FAIL.
 */
void test_bt_manager_profiles_init_a2dp_init_fails(void)
{
    // This test can work now - mock_a2dp already has mock_a2dp_set_init_result()
    
    // Arrange: Make A2DP init fail
    mock_a2dp_set_init_result(ESP_BT_STATUS_FAIL);
    
    // Act: Call profile init
    esp_err_t result = bt_manager_test_init_profiles();
    
    // Assert: Should return ESP_FAIL
    TEST_ASSERT_EQUAL(ESP_FAIL, result);
    
    // Assert: A2DP init was attempted
    TEST_ASSERT_TRUE(mock_a2dp_was_init_called());
}

/**
 * TDD Test 4: A2DP callback registration failure should propagate ESP_FAIL
 * 
 * Behavior: If esp_a2d_register_callback() fails after A2DP init succeeds,
 * bt_manager_init_profiles() must return ESP_FAIL.
 */
void test_bt_manager_profiles_init_a2dp_callback_fails(void)
{
    // Arrange: A2DP init succeeds, but callback registration fails
    mock_a2dp_set_init_result(ESP_BT_STATUS_SUCCESS);
    mock_a2dp_set_callback_result(ESP_BT_STATUS_FAIL);
    
    // Act: Call profile init
    esp_err_t result = bt_manager_test_init_profiles();
    
    // Assert: Should return ESP_FAIL
    TEST_ASSERT_EQUAL(ESP_FAIL, result);
    
    // Assert: A2DP was initialized
    TEST_ASSERT_TRUE(mock_a2dp_was_init_called());
}

/**
 * TDD Test 5: A2DP data callback registration failure should propagate ESP_FAIL
 * 
 * Behavior: If esp_a2d_source_register_data_callback() fails,
 * bt_manager_init_profiles() must return ESP_FAIL.
 */
void test_bt_manager_profiles_init_a2dp_data_callback_fails(void)
{
    // Arrange: All previous steps succeed, but data callback fails
    mock_a2dp_set_init_result(ESP_BT_STATUS_SUCCESS);
    mock_a2dp_set_callback_result(ESP_BT_STATUS_SUCCESS);
    mock_a2dp_set_data_callback_result(ESP_BT_STATUS_FAIL);
    
    // Act: Call profile init
    esp_err_t result = bt_manager_test_init_profiles();
    
    // Assert: Should return ESP_FAIL
    TEST_ASSERT_EQUAL(ESP_FAIL, result);
    
    // Assert: All steps were attempted
    TEST_ASSERT_TRUE(mock_a2dp_was_init_called());
}

/**
 * TDD Test 6: All profile init steps succeed
 * 
 * Behavior: When all 5 steps succeed, bt_manager_init_profiles() returns ESP_OK.
 * This is the happy path - should already pass.
 */
void test_bt_manager_profiles_init_all_succeed(void)
{
    // Arrange: Default mock state (all succeed)
    // (No setup needed - mocks default to success)
    
    // Act: Call profile init
    esp_err_t result = bt_manager_test_init_profiles();
    
    // Assert: Should return ESP_OK
    TEST_ASSERT_EQUAL(ESP_OK, result);
    
    // Assert: All init steps were called
    TEST_ASSERT_TRUE(mock_avrc_was_init_called());
    TEST_ASSERT_TRUE(mock_a2dp_was_init_called());
}

// Unity test runner
int main(void)
{
    UNITY_BEGIN();
    
    // Profile Initialization Tests (5.1.1)
    RUN_TEST(test_bt_manager_profiles_init_avrc_init_fails);
    RUN_TEST(test_bt_manager_profiles_init_avrc_callback_fails);
    RUN_TEST(test_bt_manager_profiles_init_a2dp_init_fails);
    RUN_TEST(test_bt_manager_profiles_init_a2dp_callback_fails);
    RUN_TEST(test_bt_manager_profiles_init_a2dp_data_callback_fails);
    RUN_TEST(test_bt_manager_profiles_init_all_succeed);
    
    return UNITY_END();
}
