/* ==========================================
 *  Unity Project - A Test Framework for C
 *  Copyright (c) 2007 Mike Karlesky, Mark VanderVoord, Greg Williams
 *  [Released under MIT License. Please refer to license.txt for details]
 * ========================================== */

#ifndef UNITY_MEMORY_H
#define UNITY_MEMORY_H

#include <stdlib.h>

/* Define memory functions that just use standard malloc/free */
#define unity_malloc malloc
#define unity_free free
#define unity_calloc calloc
#define unity_realloc realloc

void* unity_malloc(size_t size);
void unity_free(void* ptr);
void* unity_calloc(size_t num, size_t size);
void* unity_realloc(void* oldMem, size_t size);

/* Memory tracking functions */
void UnityMalloc_StartTest(void);
void UnityMalloc_EndTest(void);
void UnityMalloc_MakeMallocFailAfterCount(int count);

#endif /* UNITY_MEMORY_H */
