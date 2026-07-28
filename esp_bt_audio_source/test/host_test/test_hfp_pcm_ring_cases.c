#include "unity.h"

#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <string.h>

#include "hfp_pcm_ring.h"

#define GENERATION 7U

static hfp_pcm_ring_t ring;
static uint8_t storage[128];

void setUp(void)
{
    memset(&ring, 0, sizeof(ring));
    memset(storage, 0, sizeof(storage));
}

void tearDown(void)
{
}

void test_ring_init_rejects_invalid_arguments(void)
{
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      hfp_pcm_ring_init(NULL, storage, sizeof(storage),
                                        GENERATION));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      hfp_pcm_ring_init(&ring, NULL, sizeof(storage),
                                        GENERATION));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      hfp_pcm_ring_init(&ring, storage, 0U, GENERATION));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      hfp_pcm_ring_init(&ring, storage, sizeof(storage), 0U));
}

void test_ring_simple_write_and_read(void)
{
    static const uint8_t input[] = {1, 2, 3, 4, 5, 6};
    uint8_t output[sizeof(input)] = {0};
    hfp_pcm_ring_snapshot_t snapshot;

    TEST_ASSERT_EQUAL(ESP_OK,
                      hfp_pcm_ring_init(&ring, storage, sizeof(storage),
                                        GENERATION));
    TEST_ASSERT_TRUE(hfp_pcm_ring_write_frame(&ring, input, sizeof(input),
                                              GENERATION));
    TEST_ASSERT_EQUAL(sizeof(input),
                      hfp_pcm_ring_read(&ring, output, sizeof(output),
                                        GENERATION));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(input, output, sizeof(input));
    TEST_ASSERT_EQUAL(ESP_OK, hfp_pcm_ring_get_snapshot(&ring, &snapshot));
    TEST_ASSERT_EQUAL_UINT64(sizeof(input), snapshot.total_written_bytes);
    TEST_ASSERT_EQUAL_UINT64(sizeof(input), snapshot.total_read_bytes);
    TEST_ASSERT_EQUAL_UINT32(0, snapshot.current_used);
    TEST_ASSERT_EQUAL_UINT32(sizeof(input), snapshot.peak_used);
}

void test_ring_exact_full_and_frame_rejection_is_atomic(void)
{
    uint8_t exact[sizeof(storage)];
    uint8_t rejected[] = {0xAA, 0xBB};
    uint8_t output[sizeof(storage)];
    for (size_t i = 0; i < sizeof(exact); ++i) exact[i] = (uint8_t)i;

    TEST_ASSERT_EQUAL(ESP_OK,
                      hfp_pcm_ring_init(&ring, storage, sizeof(storage),
                                        GENERATION));
    TEST_ASSERT_TRUE(hfp_pcm_ring_write_frame(&ring, exact, sizeof(exact),
                                              GENERATION));
    TEST_ASSERT_FALSE(hfp_pcm_ring_write_frame(&ring, rejected,
                                               sizeof(rejected), GENERATION));
    TEST_ASSERT_EQUAL(sizeof(output),
                      hfp_pcm_ring_read(&ring, output, sizeof(output),
                                        GENERATION));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exact, output, sizeof(exact));

    hfp_pcm_ring_snapshot_t snapshot;
    TEST_ASSERT_EQUAL(ESP_OK, hfp_pcm_ring_get_snapshot(&ring, &snapshot));
    TEST_ASSERT_EQUAL_UINT64(1, snapshot.overflow_frames);
    TEST_ASSERT_EQUAL_UINT64(sizeof(rejected), snapshot.overflow_bytes);
    TEST_ASSERT_EQUAL_UINT64(sizeof(exact), snapshot.total_written_bytes);
}

void test_ring_wraparound_preserves_bytes(void)
{
    uint8_t first[96];
    uint8_t second[64];
    uint8_t discard[80];
    uint8_t remaining[80];
    for (size_t i = 0; i < sizeof(first); ++i) first[i] = (uint8_t)i;
    for (size_t i = 0; i < sizeof(second); ++i) second[i] = (uint8_t)(160U + i);

    TEST_ASSERT_EQUAL(ESP_OK,
                      hfp_pcm_ring_init(&ring, storage, sizeof(storage),
                                        GENERATION));
    TEST_ASSERT_TRUE(hfp_pcm_ring_write_frame(&ring, first, sizeof(first),
                                              GENERATION));
    TEST_ASSERT_EQUAL(sizeof(discard),
                      hfp_pcm_ring_read(&ring, discard, sizeof(discard),
                                        GENERATION));
    TEST_ASSERT_TRUE(hfp_pcm_ring_write_frame(&ring, second, sizeof(second),
                                              GENERATION));
    TEST_ASSERT_EQUAL(sizeof(remaining),
                      hfp_pcm_ring_read(&ring, remaining, sizeof(remaining),
                                        GENERATION));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(first + 80, remaining, 16);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(second, remaining + 16, sizeof(second));
}

void test_ring_underflow_and_overflow_counters_are_exact(void)
{
    uint8_t input[100] = {0};
    uint8_t output[120] = {0};

    TEST_ASSERT_EQUAL(ESP_OK,
                      hfp_pcm_ring_init(&ring, storage, sizeof(storage),
                                        GENERATION));
    TEST_ASSERT_TRUE(hfp_pcm_ring_write_frame(&ring, input, sizeof(input),
                                              GENERATION));
    TEST_ASSERT_FALSE(hfp_pcm_ring_write_frame(&ring, input, 40,
                                               GENERATION));
    TEST_ASSERT_EQUAL(sizeof(input),
                      hfp_pcm_ring_read(&ring, output, sizeof(output),
                                        GENERATION));

    hfp_pcm_ring_snapshot_t snapshot;
    TEST_ASSERT_EQUAL(ESP_OK, hfp_pcm_ring_get_snapshot(&ring, &snapshot));
    TEST_ASSERT_EQUAL_UINT64(1, snapshot.overflow_frames);
    TEST_ASSERT_EQUAL_UINT64(40, snapshot.overflow_bytes);
    TEST_ASSERT_EQUAL_UINT64(1, snapshot.underflow_events);
    TEST_ASSERT_EQUAL_UINT64(20, snapshot.underflow_bytes);
}

