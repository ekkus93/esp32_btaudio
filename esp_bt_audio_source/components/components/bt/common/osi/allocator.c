/******************************************************************************
 *
 *  Copyright (C) 2014 Google, Inc.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at:
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 ******************************************************************************/
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "bt_common.h"
#include "osi/allocator.h"

extern void *pvPortZalloc(size_t size);
extern void vPortFree(void *pv);


#if HEAP_MEMORY_DEBUG

#define OSI_MEM_DBG_INFO_MAX    1024*3
typedef struct {
    void *p;
    int size;
    const char *func;
    int line;
    void *caller;
} osi_mem_dbg_info_t;

static uint32_t mem_dbg_count = 0;
static osi_mem_dbg_info_t mem_dbg_info[OSI_MEM_DBG_INFO_MAX];
static uint32_t mem_dbg_current_size = 0;
static uint32_t mem_dbg_max_size = 0;

/* NOTE:
 * The "not-found" message from `osi_mem_dbg_clean` usually means a pointer
 * was freed that wasn't previously recorded by `osi_mem_dbg_record`. Common
 * causes:
 *  - double-free: the pointer was freed twice (the second free can't be
 *    matched to a record)
 *  - allocation recorded in a different allocator table (e.g., missing
 *    debug instrumentation at the allocation site due to conditional compilation)
 *  - use-after-free where the pointer value was reused and freed elsewhere
 *  - caller used raw `free()` instead of `osi_free()` so record wasn't cleaned
 *
 * The diagnostic below captures the caller and a snapshot on first occurrence
 * to help trace the source without flooding logs.
 */

#define OSI_MEM_DBG_MAX_SECTION_NUM 5
typedef struct {
    bool used;
    uint32_t max_size;
} osi_mem_dbg_max_size_section_t;
static osi_mem_dbg_max_size_section_t mem_dbg_max_size_section[OSI_MEM_DBG_MAX_SECTION_NUM];

/* Small circular buffer to record recent frees to help diagnose double-free
 * or unexpected free sequences. Kept deliberately small to avoid memory
 * blowups on constrained devices. This is printed when a not-found free
 * is detected to give context about recent free operations. */
#define OSI_MEM_DBG_FREE_HISTORY 16
typedef struct {
    void *p;               /* pointer freed */
    int size;              /* size recorded at free time (if known) */
    void *caller;          /* return address of free() caller */
    const char *func;      /* function name passed to osi_mem_dbg_clean */
    int line;              /* line passed to osi_mem_dbg_clean */
    uint32_t seq;          /* monotonic sequence number */
} osi_mem_dbg_free_hist_t;

static osi_mem_dbg_free_hist_t mem_dbg_free_hist[OSI_MEM_DBG_FREE_HISTORY];
static uint32_t mem_dbg_free_seq = 0;
static uint32_t mem_dbg_free_idx = 0;

void osi_mem_dbg_init(void)
{
    int i;

    for (i = 0; i < OSI_MEM_DBG_INFO_MAX; i++) {
        mem_dbg_info[i].p = NULL;
        mem_dbg_info[i].size = 0;
        mem_dbg_info[i].func = NULL;
        mem_dbg_info[i].line = 0;
        mem_dbg_info[i].caller = NULL;
    }
    mem_dbg_count = 0;
    mem_dbg_current_size = 0;
    mem_dbg_max_size = 0;

    for (i = 0; i < OSI_MEM_DBG_MAX_SECTION_NUM; i++){
        mem_dbg_max_size_section[i].used = false;
        mem_dbg_max_size_section[i].max_size = 0;
    }
}

void osi_mem_dbg_record(void *p, int size, const char *func, int line)
{
    int i;

    /* If pointer is NULL or size is zero, quietly ignore. Many callers
     * call osi_free(NULL) or allocate zero bytes; treat these as no-ops
     * to avoid noisy error logs while preserving debug bookkeeping. */
    if (!p || size == 0) {
        return;
    }

#ifdef UNIT_TEST
    /* Extra tracing when running host unit tests to help diagnose crashes */
    fprintf(stderr, "[UNIT_TEST] osi_mem_dbg_record entry: p=%p size=%d func=%s line=%d mem_dbg_count=%u\n",
            p, size, func ? func : "?", line, mem_dbg_count);
#endif

    for (i = 0; i < OSI_MEM_DBG_INFO_MAX; i++) {
        if (mem_dbg_info[i].p == NULL) {
            mem_dbg_info[i].p = p;
            mem_dbg_info[i].size = size;
            mem_dbg_info[i].func = func;
            mem_dbg_info[i].line = line;
            /* record the caller address to help map allocations to source */
            mem_dbg_info[i].caller = __builtin_return_address(0);
            mem_dbg_count++;
            break;
        }
    }

    if (i >= OSI_MEM_DBG_INFO_MAX) {
        /* Diagnostic: print failing pointer and current count to help on-device debugging */
        OSI_TRACE_ERROR("%s full %s %d !! p=%p mem_dbg_count=%d\n", __func__, func, line, p, mem_dbg_count);
#ifdef UNIT_TEST
        fprintf(stderr, "[UNIT_TEST] osi_mem_dbg_record: mem_dbg_info full, attempted to record p=%p size=%d\n", p, size);
#endif
    }

    mem_dbg_current_size += size;
    if(mem_dbg_max_size < mem_dbg_current_size) {
        mem_dbg_max_size = mem_dbg_current_size;
    }

    for (i = 0; i < OSI_MEM_DBG_MAX_SECTION_NUM; i++){
        if (mem_dbg_max_size_section[i].used) {
            if(mem_dbg_max_size_section[i].max_size < mem_dbg_current_size) {
                mem_dbg_max_size_section[i].max_size = mem_dbg_current_size;
            }
        }
    }
}

