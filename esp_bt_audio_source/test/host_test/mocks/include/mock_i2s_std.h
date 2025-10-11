/* Compatibility header for mock_i2s_std placed under mocks/include */
#ifndef MOCK_I2S_STD_H
#define MOCK_I2S_STD_H

#include "mock_i2s.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline void i2s_new_channel_ExpectAnyArgsAndReturn(int ret) { (void)ret; }
static inline void i2s_channel_init_std_mode_ExpectAnyArgsAndReturn(int ret) { (void)ret; }

#ifdef __cplusplus
}
#endif

#endif /* MOCK_I2S_STD_H */
