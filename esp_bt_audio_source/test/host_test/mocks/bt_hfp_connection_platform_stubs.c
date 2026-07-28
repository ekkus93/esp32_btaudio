#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "util_safe.h"
#include "platform_timing.h"

void util_safe_copy_str(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0U) return;
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    size_t length = strlen(src);
    if (length >= dst_size) length = dst_size - 1U;
    memcpy(dst, src, length);
    dst[length] = '\0';
}

static int hex_value(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    value = (char)tolower((unsigned char)value);
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    return -1;
}

bool util_parse_mac(const char *str, uint8_t out[6])
{
    if (str == NULL || out == NULL || strlen(str) != 17U) return false;
    for (size_t index = 0; index < 6U; ++index) {
        size_t offset = index * 3U;
        int high = hex_value(str[offset]);
        int low = hex_value(str[offset + 1U]);
        if (high < 0 || low < 0) return false;
        if (index < 5U && str[offset + 2U] != ':') return false;
        out[index] = (uint8_t)((high << 4) | low);
    }
    return str[17] == '\0';
}

uint64_t platform_get_time_ms(void)
{
    static uint64_t now_ms = 1000U;
    return now_ms++;
}