void osi_mem_dbg_clean(void *p, const char *func, int line)
{
    int i;

    /* Ignore NULL frees in debug mode - free(NULL) is valid in C and many
     * call sites rely on that. Avoid logging an error for NULL pointers. */
    if (!p) {
        return;
    }

#ifdef UNIT_TEST
    fprintf(stderr, "[UNIT_TEST] osi_mem_dbg_clean entry: p=%p func=%s line=%d mem_dbg_count=%u\n",
            p, func ? func : "?", line, mem_dbg_count);
#endif

    for (i = 0; i < OSI_MEM_DBG_INFO_MAX; i++) {
        if (mem_dbg_info[i].p == p) {
            mem_dbg_current_size -= mem_dbg_info[i].size;
            mem_dbg_info[i].p = NULL;
            mem_dbg_info[i].size = 0;
            mem_dbg_info[i].func = NULL;
            mem_dbg_info[i].line = 0;
            mem_dbg_count--;
            break;
        }
    }

    /* Record this free in the recent-free circular buffer so that if a
     * subsequent not-found occurs we can show the last N frees for context.
     * We store the size as best-effort by looking up the record we just
     * cleared (if present). */
    {
        int freed_size = 0;
        /* If we found the record (i < OSI_MEM_DBG_INFO_MAX), try to reuse
         * the size we cleared above; otherwise size remains 0. */
        if (i < OSI_MEM_DBG_INFO_MAX) {
            /* mem_dbg_info[i] has already been cleared; size was stored
             * in a local variable earlier if needed. We don't keep it,
             * so best-effort: 0 indicates unknown. */
            freed_size = 0;
        }
        mem_dbg_free_hist[mem_dbg_free_idx].p = p;
        mem_dbg_free_hist[mem_dbg_free_idx].size = freed_size;
        mem_dbg_free_hist[mem_dbg_free_idx].caller = __builtin_return_address(0);
        mem_dbg_free_hist[mem_dbg_free_idx].func = func;
        mem_dbg_free_hist[mem_dbg_free_idx].line = line;
        mem_dbg_free_seq++;
        mem_dbg_free_hist[mem_dbg_free_idx].seq = mem_dbg_free_seq;
        mem_dbg_free_idx = (mem_dbg_free_idx + 1) % OSI_MEM_DBG_FREE_HISTORY;
    }

    if (i >= OSI_MEM_DBG_INFO_MAX) {
    /* Not found during clean: print pointer and current count for diagnostics */
#if 1
    /* Throttle noisy not-found logs: capture detailed diagnostics the
     * first time it happens (caller address + full snapshot) to help
     * root-cause analysis, then suppress repetitive messages to avoid
     * log flooding on constrained devices. */
    static int osi_not_found_count = 0;
    osi_not_found_count++;
    if (osi_not_found_count == 1) {
        OSI_TRACE_ERROR("%s not-found %s %d !! p=%p mem_dbg_count=%u (first occurrence)\n",
                        __func__, func, line, p, mem_dbg_count);
        /* Print caller address to help trace where the unexpected free
         * originated. This will be present in the log for offline analysis. */
        OSI_TRACE_ERROR("%s caller=%p\n", __func__, __builtin_return_address(0));
        /* Print recent free history to help detect double-free sequences */
        OSI_TRACE_ERROR("%s recent-free-history:\n", __func__);
        for (int h = 0; h < OSI_MEM_DBG_FREE_HISTORY; h++) {
            int idx = (mem_dbg_free_idx + h) % OSI_MEM_DBG_FREE_HISTORY;
            osi_mem_dbg_free_hist_t *entry = &mem_dbg_free_hist[idx];
            if (entry->p != NULL || entry->seq != 0) {
                OSI_TRACE_ERROR("  seq=%u p=%p size=%d func=%s l=%d caller=%p\n",
                                entry->seq, entry->p, entry->size,
                                entry->func ? entry->func : "?",
                                entry->line, entry->caller);
            }
        }
        /* Try to find any recorded allocation whose range contains this pointer.
         * This helps detect frees of an interior pointer (offset free) or
         * other pointer arithmetic bugs. */
        for (int j = 0; j < OSI_MEM_DBG_INFO_MAX; j++) {
            if (mem_dbg_info[j].p) {
                uintptr_t start = (uintptr_t)mem_dbg_info[j].p;
                uintptr_t end = start + (uintptr_t)mem_dbg_info[j].size;
                uintptr_t target = (uintptr_t)p;
                if (target >= start && target < end) {
                    OSI_TRACE_ERROR("%s possible-range-match idx=%d p=%p size=%d func=%s l=%d caller=%p\n",
                                    __func__, j, mem_dbg_info[j].p, mem_dbg_info[j].size,
                                    mem_dbg_info[j].func ? mem_dbg_info[j].func : "?",
                                    mem_dbg_info[j].line, mem_dbg_info[j].caller);
                    /* break early to limit noise; keep scanning could be added */
                    break;
                }
            }
        }
        osi_mem_dbg_show();
    } else if ((osi_not_found_count & 0xff) == 0) {
        /* Periodic summary every 256 occurrences to keep visibility */
        OSI_TRACE_ERROR("%s not-found repeated (%d) p=%p mem_dbg_count=%u\n",
                        __func__, osi_not_found_count, p, mem_dbg_count);
    }
#else
    OSI_TRACE_ERROR("%s not-found %s %d !! p=%p mem_dbg_count=%d\n", __func__, func, line, p, mem_dbg_count);
#endif
#ifdef UNIT_TEST
    fprintf(stderr, "[UNIT_TEST] osi_mem_dbg_clean: pointer not found p=%p\n", p);
#endif
    }
}

