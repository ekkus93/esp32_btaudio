// Minimal compatibility shim for CMock-style expectation helpers used in tests.
// This header provides no-op macros so the on-device Unity tests can compile
// when CMock-generated helpers are not present. These macros intentionally do
// nothing at compile time; they only satisfy the test code's calls such as
// i2s_new_channel_ExpectAnyArgsAndReturn(ESP_OK).

#ifndef TEST_APP_MAIN_INCLUDE_MOCK_I2S_STD_H
#define TEST_APP_MAIN_INCLUDE_MOCK_I2S_STD_H

#include "esp_err.h"

// If the real CMock helpers exist, prefer those. Otherwise provide harmless
// no-op macros so the test can still compile and run on-device. The semantic
// behavior of these macros is intentionally empty; they only satisfy the
// compile-time symbols that some tests call.

#ifndef i2s_new_channel_ExpectAnyArgsAndReturn
#define i2s_new_channel_ExpectAnyArgsAndReturn(_ret) ((void)0)
#endif

#ifndef i2s_channel_init_std_mode_ExpectAnyArgsAndReturn
#define i2s_channel_init_std_mode_ExpectAnyArgsAndReturn(_ret) ((void)0)
#endif

#endif // TEST_APP_MAIN_INCLUDE_MOCK_I2S_STD_H
// Forwarder for mock_i2s_std used by tests. Includes the repository mock implementation under mocks/include.
#pragma once
#include "mock_i2s_std.h"
