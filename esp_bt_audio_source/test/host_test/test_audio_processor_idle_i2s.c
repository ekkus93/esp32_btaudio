#include "unity.h"
#include "../../main/include/audio_processor.h"

void setUp(void) {}
void tearDown(void) {}

void test_idle_i2s_failures_should_reenable_synth_when_idle(void)
{
    bool synth_after = false;
    int failures_after = -1;

    audio_processor_test_idle_i2s_failures(25 /* >= threshold */, false /* synth_enabled */, 0 /* beep_remaining */, &synth_after, &failures_after);

    TEST_ASSERT_TRUE_MESSAGE(synth_after, "Synth keepalive should be re-enabled when idle timeouts accumulate");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, failures_after, "Failure counter should reset after parking to synth");
}

void test_idle_i2s_failures_should_not_toggle_synth_when_beep_pending(void)
{
    bool synth_after = true;
    int failures_after = -1;

    audio_processor_test_idle_i2s_failures(25 /* >= threshold */, false /* synth_enabled */, 16 /* beep bytes pending */, &synth_after, &failures_after);

    TEST_ASSERT_FALSE_MESSAGE(synth_after, "Synth should stay disabled when beep audio is still pending");
    TEST_ASSERT_EQUAL_INT_MESSAGE(25, failures_after, "Failure counter should remain when synth is not re-enabled");
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_idle_i2s_failures_should_reenable_synth_when_idle);
    RUN_TEST(test_idle_i2s_failures_should_not_toggle_synth_when_beep_pending);
    return UNITY_END();
}