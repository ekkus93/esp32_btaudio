// Host-side helper that implements the allocation portion of audio_processor
// used by host-unit tests. This is intentionally small and only implements
// the persistent work buffer allocation/fallback logic (uses heap_caps_*).

#ifndef AUDIO_ALLOC_HOST_H
#define AUDIO_ALLOC_HOST_H

#include <stddef.h>

/* Allocate persistent work buffers. Parameter is the initial per-buffer
 * target in bytes (one of three buffers). Returns 0 on success, non-zero
 * on failure. On success the function sets an internal runtime work size
 * accessible via audio_processor_get_work_buffer_bytes(). */
int audio_processor_alloc_work_buffers(size_t per_buffer_try);
void audio_processor_free_work_buffers(void);
size_t audio_processor_get_work_buffer_bytes(void);

#endif // AUDIO_ALLOC_HOST_H
