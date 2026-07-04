/*
 * test_sanity — smoke test proving the host-test harness compiles, links
 * Unity, and CTest can run a suite (INFRA-1c). Replaced by real suites as
 * host-testable component logic lands (SIG-1a onward).
 */
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_harness_alive(void)
{
    TEST_ASSERT_EQUAL_INT(4, 2 + 2);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_harness_alive);
    return UNITY_END();
}
