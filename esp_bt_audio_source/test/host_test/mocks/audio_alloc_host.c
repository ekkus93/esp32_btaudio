// Minimal host implementation of the audio_processor allocation logic.
#include "audio_alloc_host.h"
#include "esp_heap_caps.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

static void* s_work_block = NULL;
static size_t s_runtime_work_bytes = 0;

int audio_processor_alloc_work_buffers(size_t per_buffer_try)
{
    if (s_work_block) return 0; /* already allocated */

    size_t try_work_bytes = per_buffer_try;
    if (try_work_bytes == 0) try_work_bytes = 4 * 1024;

    const size_t min_work_bytes = 1 * 1024; /* relaxed for host tests */

    while (try_work_bytes >= min_work_bytes) {
        size_t combined = try_work_bytes * 3U;

        void* block = heap_caps_malloc(combined, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (block == NULL) {
            /* fallback to default allocator */
            block = heap_caps_malloc(combined, MALLOC_CAP_DEFAULT | MALLOC_CAP_8BIT);
        }

        if (block != NULL) {
            s_work_block = block;
            s_runtime_work_bytes = try_work_bytes;
            return 0;
        }

        /* reduce and retry */
        try_work_bytes = try_work_bytes / 2U;
        try_work_bytes = (try_work_bytes + 3U) & ~((size_t)3U);
    }

    s_runtime_work_bytes = 0;
    return -1;
}

void audio_processor_free_work_buffers(void)
{
    if (s_work_block) {
        heap_caps_free(s_work_block);
        s_work_block = NULL;
    }
    s_runtime_work_bytes = 0;
}

size_t audio_processor_get_work_buffer_bytes(void)
{
    return s_runtime_work_bytes;
}
