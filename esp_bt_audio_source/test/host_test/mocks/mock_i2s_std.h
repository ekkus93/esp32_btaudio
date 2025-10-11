/* Minimal host-side compatibility header for tests that include "mock_i2s_std.h".
 * This file forwards to the existing mock_i2s.h and provides simple
 * static-inline no-op expectation helper functions used in tests.
 *
 * Placing this in the host_test/mocks directory matches the CMake include
 * layout used for host tests.
 */
#ifndef MOCK_I2S_STD_H
#define MOCK_I2S_STD_H

#include "mock_i2s.h"

#ifdef __cplusplus
extern "C" {
#endif

/* CMock-style expectation helpers used by some tests. Provide no-op
 * static inline implementations so the tests compile and link for host
 * builds that don't run CMock.
 */
static inline void i2s_new_channel_ExpectAnyArgsAndReturn(esp_err_t ret) { (void)ret; }
static inline void i2s_channel_init_std_mode_ExpectAnyArgsAndReturn(esp_err_t ret) { (void)ret; }

#ifdef __cplusplus
}
#endif

#endif /* MOCK_I2S_STD_H */
