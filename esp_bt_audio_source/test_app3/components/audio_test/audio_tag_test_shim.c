#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "freertos/task.h"

/* Minimal, test-only implementation of the tag buffer helpers so the
 * audio tag Unity tests can run inside test_app3 without pulling in the
 * entire production audio_processor component. */

typedef struct {
    uint8_t tag;
    uint16_t counter;
} audio_source_tag_item_t;

static RingbufHandle_t s_tag_buffer = NULL;
static uint16_t s_tag_counter = 0;

bool audio_source_tag_test_init_buffer(size_t buf_size)
{
    if (s_tag_buffer != NULL) {
        return true;
    }

    if (buf_size == 0) {
        buf_size = 512; /* fallback size */
    }

    s_tag_buffer = xRingbufferCreate(buf_size, RINGBUF_TYPE_BYTEBUF);
    s_tag_counter = 0;
    return (s_tag_buffer != NULL);
}

void audio_source_tag_test_deinit_buffer(void)
{
    if (s_tag_buffer != NULL) {
        vRingbufferDelete(s_tag_buffer);
        s_tag_buffer = NULL;
    }
}

uint16_t audio_source_tag_test_get_counter(void)
{
    return s_tag_counter;
}

void audio_source_tag_test_set_counter(uint16_t v)
{
    s_tag_counter = v;
}

bool audio_source_tag_test_push(int tag)
{
    if (s_tag_buffer == NULL) {
        return false;
    }

    audio_source_tag_item_t item = {
        .tag = (uint8_t)(tag & 0xFF),
        .counter = s_tag_counter++,
    };

    return xRingbufferSend(s_tag_buffer, &item, sizeof(item), pdMS_TO_TICKS(10)) == pdTRUE;
}

bool audio_source_tag_test_take(int *tag_out, TickType_t wait_ticks)
{
    if (s_tag_buffer == NULL || tag_out == NULL) {
        return false;
    }

    size_t item_size = 0;
    audio_source_tag_item_t *item = (audio_source_tag_item_t *)xRingbufferReceiveUpTo(
        s_tag_buffer, &item_size, wait_ticks, sizeof(audio_source_tag_item_t));
    if (item == NULL || item_size < sizeof(audio_source_tag_item_t)) {
        return false;
    }

    *tag_out = (int)item->tag;
    vRingbufferReturnItem(s_tag_buffer, item);
    return true;
}

void audio_source_tag_test_drop_one(void)
{
    if (s_tag_buffer == NULL) {
        return;
    }

    size_t item_size = 0;
    void *item = xRingbufferReceiveUpTo(s_tag_buffer, &item_size, 0, sizeof(audio_source_tag_item_t));
    if (item != NULL) {
        vRingbufferReturnItem(s_tag_buffer, item);
    }
}

void audio_source_tag_test_reset_buffer(void)
{
    if (s_tag_buffer == NULL) {
        return;
    }

    size_t item_size = 0;
    void *item = NULL;
    const size_t max_take = sizeof(audio_source_tag_item_t);

    while ((item = xRingbufferReceiveUpTo(s_tag_buffer, &item_size, 0, max_take)) != NULL && item_size > 0U) {
        vRingbufferReturnItem(s_tag_buffer, item);
    }
}
