/* Host-build-only compatibility shim, force-included (-include) into
 * translation units that call strlcpy(). ESP-IDF's newlib declares it
 * natively on-device; glibc only added it in 2.38, so older host glibc
 * (this build's toolchain included) needs an explicit declaration. The
 * matching definition lives in mocks/compat_strlcpy.c.
 */
#pragma once
#include <string.h>

#if defined(__GLIBC__) && !(defined(__GLIBC_PREREQ) && __GLIBC_PREREQ(2, 38))
size_t strlcpy(char *dst, const char *src, size_t dstsize);
#endif
