#ifndef TEST_UTILS_H
#define TEST_UTILS_H

#ifdef __cplusplus
extern "C" {
#endif

// I2S audio test setup/teardown
void i2s_audio_test_setUp(void);
void i2s_audio_test_tearDown(void);

// PCM format test setup/teardown
void pcm_format_test_setUp(void);
void pcm_format_test_tearDown(void);

// I2S channel test setup/teardown
void i2s_channel_test_setUp(void);
void i2s_channel_test_tearDown(void);

#ifdef __cplusplus
}
#endif

#endif /* TEST_UTILS_H */