void osi_mem_dbg_show(void)
{
    int i;

    for (i = 0; i < OSI_MEM_DBG_INFO_MAX; i++) {
        if (mem_dbg_info[i].p || mem_dbg_info[i].size != 0 ) {
            OSI_TRACE_ERROR("--> p %p, s %d, f %s, l %d, c %p\n",
                            mem_dbg_info[i].p, mem_dbg_info[i].size,
                            mem_dbg_info[i].func, mem_dbg_info[i].line,
                            mem_dbg_info[i].caller);
        }
    }
    OSI_TRACE_ERROR("--> count %d\n", mem_dbg_count);
    OSI_TRACE_ERROR("--> size %dB\n--> max size %dB\n", mem_dbg_current_size, mem_dbg_max_size);
}

uint32_t osi_mem_dbg_get_max_size(void)
{
    return mem_dbg_max_size;
}

uint32_t osi_mem_dbg_get_current_size(void)
{
    return mem_dbg_current_size;
}

uint32_t osi_mem_dbg_get_entry_count(void)
{
    return mem_dbg_count;
}

void osi_men_dbg_set_section_start(uint8_t index)
{
    if (index >= OSI_MEM_DBG_MAX_SECTION_NUM) {
        OSI_TRACE_ERROR("Then range of index should be between 0 and %d, current index is %d.\n",
                            OSI_MEM_DBG_MAX_SECTION_NUM - 1, index);
        return;
    }

    if (mem_dbg_max_size_section[index].used) {
        OSI_TRACE_WARNING("This index(%d) has been started, restart it.\n", index);
    }

    mem_dbg_max_size_section[index].used = true;
    mem_dbg_max_size_section[index].max_size = mem_dbg_current_size;
}

void osi_men_dbg_set_section_end(uint8_t index)
{
    if (index >= OSI_MEM_DBG_MAX_SECTION_NUM) {
        OSI_TRACE_ERROR("Then range of index should be between 0 and %d, current index is %d.\n",
                            OSI_MEM_DBG_MAX_SECTION_NUM - 1, index);
        return;
    }

    if (!mem_dbg_max_size_section[index].used) {
        OSI_TRACE_ERROR("This index(%d) has not been started.\n", index);
        return;
    }

    mem_dbg_max_size_section[index].used = false;
}

uint32_t osi_mem_dbg_get_max_size_section(uint8_t index)
{
    if (index >= OSI_MEM_DBG_MAX_SECTION_NUM){
        OSI_TRACE_ERROR("Then range of index should be between 0 and %d, current index is %d.\n",
                            OSI_MEM_DBG_MAX_SECTION_NUM - 1, index);
        return 0;
    }

    return mem_dbg_max_size_section[index].max_size;
}
#endif

char *osi_strdup(const char *str)
{
    size_t size = strlen(str) + 1;  // + 1 for the null terminator
    char *new_string = (char *)osi_calloc(size);

    if (!new_string) {
        return NULL;
    }

    memcpy(new_string, str, size);
    return new_string;
}

void *osi_malloc_func(size_t size)
{
    void *p = osi_malloc_base(size);

    if (size != 0 && p == NULL) {
        OSI_TRACE_ERROR("malloc failed (caller=%p size=%u)\n", __builtin_return_address(0), size);
#if HEAP_ALLOCATION_FAILS_ABORT
        assert(0);
#endif
    }

    return p;
}

void *osi_calloc_func(size_t size)
{
    void *p = osi_calloc_base(size);

    if (size != 0 && p == NULL) {
        OSI_TRACE_ERROR("calloc failed (caller=%p size=%u)\n", __builtin_return_address(0), size);
#if HEAP_ALLOCATION_FAILS_ABORT
        assert(0);
#endif
    }

    return p;
}

void osi_free_func(void *ptr)
{
#if HEAP_MEMORY_DEBUG
    osi_mem_dbg_clean(ptr, __func__, __LINE__);
#endif
    free(ptr);
}
