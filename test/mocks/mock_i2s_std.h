/* Minimal compatibility header for tests that include "mock_i2s_std.h" from
 * repository-level test paths. Mirrors the host_test version and keeps the
 * compile-time expectations simple.
 */
#ifndef MOCK_I2S_STD_H
#define MOCK_I2S_STD_H

#include "mock_i2s.h"

static inline void i2s_new_channel_ExpectAnyArgsAndReturn(esp_err_t ret) { (void)ret; }
static inline void i2s_channel_init_std_mode_ExpectAnyArgsAndReturn(esp_err_t ret) { (void)ret; }

#endif /* MOCK_I2S_STD_H */
