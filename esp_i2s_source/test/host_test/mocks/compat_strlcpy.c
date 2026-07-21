/* Host-build-only compatibility shim.
 *
 * Production code calls strlcpy(), which is provided natively by ESP-IDF's
 * newlib on-device, but was only added to glibc in 2.38 (many host build
 * machines, including CI, still ship older glibc). Provide it here so host
 * tests can link on any glibc version without changing device behavior.
 * strcasestr() does not need a shim — glibc has always had it, just gated
 * behind _GNU_SOURCE, which this CMakeLists.txt defines globally.
 */
#include <string.h>

#if defined(__GLIBC__) && !(defined(__GLIBC_PREREQ) && __GLIBC_PREREQ(2, 38))
size_t strlcpy(char *dst, const char *src, size_t dstsize)
{
    size_t srclen = strlen(src);
    if (dstsize != 0) {
        size_t copy_len = (srclen < dstsize - 1) ? srclen : dstsize - 1;
        memcpy(dst, src, copy_len);
        dst[copy_len] = '\0';
    }
    return srclen;
}
#endif
