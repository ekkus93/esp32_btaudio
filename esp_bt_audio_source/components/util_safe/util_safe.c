#include "util_safe.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

void util_safe_memset(void *dst, size_t dst_size, int value, size_t len) {
    if (dst == NULL || dst_size == 0 || len == 0) {
        return;
    }
    size_t to_set = (len > dst_size) ? dst_size : len;
    uint8_t *d = (uint8_t *)dst;
    uint8_t byte_value = (uint8_t)value;
    for (size_t i = 0; i < to_set; ++i) {
        d[i] = byte_value;
    }
}

size_t util_safe_memcpy(void *dst, size_t dst_capacity, const void *src, size_t src_len) {
    if (dst == NULL || src == NULL || dst_capacity == 0 || src_len == 0) {
        return 0;
    }
    size_t to_copy = src_len;
    if (to_copy > dst_capacity) {
        to_copy = dst_capacity;
    }
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < to_copy; ++i) {
        d[i] = s[i];
    }
    return to_copy;
}

size_t util_safe_memmove(void *dst, size_t dst_capacity, const void *src, size_t src_len) {
    if (dst == NULL || src == NULL || dst_capacity == 0 || src_len == 0) {
        return 0;
    }
    size_t to_copy = src_len;
    if (to_copy > dst_capacity) {
        to_copy = dst_capacity;
    }

    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    if (d == s) {
        return to_copy;
    }

    if (d > s && d < (s + to_copy)) {
        for (size_t i = to_copy; i > 0; --i) {
            d[i - 1] = s[i - 1];
        }
    } else {
        for (size_t i = 0; i < to_copy; ++i) {
            d[i] = s[i];
        }
    }
    return to_copy;
}

void util_safe_copy_str(char *dst, size_t dst_size, const char *src) {
    if (dst == NULL || dst_size == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    size_t i = 0;
    while (i + 1 < dst_size && src[i] != '\0') {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

int util_safe_vsnprintf(char *dst, size_t dst_size, const char *fmt, va_list args) {
    if (dst == NULL || dst_size == 0 || fmt == NULL) {
        return 0;
    }
    int written = vsnprintf(dst, dst_size, fmt, args); // NOLINT(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    if (written < 0 || (size_t)written >= dst_size) {
        dst[dst_size - 1] = '\0';
    }
    return written;
}

int util_safe_snprintf(char *dst, size_t dst_size, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int written = util_safe_vsnprintf(dst, dst_size, fmt, args);
    va_end(args);
    return written;
}

bool util_parse_mac(const char *str, uint8_t out[6]) {
    if (str == NULL || out == NULL) {
        return false;
    }

    const char *p = str;
    for (int i = 0; i < 6; ++i) {
        if (!isxdigit((unsigned char)p[0]) || !isxdigit((unsigned char)p[1])) {
            return false;
        }
        char byte_str[3] = { p[0], p[1], '\0' };
        char *end = NULL;
        unsigned long parsed_value = strtoul(byte_str, &end, 16);
        if (end != byte_str + 2 || parsed_value > UCHAR_MAX) {
            return false;
        }

        out[i] = (uint8_t)parsed_value;
        p += 2;

        if (i < 5) {
            if (*p == ':') {
                ++p;
            } else if (*p == '\0') {
                return false; /* premature end */
            }
        } else if (*p != '\0') {
            return false; /* trailing characters */
        }
    }

    return true;
}

static char to_hex(uint8_t v) {
    return (char)((v < 10U) ? ('0' + v) : ('A' + (v - 10U)));
}

void util_format_mac(const uint8_t addr[6], char *out, size_t out_len) {
    if (addr == NULL || out == NULL || out_len == 0) {
        return;
    }
    size_t pos = 0;
    for (int i = 0; i < 6 && (pos + 2) < out_len; ++i) {
        uint8_t byte = addr[i];
        if ((pos + 3) > out_len) {
            break;
        }
        out[pos++] = to_hex((uint8_t)((byte >> 4) & 0x0F));
        out[pos++] = to_hex((uint8_t)(byte & 0x0F));
        if (i < 5 && pos < out_len) {
            out[pos++] = ':';
        }
    }
    if (pos >= out_len) {
        out[out_len - 1] = '\0';
    } else {
        out[pos] = '\0';
    }
}
