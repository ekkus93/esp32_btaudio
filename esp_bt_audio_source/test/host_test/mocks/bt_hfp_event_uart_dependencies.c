#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int cmd_vsnprintf_safe(char *dst, size_t dst_size,
                       const char *fmt, va_list args)
{
    return vsnprintf(dst, dst_size, fmt, args);
}

int cmd_snprintf_safe(char *dst, size_t dst_size, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int result = vsnprintf(dst, dst_size, fmt, args);
    va_end(args);
    return result;
}

void *cmd_memcpy_safe(void *dst, const void *src, size_t len)
{
    return memcpy(dst, src, len);
}

void *cmd_memmove_safe(void *dst, const void *src, size_t len)
{
    return memmove(dst, src, len);
}

void *cmd_memset_safe(void *dst, int value, size_t len)
{
    return memset(dst, value, len);
}

uint64_t cmd_get_timestamp_ms(void)
{
    return 0U;
}
