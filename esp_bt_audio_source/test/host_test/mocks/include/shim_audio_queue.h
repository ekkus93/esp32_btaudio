#pragma once

#include "audio_queue.h"

#include <stddef.h>
#include <stdint.h>

/* Helpers exposed by the host shim_audio_queue.c implementation. */
size_t audio_queue_last_len(void);
audio_source_tag_t audio_queue_last_tag(void);
uint16_t audio_queue_last_tag_id(void);
const uint8_t *audio_queue_last_data(void);
void audio_queue_set_fail_enqueue(bool fail);
void audio_queue_fail_after_enqueue(size_t allowed);
void audio_queue_disable_fail_after(void);
void audio_queue_set_tag_counter(uint16_t start);
/* Test helpers for dequeue observation */
size_t audio_queue_get_dequeue_count(void);
void audio_queue_reset_dequeue_count(void);
/* Snapshot helper (host shim) */
esp_err_t audio_descriptor_snapshot(audio_chunk_t *out, size_t max_items, size_t *captured_out);
