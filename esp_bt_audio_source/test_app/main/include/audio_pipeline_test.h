#ifndef AUDIO_PIPELINE_TEST_H
#define AUDIO_PIPELINE_TEST_H

/**
 * @file audio_pipeline_test.h
 * @brief Audio Pipeline Test Function Declarations
 */

#ifdef __cplusplus
extern "C" {
#endif

// Test functions
void test_audio_pipeline_initialization(void);
void test_audio_buffer_management(void);

// Main test runner
void run_audio_pipeline_tests(void);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_PIPELINE_TEST_H */
