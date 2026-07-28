#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "util_safe.h"

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
    unsigned char byte = (unsigned char)value;
    if (byte >= '0' && byte <= '9') return byte - '0';
    byte = (unsigned char)tolower(byte);
    if (byte >= 'a' && byte <= 'f') return byte - 'a' + 10;
    return -1;
}

bool util_parse_mac(const char *str, uint8_t out[6])
{
    if (str == NULL || out == NULL || strlen(str) != 17U) return false;
    for (size_t index = 0U; index < 6U; ++index) {
        size_t offset = index * 3U;
        int high = hex_value(str[offset]);
        int low = hex_value(str[offset + 1U]);
        if (high < 0 || low < 0) return false;
        if (index < 5U && str[offset + 2U] != ':') return false;
        out[index] = (uint8_t)((high << 4) | low);
    }
    return true;
}
