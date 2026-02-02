#include "mem_util.h"

size_t safe_memcpy(void *dst, size_t dst_size, const void *src, size_t len)
{
	if (dst == NULL || src == NULL || dst_size == 0 || len == 0) {
		return 0;
	}
	size_t to_copy = (len > dst_size) ? dst_size : len;
	uint8_t *d = (uint8_t *)dst;
	const uint8_t *s = (const uint8_t *)src;
	for (size_t i = 0; i < to_copy; ++i) {
		d[i] = s[i];
	}
	return to_copy;
}

void safe_memset(void *dst, size_t dst_size, int value, size_t len)
{
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
