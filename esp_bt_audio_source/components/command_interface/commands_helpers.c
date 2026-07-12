#include "commands_priv.h"
#include "util_safe.h"
#include "platform_timing.h"

#if !defined(ESP_PLATFORM)
int g_mock_log_level = ESP_LOG_INFO;
#if defined(__GNUC__)
extern const char *cmd_version_host_override(void) __attribute__((weak));
#else
extern const char *cmd_version_host_override(void);
#pragma weak cmd_version_host_override
#endif
#endif

int cmd_vsnprintf_safe(char *dst, size_t dst_size, const char *fmt, va_list args)
{
    return util_safe_vsnprintf(dst, dst_size, fmt, args);
}

int cmd_snprintf_safe(char *dst, size_t dst_size, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int written = util_safe_vsnprintf(dst, dst_size, fmt, args);
    va_end(args);
    return written;
}

void *cmd_memcpy_safe(void *dst, const void *src, size_t len)
{
    util_safe_memcpy(dst, len, src, len);
    return dst;
}

void *cmd_memmove_safe(void *dst, const void *src, size_t len)
{
    util_safe_memmove(dst, len, src, len);
    return dst;
}

void *cmd_memset_safe(void *dst, int value, size_t len)
{
    util_safe_memset(dst, len, value, len);
    return dst;
}

void cmd_safe_copy(char *dst, size_t dst_size, const char *src)
{
    util_safe_copy_str(dst, dst_size, src);
}

void cmd_safe_append(char *dst, size_t dst_size, const char *suffix)
{
    if (dst == NULL || dst_size == 0)
    {
        return;
    }

    size_t used = strnlen(dst, dst_size);
    if (used >= dst_size)
    {
        dst[dst_size - 1] = '\0';
        return;
    }
    if (suffix == NULL || suffix[0] == '\0')
    {
        return;
    }

    size_t remaining = dst_size - used;
    size_t i = 0;
    while (i + 1 < remaining && suffix[i] != '\0')
    {
        dst[used + i] = suffix[i];
        ++i;
    }
    dst[used + i] = '\0';
}

void copy_truncated_identifier(const char *src, char *dst, size_t dst_size)
{
    if (dst_size == 0)
    {
        return;
    }
    if (src == NULL || src[0] == '\0')
    {
        dst[0] = '\0';
        return;
    }

    size_t max_copy = dst_size - 1;
    size_t i = 0;
    while (i < max_copy && src[i] != '\0')
    {
        dst[i] = src[i];
        ++i;
    }

    if (src[i] != '\0' && dst_size > 4)
    {
        size_t ellipsis_start = dst_size - 4;
        if (ellipsis_start > i)
        {
            ellipsis_start = i;
        }
        dst[ellipsis_start] = '.';
        dst[ellipsis_start + 1] = '.';
        dst[ellipsis_start + 2] = '.';
        dst[ellipsis_start + 3] = '\0';
    }
    else
    {
        dst[i] = '\0';
    }
}

bool cmd_parse_log_level(const char *level_str, int *out_level)
{
    if (level_str == NULL || out_level == NULL)
    {
        return false;
    }

    char *end = NULL;
    long numeric = strtol(level_str, &end, 10);
    if (end != level_str && *end == '\0')
    {
        if (numeric >= ESP_LOG_NONE && numeric <= ESP_LOG_VERBOSE)
        {
            *out_level = (int)numeric;
            return true;
        }
        return false;
    }

    if (strcasecmp(level_str, "NONE") == 0) {
        *out_level = ESP_LOG_NONE;
    } else if (strcasecmp(level_str, "ERROR") == 0 || strcasecmp(level_str, "ERR") == 0) {
        *out_level = ESP_LOG_ERROR;
    } else if (strcasecmp(level_str, "WARN") == 0 || strcasecmp(level_str, "WARNING") == 0) {
        *out_level = ESP_LOG_WARN;
    } else if (strcasecmp(level_str, "INFO") == 0) {
        *out_level = ESP_LOG_INFO;
    } else if (strcasecmp(level_str, "DEBUG") == 0 || strcasecmp(level_str, "DBG") == 0) {
        *out_level = ESP_LOG_DEBUG;
    } else if (strcasecmp(level_str, "VERBOSE") == 0 || strcasecmp(level_str, "TRACE") == 0) {
        *out_level = ESP_LOG_VERBOSE;
    } else {
        return false;
    }

    return true;
}

uint64_t cmd_get_timestamp_ms(void)
{
    return platform_get_time_ms();
}

void cmd_append_metadata(char *buf, size_t buf_len, const char *key, const char *value)
{
    if (!buf || buf_len == 0 || !key || !value || value[0] == '\0')
    {
        return;
    }

    size_t used = strlen(buf);
    if (used >= buf_len - 1)
    {
        return;
    }

    int written = snprintf(buf + used, buf_len - used, "%s%s=%s",
                           (used > 0) ? "," : "", key, value);
    if (written < 0)
    {
        buf[0] = '\0';
    }
}

