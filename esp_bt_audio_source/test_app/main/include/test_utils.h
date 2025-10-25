#ifndef TEST_UTILS_H
#define TEST_UTILS_H

#include <stdbool.h>
#include <stddef.h>

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

// Serial command harness helpers used by pairing command tests
void test_utils_reset_state(void);
bool test_send_serial_cmd(const char *cmd);
bool test_capture_event(char *out_buf, size_t out_len);

#ifdef __cplusplus
}
#endif

#endif /* TEST_UTILS_H */
