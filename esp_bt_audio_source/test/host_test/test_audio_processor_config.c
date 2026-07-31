#include "unity.h"
#include "audio_processor.h"
#include "audio_processor_internal.h"

#include <string.h>

void audio_processor_core_stub_reset(void);
void audio_processor_core_stub_set_i2s_running(bool running);
void audio_processor_core_stub_set_i2s_init_result(esp_err_t result);
uint32_t audio_processor_core_stub_get_i2s_init_calls(void);
uint32_t audio_processor_core_stub_get_nvs_set_i2s_pins_calls(void);
void audio_processor_core_stub_get_last_i2s_pins(int *bclk, int *ws, int *din, int *dout);

void setUp(void)
{
    audio_processor_core_stub_reset();

    s_is_initialized = false;
    s_is_running = false;
    s_force_synth = false;
    s_volume_gain = 0;
    s_audio_diag_enabled = false;

    s_audio_config.sample_rate = AUDIO_SAMPLE_RATE_44K;
    s_audio_config.bit_depth = AUDIO_BIT_DEPTH_16;
    s_audio_config.channels = AUDIO_CHANNEL_STEREO;
    s_audio_config.mute = false;

    memset(&s_audio_stats, 0, sizeof(s_audio_stats));
    s_probe_captured = 0;
    s_probe_target = 0;
    memset(s_probe_buf, 0, sizeof(s_probe_buf));

    s_audio_ring = NULL;
}

void tearDown(void)
{
}

/* ================= BT-1 (docs/UNIT_TESTS2_TODO.md): audio_processor_config.c ================= */

/* ---- uninitialized-state guard: every setter rejects when !s_is_initialized ---- */

void test_set_sample_rate_rejects_when_not_initialized(void)
{
    s_is_initialized = false;
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_STATE, audio_processor_set_sample_rate(AUDIO_SAMPLE_RATE_48K));
}

void test_set_mute_rejects_when_not_initialized(void)
{
    s_is_initialized = false;
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_STATE, audio_processor_set_mute(true));
}

void test_set_channels_rejects_when_not_initialized(void)
{
    s_is_initialized = false;
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_STATE, audio_processor_set_channels(AUDIO_CHANNEL_MONO));
}

void test_set_bit_depth_rejects_when_not_initialized(void)
{
    s_is_initialized = false;
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_STATE, audio_processor_set_bit_depth(AUDIO_BIT_DEPTH_24));
}

void test_set_i2s_pins_rejects_when_not_initialized(void)
{
    s_is_initialized = false;
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_STATE, audio_processor_set_i2s_pins(1, 2, 3, 4));
}

/* ---- set_mute ---- */

void test_set_mute_toggles_and_reflected_in_config_and_status(void)
{
    s_is_initialized = true;
    s_audio_config.mute = false;

    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_set_mute(true));
    TEST_ASSERT_TRUE(s_audio_config.mute);
    audio_config_t cfg;
    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_get_config(&cfg));
    TEST_ASSERT_TRUE(cfg.mute);
    audio_status_t st;
    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_get_status(&st));
    TEST_ASSERT_TRUE(st.mute);

    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_set_mute(false));
    TEST_ASSERT_FALSE(s_audio_config.mute);

    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_set_mute(true));
    TEST_ASSERT_TRUE(s_audio_config.mute);
}

/* ---- set_channels ---- */

void test_set_channels_rejects_invalid_enum(void)
{
    s_is_initialized = true;
    s_audio_config.channels = AUDIO_CHANNEL_STEREO;
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, audio_processor_set_channels((audio_channel_t)0));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, audio_processor_set_channels((audio_channel_t)3));
    /* rejection leaves the config untouched */
    TEST_ASSERT_EQUAL_INT(AUDIO_CHANNEL_STEREO, s_audio_config.channels);
}

void test_set_channels_same_value_is_noop_no_reconfigure(void)
{
    s_is_initialized = true;
    s_is_running = false;
    s_audio_config.channels = AUDIO_CHANNEL_STEREO;

    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_set_channels(AUDIO_CHANNEL_STEREO));
    TEST_ASSERT_EQUAL_UINT32(0, audio_processor_core_stub_get_i2s_init_calls());
}

