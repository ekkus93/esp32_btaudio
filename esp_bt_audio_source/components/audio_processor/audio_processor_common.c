#include "audio_processor_internal.h"

size_t audio_processor_queue_free_bytes(void)
{
    size_t used = audio_descriptor_used();
    if (used >= AUDIO_CHUNK_POOL_BLOCKS) {
        return 0;
    }
    size_t free_blocks = (size_t)AUDIO_CHUNK_POOL_BLOCKS - used;
    return free_blocks * (size_t)AUDIO_CHUNK_BLOCK_BYTES;
}

void audio_processor_flush_priority_queues(const char* reason)
{
    (void)reason;
    audio_chunk_t chunk = {0};
    while (audio_chunk_dequeue(&chunk, 0)) {
        audio_chunk_release_block(chunk.data);
    }
    s_audio_rb_residual_len = 0;
    s_audio_rb_residual_pos = 0;
}