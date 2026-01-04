/* Host unit test: ensure audio_processor_beep_tone flushes priority queues */
#include "unity.h"
#include "audio_queue.h"
#include "audio_processor.h"
#include "esp_err.h"

/* Prototypes for shim helpers (declared in mocks/include/shim_audio_queue.h) */
extern void audio_queue_reset_dequeue_count(void);
extern size_t audio_queue_get_dequeue_count(void);

void setUp(void)
{
    audio_chunk_pool_init();
    audio_chunk_clear();
    audio_queue_reset_dequeue_count();
}

void tearDown(void) {}

void test_flush_called_before_beep(void)
{
    const uint8_t payload[4] = {0x11, 0x22, 0x33, 0x44};
    /* Enqueue an existing priority chunk so flush has something to drop. */
    TEST_ASSERT_TRUE(audio_chunk_enqueue_bytes(payload, sizeof(payload), AUDIO_SOURCE_TAG_WAV));
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)audio_descriptor_used());

    /* Reset count and call the public beep API; it should flush priority queues. */
    audio_queue_reset_dequeue_count();
    esp_err_t rc = audio_processor_beep_tone(50, 1000.0);
    TEST_ASSERT_EQUAL(ESP_OK, rc);

    size_t dq = audio_queue_get_dequeue_count();
    TEST_ASSERT_TRUE_MESSAGE(dq >= 1, "audio_processor_beep_tone did not dequeue priority items");
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_flush_called_before_beep);
    return UNITY_END();
}