void test_set_channels_while_stopped_reconfigures_without_restart(void)
{
    s_is_initialized = true;
    s_is_running = false;
    s_audio_config.channels = AUDIO_CHANNEL_STEREO;

    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_set_channels(AUDIO_CHANNEL_MONO));
    TEST_ASSERT_EQUAL_INT(AUDIO_CHANNEL_MONO, s_audio_config.channels);
    TEST_ASSERT_EQUAL_UINT32(1, audio_processor_core_stub_get_i2s_init_calls());
    TEST_ASSERT_FALSE(s_is_running); /* was never started */
}

void test_set_channels_while_running_stops_reconfigures_restarts(void)
{
    s_is_initialized = true;
    s_is_running = true;
    audio_processor_core_stub_set_i2s_running(true);
    s_audio_config.channels = AUDIO_CHANNEL_STEREO;

    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_set_channels(AUDIO_CHANNEL_MONO));
    TEST_ASSERT_EQUAL_INT(AUDIO_CHANNEL_MONO, s_audio_config.channels);
    TEST_ASSERT_EQUAL_UINT32(1, audio_processor_core_stub_get_i2s_init_calls());
    TEST_ASSERT_TRUE(s_is_running); /* restarted */
}

void test_set_channels_propagates_i2s_init_failure(void)
{
    s_is_initialized = true;
    s_is_running = false;
    s_audio_config.channels = AUDIO_CHANNEL_STEREO;
    audio_processor_core_stub_set_i2s_init_result(ESP_FAIL);

    TEST_ASSERT_EQUAL_INT(ESP_FAIL, audio_processor_set_channels(AUDIO_CHANNEL_MONO));
    /* Note: the config field is updated BEFORE i2s_manager_init is attempted
     * (see audio_processor_config.c) -- the error is real but s_audio_config
     * already reflects the new value on failure. Documenting actual behavior,
     * not asserting a rollback that doesn't exist. */
    TEST_ASSERT_EQUAL_INT(AUDIO_CHANNEL_MONO, s_audio_config.channels);
}

/* ---- set_bit_depth: same shape as set_channels, but NO enum validation
 * exists in the source -- any int value is accepted. Documented, not assumed
 * a bug (see docs/UNIT_TESTS2_TODO.md BT-1). ---- */

void test_set_bit_depth_same_value_is_noop_no_reconfigure(void)
{
    s_is_initialized = true;
    s_is_running = false;
    s_audio_config.bit_depth = AUDIO_BIT_DEPTH_16;

    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_set_bit_depth(AUDIO_BIT_DEPTH_16));
    TEST_ASSERT_EQUAL_UINT32(0, audio_processor_core_stub_get_i2s_init_calls());
}

void test_set_bit_depth_while_stopped_reconfigures_without_restart(void)
{
    s_is_initialized = true;
    s_is_running = false;
    s_audio_config.bit_depth = AUDIO_BIT_DEPTH_16;

    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_set_bit_depth(AUDIO_BIT_DEPTH_24));
    TEST_ASSERT_EQUAL_INT(AUDIO_BIT_DEPTH_24, s_audio_config.bit_depth);
    TEST_ASSERT_EQUAL_UINT32(1, audio_processor_core_stub_get_i2s_init_calls());
    TEST_ASSERT_FALSE(s_is_running);
}

void test_set_bit_depth_while_running_stops_reconfigures_restarts(void)
{
    s_is_initialized = true;
    s_is_running = true;
    audio_processor_core_stub_set_i2s_running(true);
    s_audio_config.bit_depth = AUDIO_BIT_DEPTH_16;

    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_set_bit_depth(AUDIO_BIT_DEPTH_32));
    TEST_ASSERT_EQUAL_INT(AUDIO_BIT_DEPTH_32, s_audio_config.bit_depth);
    TEST_ASSERT_EQUAL_UINT32(1, audio_processor_core_stub_get_i2s_init_calls());
    TEST_ASSERT_TRUE(s_is_running);
}

void test_set_bit_depth_propagates_i2s_init_failure(void)
{
    s_is_initialized = true;
    s_is_running = false;
    s_audio_config.bit_depth = AUDIO_BIT_DEPTH_16;
    audio_processor_core_stub_set_i2s_init_result(ESP_FAIL);

    TEST_ASSERT_EQUAL_INT(ESP_FAIL, audio_processor_set_bit_depth(AUDIO_BIT_DEPTH_24));
}

