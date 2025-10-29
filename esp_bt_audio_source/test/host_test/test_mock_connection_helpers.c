/* Verify mock connection helpers update test-visible state even when
 * the manager was not explicitly initialized by the test harness.
 */

#include "unity.h"
#include <stdio.h>

/* Prototypes for the bt_manager mock helpers under test. These are
 * provided by the bt_manager component when built into the host test
 * target (we link bt_manager.c directly into the test binary). */
extern void bt_manager_mock_connection_established(const char* mac, const char* name);
extern void bt_manager_mock_connection_closed(const char* mac);

/* The test-visible getter is provided by our host-mode mock in
 * mocks/mock_audio_and_btstate.c and returns 1 when connected. */
extern int bt_get_connection_state(void);

void setUp(void) {
    /* No initialization on purpose: ensure the mock helpers still work */
}

void tearDown(void) {
}

void test_mock_connection_helpers_without_init(void) {
    /* Ensure starting state is disconnected (some harnesses default to 0)
     * but be tolerant if already connected; we'll explicitly close then
     * open to exercise both directions. */
    bt_manager_mock_connection_closed("aa:bb:cc:11:22:33");
    TEST_ASSERT_EQUAL(0, bt_get_connection_state());

    /* Establish connection using the mock helper without calling init().
     * The function should set internal state and make bt_get_connection_state
     * return 1. */
    bt_manager_mock_connection_established("aa:bb:cc:11:22:33", "MockSpeaker");
    TEST_ASSERT_EQUAL(1, bt_get_connection_state());

    /* Close the connection and ensure the getter returns 0 again. */
    bt_manager_mock_connection_closed("aa:bb:cc:11:22:33");
    TEST_ASSERT_EQUAL(0, bt_get_connection_state());
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_mock_connection_helpers_without_init);
    return UNITY_END();
}
