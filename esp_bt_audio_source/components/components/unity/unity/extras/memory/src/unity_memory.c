/* ==========================================
 *  Unity Project - A Test Framework for C
 *  Copyright (c) 2007 Mike Karlesky, Mark VanderVoord, Greg Williams
 *  [Released under MIT License. Please refer to license.txt for details]
 * ========================================== */

#include "unity_memory.h"

/* These are just wrappers around standard library functions */

void* unity_malloc(size_t size)
{
    return malloc(size);
}

void unity_free(void* ptr)
{
    free(ptr);
}

void* unity_calloc(size_t num, size_t size)
{
    return calloc(num, size);
}

void* unity_realloc(void* oldMem, size_t size)
{
    return realloc(oldMem, size);
}

/* Simple implementation of memory tracking functions */
void UnityMalloc_StartTest(void)
{
    /* Do nothing in this simple implementation */
}

void UnityMalloc_EndTest(void)
{
    /* Do nothing in this simple implementation */
}

void UnityMalloc_MakeMallocFailAfterCount(int count)
{
    /* Do nothing in this simple implementation */
}
