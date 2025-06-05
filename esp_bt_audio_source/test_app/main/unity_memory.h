#ifndef UNITY_MEMORY_H
#define UNITY_MEMORY_H

#include <stdlib.h>

// Define minimal Unity memory functions
void* unity_malloc(size_t size);
void* unity_calloc(size_t num, size_t size);
void* unity_realloc(void* ptr, size_t size);
void unity_free(void* ptr);

// Simple implementation - just use standard malloc/free
#define unity_malloc malloc
#define unity_calloc calloc
#define unity_realloc realloc
#define unity_free free

#endif // UNITY_MEMORY_H
