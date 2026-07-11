/* WIFI-1a: host tests for the pure WiFi STA/AP fallback state machine. */
#include "unity.h"
#include "wifi_sm.h"

void setUp(void) {}
void tearDown(void) {}

/* --- start-up branching --- */

void test_start_with_creds_goes_sta(void)
{
    wifi_sm_t s;
    wifi_sm_init(&s, true, 5);
    TEST_ASSERT_EQUAL(WIFI_SM_INIT, wifi_sm_state(&s));
    TEST_ASSERT_EQUAL(WIFI_SM_ACT_START_STA, wifi_sm_start(&s));
    TEST_ASSERT_EQUAL(WIFI_SM_STA_CONNECTING, wifi_sm_state(&s));
}

void test_start_without_creds_goes_ap(void)
{
    wifi_sm_t s;
    wifi_sm_init(&s, false, 5);
    TEST_ASSERT_EQUAL(WIFI_SM_ACT_START_AP, wifi_sm_start(&s));
    TEST_ASSERT_EQUAL(WIFI_SM_AP_MODE, wifi_sm_state(&s));
    TEST_ASSERT_EQUAL_UINT32(1, s.ap_fallbacks);
}

/* --- happy path --- */

void test_connect_success(void)
{
    wifi_sm_t s;
    wifi_sm_init(&s, true, 5);
    wifi_sm_start(&s);
    TEST_ASSERT_EQUAL(WIFI_SM_ACT_NONE, wifi_sm_on_connected(&s));
    TEST_ASSERT_EQUAL(WIFI_SM_STA_CONNECTED, wifi_sm_state(&s));
    TEST_ASSERT_EQUAL_UINT32(1, s.sta_connects);
    TEST_ASSERT_EQUAL_INT(0, s.attempts);
}

/* --- retry then AP fallback --- */

void test_retries_then_ap_fallback(void)
{
    wifi_sm_t s;
    wifi_sm_init(&s, true, 3);
    wifi_sm_start(&s);
    /* fails 1,2 -> retry (stay connecting) */
    TEST_ASSERT_EQUAL(WIFI_SM_ACT_START_STA, wifi_sm_on_disconnected(&s));
    TEST_ASSERT_EQUAL(WIFI_SM_STA_CONNECTING, wifi_sm_state(&s));
    TEST_ASSERT_EQUAL(WIFI_SM_ACT_START_STA, wifi_sm_on_disconnected(&s));
    TEST_ASSERT_EQUAL(WIFI_SM_STA_CONNECTING, wifi_sm_state(&s));
    /* 3rd failure hits max_retries -> AP */
    TEST_ASSERT_EQUAL(WIFI_SM_ACT_START_AP, wifi_sm_on_disconnected(&s));
    TEST_ASSERT_EQUAL(WIFI_SM_AP_MODE, wifi_sm_state(&s));
    TEST_ASSERT_EQUAL_UINT32(1, s.ap_fallbacks);
    TEST_ASSERT_EQUAL_UINT32(3, s.disconnects);
}

void test_success_resets_retry_counter(void)
{
    wifi_sm_t s;
    wifi_sm_init(&s, true, 3);
    wifi_sm_start(&s);
    wifi_sm_on_disconnected(&s);          /* attempt 1 fails */
    wifi_sm_on_disconnected(&s);          /* attempt 2 fails */
    wifi_sm_on_connected(&s);             /* then succeeds */
    TEST_ASSERT_EQUAL_INT(0, s.attempts);
    /* a fresh drop starts the retry budget over, not at 2 */
    TEST_ASSERT_EQUAL(WIFI_SM_ACT_START_STA, wifi_sm_on_disconnected(&s));
    TEST_ASSERT_EQUAL(WIFI_SM_STA_CONNECTING, wifi_sm_state(&s));
}

void test_drop_from_connected_retries(void)
{
    wifi_sm_t s;
    wifi_sm_init(&s, true, 2);
    wifi_sm_start(&s);
    wifi_sm_on_connected(&s);
    /* link drops -> reconnect attempt */
    TEST_ASSERT_EQUAL(WIFI_SM_ACT_START_STA, wifi_sm_on_disconnected(&s));
    TEST_ASSERT_EQUAL(WIFI_SM_STA_CONNECTING, wifi_sm_state(&s));
    /* second consecutive failure hits max_retries=2 -> AP */
    TEST_ASSERT_EQUAL(WIFI_SM_ACT_START_AP, wifi_sm_on_disconnected(&s));
    TEST_ASSERT_EQUAL(WIFI_SM_AP_MODE, wifi_sm_state(&s));
}

/* --- provisioning transitions --- */

void test_set_creds_from_ap_restarts_sta(void)
{
    wifi_sm_t s;
    wifi_sm_init(&s, false, 5);
    wifi_sm_start(&s);                     /* AP mode */
    TEST_ASSERT_EQUAL(WIFI_SM_ACT_START_STA, wifi_sm_on_set_creds(&s));
    TEST_ASSERT_EQUAL(WIFI_SM_STA_CONNECTING, wifi_sm_state(&s));
    TEST_ASSERT_TRUE(s.has_creds);
    TEST_ASSERT_EQUAL_INT(0, s.attempts);
}

void test_clear_creds_returns_to_ap(void)
{
    wifi_sm_t s;
    wifi_sm_init(&s, true, 5);
    wifi_sm_start(&s);
    wifi_sm_on_connected(&s);
    TEST_ASSERT_EQUAL(WIFI_SM_ACT_START_AP, wifi_sm_on_clear_creds(&s));
    TEST_ASSERT_EQUAL(WIFI_SM_AP_MODE, wifi_sm_state(&s));
    TEST_ASSERT_FALSE(s.has_creds);
}

/* --- guards --- */

void test_max_retries_clamped(void)
{
    wifi_sm_t s;
    wifi_sm_init(&s, true, 0);
    TEST_ASSERT_EQUAL_INT(WIFI_SM_DEFAULT_MAX_RETRIES, s.max_retries);
    wifi_sm_init(&s, true, -3);
    TEST_ASSERT_EQUAL_INT(WIFI_SM_DEFAULT_MAX_RETRIES, s.max_retries);
}

void test_max_retries_one_goes_ap_on_first_failure(void)
{
    wifi_sm_t s;
    wifi_sm_init(&s, true, 1);
    wifi_sm_start(&s);
    TEST_ASSERT_EQUAL(WIFI_SM_ACT_START_AP, wifi_sm_on_disconnected(&s));
    TEST_ASSERT_EQUAL(WIFI_SM_AP_MODE, wifi_sm_state(&s));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_start_with_creds_goes_sta);
    RUN_TEST(test_start_without_creds_goes_ap);
    RUN_TEST(test_connect_success);
    RUN_TEST(test_retries_then_ap_fallback);
    RUN_TEST(test_success_resets_retry_counter);
    RUN_TEST(test_drop_from_connected_retries);
    RUN_TEST(test_set_creds_from_ap_restarts_sta);
    RUN_TEST(test_clear_creds_returns_to_ap);
    RUN_TEST(test_max_retries_clamped);
    RUN_TEST(test_max_retries_one_goes_ap_on_first_failure);
    return UNITY_END();
}
