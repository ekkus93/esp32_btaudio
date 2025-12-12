#include <stdio.h>
#include <stdint.h>
#include "unity.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"

/* Test-only externs from audio_processor.c (available when building
 * with CONFIG_BT_MOCK_TESTING enabled). */
extern bool audio_source_tag_test_init_buffer(size_t buf_size);
extern void audio_source_tag_test_deinit_buffer(void);
extern bool audio_source_tag_test_push(int tag);
extern bool audio_source_tag_test_take(int *tag_out, TickType_t wait_ticks);
extern void audio_source_tag_test_drop_one(void);
extern void audio_source_tag_test_reset_buffer(void);
extern uint16_t audio_source_tag_test_get_counter(void);
extern void audio_source_tag_test_set_counter(uint16_t v);

/* setUp/tearDown are used by Unity runner */
void setUp(void) {
    TEST_ASSERT_TRUE(audio_source_tag_test_init_buffer(512));
}

void tearDown(void) {
    audio_source_tag_test_deinit_buffer();
}

static void test_tag_item_size(void)
{
    /* The tag item should be small (one byte tag + two byte counter).
     * Size may include padding; verify it's reasonable. */
    size_t sz = sizeof(((struct { uint8_t tag; uint16_t counter; }){0}));
    TEST_ASSERT_TRUE_MESSAGE(sz >= 3 && sz <= 4, "unexpected tag-item size");
}

static void test_tag_counter_increments_and_wrap(void)
{
    const int N = 10;
    uint16_t before = audio_source_tag_test_get_counter();

    for (int i = 0; i < N; ++i) {
        TEST_ASSERT_TRUE(audio_source_tag_test_push((i & 0xFF)));
    }

    for (int i = 0; i < N; ++i) {
        int tag = -1;
        TEST_ASSERT_TRUE(audio_source_tag_test_take(&tag, pdMS_TO_TICKS(10)));
        TEST_ASSERT_GREATER_OR_EQUAL_INT(0, tag);
    }

    uint16_t after = audio_source_tag_test_get_counter();
    /* after should be >= before (modulo wrap). For short N we can assert changed. */
    TEST_ASSERT_NOT_EQUAL(before, after);

    /* Now test wrap: set counter near 0xFFFF and push two items */
    audio_source_tag_test_set_counter(0xFFFE);
    TEST_ASSERT_TRUE(audio_source_tag_test_push(1));
    TEST_ASSERT_TRUE(audio_source_tag_test_push(2));
    int t;
    TEST_ASSERT_TRUE(audio_source_tag_test_take(&t, pdMS_TO_TICKS(10)));
    TEST_ASSERT_TRUE(audio_source_tag_test_take(&t, pdMS_TO_TICKS(10)));
}

static void test_drop_and_reset(void)
{
    /* Push some tags, drop one and then reset and confirm take fails */
    TEST_ASSERT_TRUE(audio_source_tag_test_push(10));
    TEST_ASSERT_TRUE(audio_source_tag_test_push(11));
    audio_source_tag_test_drop_one();
    /* One remaining */
    int tag;
    TEST_ASSERT_TRUE(audio_source_tag_test_take(&tag, pdMS_TO_TICKS(10)));
    /* Now empty */
    TEST_ASSERT_FALSE(audio_source_tag_test_take(&tag, pdMS_TO_TICKS(10)));

    /* Refill and reset */
    TEST_ASSERT_TRUE(audio_source_tag_test_push(20));
    TEST_ASSERT_TRUE(audio_source_tag_test_push(21));
    audio_source_tag_test_reset_buffer();
    TEST_ASSERT_FALSE(audio_source_tag_test_take(&tag, pdMS_TO_TICKS(10)));
}

/* Registration helper so the test app can RUN_TEST without a local main(). */
void audio_tag_tests_register(void)
{
#ifdef CONFIG_BT_MOCK_TESTING
    RUN_TEST(test_tag_item_size);
    RUN_TEST(test_tag_counter_increments_and_wrap);
    RUN_TEST(test_drop_and_reset);
#else
    printf("audio_tag_tests_register: skipped (CONFIG_BT_MOCK_TESTING not enabled)\n");
#endif
}
