#include "unity.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "bt_hfp_audio.h"

#define CONCURRENCY_PEER "AA:BB:CC:DD:EE:FF"
#define CONCURRENCY_GENERATION 77U
#define CONCURRENCY_SYNC_HANDLE 0x4567U
#define OVERLAP_THREADS 4U

void mock_hfp_audio_i2s_reset(void);
void mock_hfp_audio_i2s_set_expected_generation(uint32_t generation);
unsigned mock_hfp_audio_i2s_calls(void);

static pthread_mutex_t s_pause_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_pause_cv = PTHREAD_COND_INITIALIZER;
static bool s_callback_entered;
static bool s_callback_released;
static unsigned s_now_calls;

static esp_err_t concurrency_register_callback(void)
{
    return ESP_OK;
}

static int64_t concurrency_now_us(void)
{
    TEST_ASSERT_EQUAL_INT(0, pthread_mutex_lock(&s_pause_lock));
    s_now_calls++;
    if (s_now_calls == 1U) {
        s_callback_entered = true;
        TEST_ASSERT_EQUAL_INT(0, pthread_cond_broadcast(&s_pause_cv));
        while (!s_callback_released) {
            TEST_ASSERT_EQUAL_INT(0, pthread_cond_wait(&s_pause_cv,
                                                       &s_pause_lock));
        }
        TEST_ASSERT_EQUAL_INT(0, pthread_mutex_unlock(&s_pause_lock));
        return 1000;
    }
    TEST_ASSERT_EQUAL_INT(0, pthread_mutex_unlock(&s_pause_lock));
    return 1100;
}

static void *callback_thread(void *unused)
{
    (void)unused;
    const int16_t pcm[] = {1, -2, 3, -4};
    bt_hfp_audio_test_handle_incoming(
        CONCURRENCY_SYNC_HANDLE,
        (const uint8_t *)pcm,
        sizeof(pcm),
        sizeof(pcm),
        false);
    return NULL;
}

void test_cleanup_refuses_while_full_callback_lifetime_is_active(void)
{
    bt_hfp_audio_test_reset();
    mock_hfp_audio_i2s_reset();
    mock_hfp_audio_i2s_set_expected_generation(CONCURRENCY_GENERATION);

    s_callback_entered = false;
    s_callback_released = false;
    s_now_calls = 0U;

    const bt_hfp_audio_platform_ops_t ops = {
        .register_callback = concurrency_register_callback,
        .now_us = concurrency_now_us,
    };
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_audio_test_set_platform_ops(&ops));
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_audio_register_callback());

    bt_duplex_snapshot_t duplex;
    memset(&duplex, 0, sizeof(duplex));
    duplex.peer_valid = true;
    memcpy(duplex.peer_mac, CONCURRENCY_PEER, sizeof(CONCURRENCY_PEER));
    duplex.session_generation = CONCURRENCY_GENERATION;
    duplex.hfp_profile_state = BT_HFP_PROFILE_SLC_CONNECTED;
    duplex.hfp_audio_state = BT_HFP_AUDIO_CONNECTED_CVSD;
    duplex.codec = BT_HFP_CODEC_CVSD;
    duplex.i2s_state = BT_HFP_I2S_RUNNING;
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_audio_apply_duplex_state(
        &duplex, CONCURRENCY_PEER, CONCURRENCY_SYNC_HANDLE, 120U));

    pthread_t thread;
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&thread, NULL,
                                            callback_thread, NULL));

    TEST_ASSERT_EQUAL_INT(0, pthread_mutex_lock(&s_pause_lock));
    while (!s_callback_entered) {
        TEST_ASSERT_EQUAL_INT(0, pthread_cond_wait(&s_pause_cv, &s_pause_lock));
    }
    TEST_ASSERT_EQUAL_INT(0, pthread_mutex_unlock(&s_pause_lock));

    pthread_t overlaps[OVERLAP_THREADS];
    for (unsigned index = 0U; index < OVERLAP_THREADS; ++index) {
        TEST_ASSERT_EQUAL_INT(
            0, pthread_create(&overlaps[index], NULL, callback_thread, NULL));
    }
    for (unsigned index = 0U; index < OVERLAP_THREADS; ++index) {
        TEST_ASSERT_EQUAL_INT(0, pthread_join(overlaps[index], NULL));
    }

    bt_hfp_audio_snapshot_t snapshot;
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_audio_get_snapshot(&snapshot));
    TEST_ASSERT_EQUAL_UINT32(1, snapshot.active_callbacks);
    TEST_ASSERT_EQUAL_UINT32(OVERLAP_THREADS,
                               snapshot.callback_overlap_rejections);
    TEST_ASSERT_EQUAL_UINT64(0, snapshot.incoming_callbacks);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      bt_hfp_audio_cleanup_after_stack_shutdown());

    TEST_ASSERT_EQUAL_INT(0, pthread_mutex_lock(&s_pause_lock));
    s_callback_released = true;
    TEST_ASSERT_EQUAL_INT(0, pthread_cond_broadcast(&s_pause_cv));
    TEST_ASSERT_EQUAL_INT(0, pthread_mutex_unlock(&s_pause_lock));
    TEST_ASSERT_EQUAL_INT(0, pthread_join(thread, NULL));

    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_audio_get_snapshot(&snapshot));
    TEST_ASSERT_EQUAL_UINT32(0, snapshot.active_callbacks);
    TEST_ASSERT_EQUAL_UINT32(OVERLAP_THREADS,
                               snapshot.callback_overlap_rejections);
    TEST_ASSERT_EQUAL_UINT64(1, snapshot.incoming_callbacks);
    TEST_ASSERT_EQUAL_UINT64(0, snapshot.accepted_frames);
    TEST_ASSERT_EQUAL_UINT64(1, snapshot.inactive_frames);
    TEST_ASSERT_EQUAL_UINT64(1, snapshot.dropped_frames);
    TEST_ASSERT_EQUAL_UINT(0, mock_hfp_audio_i2s_calls());
    TEST_ASSERT_EQUAL(ESP_OK, bt_hfp_audio_cleanup_after_stack_shutdown());
}
