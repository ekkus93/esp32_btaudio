#include "unity.h"
#include "i2s_audio.h"
#include "esp_err.h"
#include <stdint.h>

void setUp(void)
{
    i2s_driver_deinit();
}

void tearDown(void)
{
    i2s_driver_deinit();
}

static void test_driver_init_sets_state(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, i2s_driver_init(48000, I2S_DATA_BIT_WIDTH_24BIT, I2S_SLOT_MODE_STEREO));
    TEST_ASSERT_TRUE(i2s_is_driver_installed());
    TEST_ASSERT_EQUAL(I2S_SLOT_MODE_STEREO, i2s_get_channel_format());
}

static void test_reinit_changes_config(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, i2s_driver_init(44100, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO));
    TEST_ASSERT_TRUE(i2s_is_driver_installed());
    TEST_ASSERT_EQUAL(I2S_SLOT_MODE_STEREO, i2s_get_channel_format());

    TEST_ASSERT_EQUAL(ESP_OK, i2s_driver_init(32000, I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO));
    TEST_ASSERT_TRUE(i2s_is_driver_installed());
    TEST_ASSERT_EQUAL(I2S_SLOT_MODE_MONO, i2s_get_channel_format());
}

static void test_deinit_clears_install_flag(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, i2s_driver_init(44100, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO));
    TEST_ASSERT_TRUE(i2s_is_driver_installed());
    TEST_ASSERT_EQUAL(ESP_OK, i2s_driver_deinit());
    TEST_ASSERT_FALSE(i2s_is_driver_installed());
}

static void test_write_samples_requires_install(void)
{
    int16_t data[4] = {0};
    size_t bytes = 0;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, i2s_write_samples(data, 4, &bytes));
}

static void test_write_samples_sets_bytes_when_installed(void)
{
    int16_t data[4] = {1, 2, 3, 4};
    size_t bytes = 0;
    TEST_ASSERT_EQUAL(ESP_OK, i2s_driver_init(44100, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO));
    TEST_ASSERT_EQUAL(ESP_OK, i2s_write_samples(data, 4, &bytes));
    TEST_ASSERT_EQUAL(sizeof(data), bytes);
}

static void test_write_samples_rejects_null_buffer(void)
{
    size_t bytes = 0;
    TEST_ASSERT_EQUAL(ESP_OK, i2s_driver_init(44100, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, i2s_write_samples(NULL, 4, &bytes));
}

static void test_write_mono_samples_in_stereo_mode_ok(void)
{
    int16_t mono[2] = {10, 20};
    TEST_ASSERT_EQUAL(ESP_OK, i2s_driver_init(44100, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO));
    TEST_ASSERT_EQUAL(ESP_OK, i2s_write_mono_samples(mono, 2));
}

static void test_write_mono_samples_rejects_null(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, i2s_driver_init(44100, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, i2s_write_mono_samples(NULL, 2));
}

static void test_write_stereo_samples_in_mono_mode_ok(void)
{
    int16_t stereo[4] = {1, 2, 3, 4};
    TEST_ASSERT_EQUAL(ESP_OK, i2s_config_mono(44100, I2S_DATA_BIT_WIDTH_16BIT));
    TEST_ASSERT_EQUAL(I2S_SLOT_MODE_MONO, i2s_get_channel_format());
    TEST_ASSERT_EQUAL(ESP_OK, i2s_write_stereo_samples(stereo, 4));
}

static void test_mono_to_stereo_duplicates_samples(void)
{
    int16_t mono[3] = {1, -2, 3};
    int16_t stereo[6] = {0};
    TEST_ASSERT_EQUAL(ESP_OK, i2s_convert_mono_to_stereo(mono, stereo, 3));
    TEST_ASSERT_EQUAL_INT16(mono[0], stereo[0]);
    TEST_ASSERT_EQUAL_INT16(mono[0], stereo[1]);
    TEST_ASSERT_EQUAL_INT16(mono[1], stereo[2]);
    TEST_ASSERT_EQUAL_INT16(mono[1], stereo[3]);
    TEST_ASSERT_EQUAL_INT16(mono[2], stereo[4]);
    TEST_ASSERT_EQUAL_INT16(mono[2], stereo[5]);
}

static void test_stereo_to_mono_averages_channels(void)
{
    int16_t stereo[6] = {10, 14, -20, 4, 3, 5};
    int16_t mono[3] = {0};
    TEST_ASSERT_EQUAL(ESP_OK, i2s_convert_stereo_to_mono(stereo, mono, 3));
    TEST_ASSERT_EQUAL_INT16(12, mono[0]);
    TEST_ASSERT_EQUAL_INT16(-8, mono[1]);
    TEST_ASSERT_EQUAL_INT16(4, mono[2]);
}

static void test_process_channels_null_buffer_invalid(void)
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, i2s_process_channels(NULL, 1));
}

static void test_config_helpers_update_channel_format(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, i2s_config_mono(22050, I2S_DATA_BIT_WIDTH_16BIT));
    TEST_ASSERT_TRUE(i2s_is_driver_installed());
    TEST_ASSERT_EQUAL(I2S_SLOT_MODE_MONO, i2s_get_channel_format());

    TEST_ASSERT_EQUAL(ESP_OK, i2s_config_stereo(48000, I2S_DATA_BIT_WIDTH_24BIT));
    TEST_ASSERT_EQUAL(I2S_SLOT_MODE_STEREO, i2s_get_channel_format());
}

static void test_configure_standard_mode_sets_defaults(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, i2s_configure_standard_mode());
    TEST_ASSERT_TRUE(i2s_is_driver_installed());
    TEST_ASSERT_EQUAL(I2S_SLOT_MODE_STEREO, i2s_get_channel_format());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_driver_init_sets_state);
    RUN_TEST(test_reinit_changes_config);
    RUN_TEST(test_deinit_clears_install_flag);
    RUN_TEST(test_write_samples_requires_install);
    RUN_TEST(test_write_samples_sets_bytes_when_installed);
    RUN_TEST(test_write_samples_rejects_null_buffer);
    RUN_TEST(test_write_mono_samples_in_stereo_mode_ok);
    RUN_TEST(test_write_mono_samples_rejects_null);
    RUN_TEST(test_write_stereo_samples_in_mono_mode_ok);
    RUN_TEST(test_mono_to_stereo_duplicates_samples);
    RUN_TEST(test_stereo_to_mono_averages_channels);
    RUN_TEST(test_process_channels_null_buffer_invalid);
    RUN_TEST(test_config_helpers_update_channel_format);
    RUN_TEST(test_configure_standard_mode_sets_defaults);
    return UNITY_END();
}
