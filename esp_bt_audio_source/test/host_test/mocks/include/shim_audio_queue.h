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
void audio_queue_set_tag_counter(uint16_t start);