/* ---- set_sample_rate: same shape as set_channels/set_bit_depth ---- */

void test_set_sample_rate_same_value_is_noop_no_reconfigure(void)
{
    s_is_initialized = true;
    s_is_running = false;
    s_audio_config.sample_rate = AUDIO_SAMPLE_RATE_44K;

    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_set_sample_rate(AUDIO_SAMPLE_RATE_44K));
    TEST_ASSERT_EQUAL_UINT32(0, audio_processor_core_stub_get_i2s_init_calls());
}

void test_set_sample_rate_while_stopped_reconfigures_without_restart(void)
{
    s_is_initialized = true;
    s_is_running = false;
    s_audio_config.sample_rate = AUDIO_SAMPLE_RATE_44K;

    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_set_sample_rate(AUDIO_SAMPLE_RATE_48K));
    TEST_ASSERT_EQUAL_INT(AUDIO_SAMPLE_RATE_48K, s_audio_config.sample_rate);
    TEST_ASSERT_EQUAL_UINT32(1, audio_processor_core_stub_get_i2s_init_calls());
    TEST_ASSERT_FALSE(s_is_running);
}

void test_set_sample_rate_while_running_stops_reconfigures_restarts(void)
{
    s_is_initialized = true;
    s_is_running = true;
    audio_processor_core_stub_set_i2s_running(true);
    s_audio_config.sample_rate = AUDIO_SAMPLE_RATE_44K;

    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_set_sample_rate(AUDIO_SAMPLE_RATE_48K));
    TEST_ASSERT_EQUAL_INT(AUDIO_SAMPLE_RATE_48K, s_audio_config.sample_rate);
    TEST_ASSERT_EQUAL_UINT32(1, audio_processor_core_stub_get_i2s_init_calls());
    TEST_ASSERT_TRUE(s_is_running);
}

void test_set_sample_rate_propagates_i2s_init_failure(void)
{
    s_is_initialized = true;
    s_is_running = false;
    s_audio_config.sample_rate = AUDIO_SAMPLE_RATE_44K;
    audio_processor_core_stub_set_i2s_init_result(ESP_FAIL);

    TEST_ASSERT_EQUAL_INT(ESP_FAIL, audio_processor_set_sample_rate(AUDIO_SAMPLE_RATE_48K));
}

/* Note (docs/UNIT_TESTS2_TODO.md BT-1): a "was_running, audio_processor_stop()
 * itself fails" case was planned, but audio_processor_stop() in this
 * UNIT_TEST harness only fails on !s_is_initialized / !s_is_running, both of
 * which are already false in the was_running=true scenario being tested --
 * there is no reachable failure path to inject here without a dedicated stop
 * failure hook that does not currently exist. Not fabricating one; noting
 * the gap instead of asserting a scenario the code can't actually produce. */

/* ---- set_i2s_pins ---- */

void test_set_i2s_pins_while_stopped_reconfigures_and_persists(void)
{
    s_is_initialized = true;
    s_is_running = false;

    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_set_i2s_pins(11, 12, 13, 14));
    TEST_ASSERT_EQUAL_INT(11, s_audio_config.i2s_bclk_pin);
    TEST_ASSERT_EQUAL_INT(12, s_audio_config.i2s_ws_pin);
    TEST_ASSERT_EQUAL_INT(13, s_audio_config.i2s_din_pin);
    TEST_ASSERT_EQUAL_INT(14, s_audio_config.i2s_dout_pin);
    TEST_ASSERT_EQUAL_UINT32(1, audio_processor_core_stub_get_i2s_init_calls());
    TEST_ASSERT_EQUAL_UINT32(1, audio_processor_core_stub_get_nvs_set_i2s_pins_calls());
    int bclk, ws, din, dout;
    audio_processor_core_stub_get_last_i2s_pins(&bclk, &ws, &din, &dout);
    TEST_ASSERT_EQUAL_INT(11, bclk);
    TEST_ASSERT_EQUAL_INT(12, ws);
    TEST_ASSERT_EQUAL_INT(13, din);
    TEST_ASSERT_EQUAL_INT(14, dout);
    TEST_ASSERT_FALSE(s_is_running);
}

