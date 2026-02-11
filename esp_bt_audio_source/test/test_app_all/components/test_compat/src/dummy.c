/* Dummy source to ensure the test_compat component is registered by CMake
 * The file intentionally contains no logic. It prevents the component from
 * being skipped due to having zero source files, which in turn ensures the
 * component's include directory is added to the compiler search path.
 */

#include "driver/i2s_std.h"

static const int __test_compat_dummy = 0;
