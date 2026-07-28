#include "util_safe.h"

#include <ctype.h>
#include <string.h>

void util_safe_copy_str(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0U) return;
    if (src == NULL) src = "";
    size_t length = strlen(src);
    if (length >= dst_size) length = dst_size - 1U;
    memcpy(dst, src, length);
    dst[length] = '\0';
}

static int hex_value(unsigned char value)
{
    if (value >= '0' && value <= '9') return (int)(value - '0');
    value = (unsigned char)toupper(value);
    if (value >= 'A' && value <= 'F') return 10 + (int)(value - 'A');
    return -1;
}

bool util_parse_mac(const char *str, uint8_t out[6])
{
    if (str == NULL || out == NULL || strlen(str) != 17U) return false;
    for (size_t index = 0U; index < 6U; ++index) {
        size_t offset = index * 3U;
        int high = hex_value((unsigned char)str[offset]);
        int low = hex_value((unsigned char)str[offset + 1U]);
        if (high < 0 || low < 0) return false;
        if (index < 5U && str[offset + 2U] != ':') return false;
        out[index] = (uint8_t)((unsigned)high << 4U | (unsigned)low);
    }
    return true;
}