void test_set_i2s_pins_while_running_stops_reconfigures_restarts(void)
{
    s_is_initialized = true;
    s_is_running = true;
    audio_processor_core_stub_set_i2s_running(true);

    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_set_i2s_pins(21, 22, 23, 24));
    TEST_ASSERT_EQUAL_UINT32(1, audio_processor_core_stub_get_i2s_init_calls());
    TEST_ASSERT_TRUE(s_is_running);
}

/* ---- configure_i2s (de-static'd for this test; unreachable via the public
 * setters, which never pass NULL through) ---- */

void test_configure_i2s_null_config_rejected(void)
{
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, configure_i2s(NULL));
}

/* ---- audio_processor_get_config ---- */

void test_get_config_rejects_when_not_initialized(void)
{
    s_is_initialized = false;
    audio_config_t cfg;
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_STATE, audio_processor_get_config(&cfg));
}

void test_get_config_rejects_null_output(void)
{
    s_is_initialized = true;
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, audio_processor_get_config(NULL));
}

void test_get_config_round_trip_after_setters(void)
{
    s_is_initialized = true;
    s_is_running = false;
    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_set_mute(true));
    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_set_channels(AUDIO_CHANNEL_MONO));
    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_set_bit_depth(AUDIO_BIT_DEPTH_24));
    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_set_sample_rate(AUDIO_SAMPLE_RATE_48K));

    audio_config_t cfg;
    TEST_ASSERT_EQUAL_INT(ESP_OK, audio_processor_get_config(&cfg));
    TEST_ASSERT_TRUE(cfg.mute);
    TEST_ASSERT_EQUAL_INT(AUDIO_CHANNEL_MONO, cfg.channels);
    TEST_ASSERT_EQUAL_INT(AUDIO_BIT_DEPTH_24, cfg.bit_depth);
    TEST_ASSERT_EQUAL_INT(AUDIO_SAMPLE_RATE_48K, cfg.sample_rate);
}

int main(void)
{
    UNITY_BEGIN();
    /* BT-1 (docs/UNIT_TESTS2_TODO.md): audio_processor_config.c */
    RUN_TEST(test_set_sample_rate_rejects_when_not_initialized);
    RUN_TEST(test_set_mute_rejects_when_not_initialized);
    RUN_TEST(test_set_channels_rejects_when_not_initialized);
    RUN_TEST(test_set_bit_depth_rejects_when_not_initialized);
    RUN_TEST(test_set_i2s_pins_rejects_when_not_initialized);

    RUN_TEST(test_set_mute_toggles_and_reflected_in_config_and_status);

    RUN_TEST(test_set_channels_rejects_invalid_enum);
    RUN_TEST(test_set_channels_same_value_is_noop_no_reconfigure);
    RUN_TEST(test_set_channels_while_stopped_reconfigures_without_restart);
    RUN_TEST(test_set_channels_while_running_stops_reconfigures_restarts);
    RUN_TEST(test_set_channels_propagates_i2s_init_failure);

    RUN_TEST(test_set_bit_depth_same_value_is_noop_no_reconfigure);
    RUN_TEST(test_set_bit_depth_while_stopped_reconfigures_without_restart);
    RUN_TEST(test_set_bit_depth_while_running_stops_reconfigures_restarts);
    RUN_TEST(test_set_bit_depth_propagates_i2s_init_failure);

    RUN_TEST(test_set_sample_rate_same_value_is_noop_no_reconfigure);
    RUN_TEST(test_set_sample_rate_while_stopped_reconfigures_without_restart);
    RUN_TEST(test_set_sample_rate_while_running_stops_reconfigures_restarts);
    RUN_TEST(test_set_sample_rate_propagates_i2s_init_failure);

    RUN_TEST(test_set_i2s_pins_while_stopped_reconfigures_and_persists);
    RUN_TEST(test_set_i2s_pins_while_running_stops_reconfigures_restarts);

    RUN_TEST(test_configure_i2s_null_config_rejected);

    RUN_TEST(test_get_config_rejects_when_not_initialized);
    RUN_TEST(test_get_config_rejects_null_output);
    RUN_TEST(test_get_config_round_trip_after_setters);
    return UNITY_END();
}
