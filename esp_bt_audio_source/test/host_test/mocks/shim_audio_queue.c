#include "audio_queue.h"

#include <string.h>

static bool s_pool_inited = false;
static uint8_t s_last_data[AUDIO_CHUNK_BLOCK_BYTES];
static size_t s_last_len = 0;
static audio_source_tag_t s_last_tag = AUDIO_SOURCE_TAG_INVALID;
static uint16_t s_last_tag_id = 0;
static uint16_t s_tag_counter = 0;
static bool s_fail_enqueue = false;

bool audio_chunk_pool_init(void)
{
    s_pool_inited = true;
    return true;
}

void audio_chunk_pool_deinit(void)
{
    s_pool_inited = false;
}

uint8_t *audio_chunk_alloc_block(TickType_t wait_ticks)
{
    (void)wait_ticks;
    return NULL;
}

void audio_chunk_release_block(uint8_t *ptr)
{
    (void)ptr;
}

static bool enqueue_common(const uint8_t *data, size_t len, audio_source_tag_t tag, uint16_t tag_id)
{
    if (s_fail_enqueue || !s_pool_inited || data == NULL || len == 0) {
        return false;
    }
    if (len > sizeof(s_last_data)) {
        len = sizeof(s_last_data);
    }

    memcpy(s_last_data, data, len);
    s_last_len = len;
    s_last_tag = tag;
    s_last_tag_id = tag_id;

    /* Keep the next auto-generated tag_id monotonic. */
    if ((uint16_t)(tag_id + 1) > s_tag_counter) {
        s_tag_counter = (uint16_t)(tag_id + 1);
    }

    return true;
}

bool audio_chunk_enqueue_bytes(const uint8_t *data, size_t len, audio_source_tag_t tag)
{
    return enqueue_common(data, len, tag, s_tag_counter++);
}

bool audio_chunk_enqueue_bytes_with_id(const uint8_t *data, size_t len, audio_source_tag_t tag, uint16_t tag_id)
{
    return enqueue_common(data, len, tag, tag_id);
}

bool audio_chunk_dequeue(audio_chunk_t *out_chunk, TickType_t wait_ticks)
{
    (void)wait_ticks;
    if (!out_chunk || s_last_len == 0) {
        return false;
    }

    out_chunk->data = s_last_data;
    out_chunk->len = s_last_len;
    out_chunk->tag = s_last_tag;
    out_chunk->tag_id = s_last_tag_id;

    s_last_len = 0;
    return true;
}

void audio_chunk_clear(void)
{
    s_last_len = 0;
    s_last_tag = AUDIO_SOURCE_TAG_INVALID;
    s_last_tag_id = 0;
    s_fail_enqueue = false;
}

size_t audio_descriptor_used(void)
{
    return (s_last_len > 0) ? 1 : 0;
}

/* Test helpers */
size_t audio_queue_last_len(void) { return s_last_len; }
audio_source_tag_t audio_queue_last_tag(void) { return s_last_tag; }
uint16_t audio_queue_last_tag_id(void) { return s_last_tag_id; }
const uint8_t *audio_queue_last_data(void) { return s_last_data; }
void audio_queue_set_fail_enqueue(bool fail) { s_fail_enqueue = fail; }
void audio_queue_set_tag_counter(uint16_t start) { s_tag_counter = start; }