void test_ring_generation_and_reset_contract(void)
{
    uint8_t input[8] = {0};
    uint8_t output[8] = {0};
    hfp_pcm_ring_snapshot_t snapshot;

    TEST_ASSERT_EQUAL(ESP_OK,
                      hfp_pcm_ring_init(&ring, storage, sizeof(storage),
                                        GENERATION));
    TEST_ASSERT_FALSE(hfp_pcm_ring_write_frame(&ring, input, sizeof(input),
                                               GENERATION - 1U));
    TEST_ASSERT_EQUAL(0,
                      hfp_pcm_ring_read(&ring, output, sizeof(output),
                                        GENERATION - 1U));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      hfp_pcm_ring_reset(&ring, GENERATION + 1U, false));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      hfp_pcm_ring_reset(&ring, GENERATION, true));
    TEST_ASSERT_EQUAL(ESP_OK,
                      hfp_pcm_ring_reset(&ring, GENERATION + 1U, true));
    TEST_ASSERT_EQUAL(ESP_OK, hfp_pcm_ring_get_snapshot(&ring, &snapshot));
    TEST_ASSERT_EQUAL_UINT32(GENERATION + 1U, snapshot.generation);
    TEST_ASSERT_EQUAL_UINT32(0, snapshot.current_used);
    TEST_ASSERT_EQUAL_UINT64(2, snapshot.stale_generation_operations);
}

void test_ring_invalid_operations_are_visible(void)
{
    uint8_t output[8];
    TEST_ASSERT_EQUAL(ESP_OK,
                      hfp_pcm_ring_init(&ring, storage, sizeof(storage),
                                        GENERATION));
    TEST_ASSERT_FALSE(hfp_pcm_ring_write_frame(&ring, NULL, 4, GENERATION));
    TEST_ASSERT_FALSE(hfp_pcm_ring_write_frame(&ring, output, 0, GENERATION));
    TEST_ASSERT_EQUAL(0,
                      hfp_pcm_ring_read(&ring, NULL, sizeof(output),
                                        GENERATION));
    TEST_ASSERT_EQUAL(0,
                      hfp_pcm_ring_read(&ring, output, 0, GENERATION));
    hfp_pcm_ring_snapshot_t snapshot;
    TEST_ASSERT_EQUAL(ESP_OK, hfp_pcm_ring_get_snapshot(&ring, &snapshot));
    TEST_ASSERT_EQUAL_UINT64(4, snapshot.invalid_operations);
}

typedef struct {
    hfp_pcm_ring_t *ring;
    uint32_t generation;
    uint32_t frames;
    atomic_bool failed;
} stress_context_t;

static void *producer_thread(void *arg)
{
    stress_context_t *ctx = (stress_context_t *)arg;
    for (uint32_t value = 0; value < ctx->frames; ++value) {
        while (!hfp_pcm_ring_write_frame(ctx->ring, &value, sizeof(value),
                                         ctx->generation)) {
            sched_yield();
        }
    }
    return NULL;
}

static void *consumer_thread(void *arg)
{
    stress_context_t *ctx = (stress_context_t *)arg;
    for (uint32_t expected = 0; expected < ctx->frames;) {
        uint32_t value = UINT32_MAX;
        size_t read = hfp_pcm_ring_read(ctx->ring, &value, sizeof(value),
                                        ctx->generation);
        if (read == 0U) {
            sched_yield();
            continue;
        }
        if (read != sizeof(value) || value != expected) {
            atomic_store_explicit(&ctx->failed, true, memory_order_release);
            return NULL;
        }
        expected++;
    }
    return NULL;
}

void test_ring_spsc_stress_preserves_order(void)
{
    uint8_t stress_storage[1024];
    stress_context_t ctx = {
        .ring = &ring,
        .generation = GENERATION,
        .frames = 50000,
    };
    atomic_init(&ctx.failed, false);
    pthread_t producer;
    pthread_t consumer;

    TEST_ASSERT_EQUAL(ESP_OK,
                      hfp_pcm_ring_init(&ring, stress_storage,
                                        sizeof(stress_storage), GENERATION));
    TEST_ASSERT_EQUAL(0, pthread_create(&producer, NULL, producer_thread, &ctx));
    TEST_ASSERT_EQUAL(0, pthread_create(&consumer, NULL, consumer_thread, &ctx));
    TEST_ASSERT_EQUAL(0, pthread_join(producer, NULL));
    TEST_ASSERT_EQUAL(0, pthread_join(consumer, NULL));
    TEST_ASSERT_FALSE(atomic_load_explicit(&ctx.failed, memory_order_acquire));

    hfp_pcm_ring_snapshot_t snapshot;
    TEST_ASSERT_EQUAL(ESP_OK, hfp_pcm_ring_get_snapshot(&ring, &snapshot));
    TEST_ASSERT_EQUAL_UINT64((uint64_t)ctx.frames * sizeof(uint32_t),
                             snapshot.total_written_bytes);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)ctx.frames * sizeof(uint32_t),
                             snapshot.total_read_bytes);
    TEST_ASSERT_EQUAL_UINT32(0, snapshot.current_used);
}
