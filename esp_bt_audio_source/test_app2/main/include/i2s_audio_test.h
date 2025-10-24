#ifndef I2S_AUDIO_TEST_H
#define I2S_AUDIO_TEST_H

/**
 * @file i2s_audio_test.h
 * @brief I2S Audio Test Function Declarations
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Test setup and teardown functions
void i2s_audio_test_setUp(void);
void i2s_audio_test_tearDown(void);

// Test functions
void test_i2s_driver_init(void);
void test_i2s_standard_mode(void);

// Other related test functions
void pcm_format_test_setUp(void);
void pcm_format_test_tearDown(void);
void i2s_channel_test_setUp(void);
void i2s_channel_test_tearDown(void);

// Main test runner function - add this missing declaration
void run_i2s_audio_tests(void);

#ifdef __cplusplus
}
#endif

#endif /* I2S_AUDIO_TEST_H */
