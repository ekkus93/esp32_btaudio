// Memory utility helpers shared across main audio pipeline modules.
#ifndef MEM_UTIL_H_
#define MEM_UTIL_H_

#include <stddef.h>
#include <stdint.h>

size_t safe_memcpy(void *dst, size_t dst_size, const void *src, size_t len);
void safe_memset(void *dst, size_t dst_size, int value, size_t len);

#endif // MEM_UTIL_H_
