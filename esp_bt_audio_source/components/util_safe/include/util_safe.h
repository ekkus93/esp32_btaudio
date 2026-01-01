#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bounded memory helpers */
void util_safe_memset(void *dst, int value, size_t len);
void util_safe_memcpy(void *dst, size_t dst_size, const void *src, size_t len);
void util_safe_memmove(void *dst, size_t dst_size, const void *src, size_t len);
void util_safe_copy_str(char *dst, size_t dst_size, const char *src);

/* Bounded printf wrappers: return characters that would have been written, like snprintf. */
int util_safe_vsnprintf(char *dst, size_t dst_size, const char *fmt, va_list args);
int util_safe_snprintf(char *dst, size_t dst_size, const char *fmt, ...);

/* MAC helpers (6-byte Bluetooth addresses) */
bool util_parse_mac(const char *str, uint8_t out[6]);
void util_format_mac(const uint8_t addr[6], char *out, size_t out_len);

#ifdef __cplusplus
}
#endif
