#include "unity.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "hfp_i2s_output.h"

#define GENERATION 41U
#define PEER "aa:bb:cc:dd:ee:ff"

typedef struct {
    int alloc_calls;
    int fail_alloc_call;
    int free_calls;
    int channel_new_calls;
    int channel_init_calls;
    int channel_enable_calls;
    int channel_disable_calls;
    int channel_delete_calls;
    int task_create_calls;
    int task_release_calls;
    int task_wait_calls;
    int write_calls;
    esp_err_t channel_new_result;
    esp_err_t channel_init_result;
    esp_err_t channel_enable_result;
    esp_err_t channel_disable_result;
    esp_err_t channel_delete_result;
    esp_err_t task_create_result;
    esp_err_t task_release_result;
    esp_err_t task_wait_result;
    esp_err_t write_result;
    bool short_write;
    uint8_t last_write[1024];
    size_t last_write_bytes;
} fake_state_t;

static fake_state_t fake;
static hfp_i2s_output_config_t config;
static hfp_i2s_pin_owners_t owners;

static void *fake_alloc(size_t bytes)
{
    fake.alloc_calls++;
    if (fake.fail_alloc_call == fake.alloc_calls) return NULL;
    return calloc(1U, bytes);
}

static void fake_free(void *ptr)
{
    if (ptr != NULL) fake.free_calls++;
    free(ptr);
}

static esp_err_t fake_channel_new(const hfp_i2s_output_config_t *unused,
                                  void **channel_out)
{
    (void)unused;
    fake.channel_new_calls++;
    if (fake.channel_new_result == ESP_OK) *channel_out = (void *)0x1111;
    return fake.channel_new_result;
}

static esp_err_t fake_channel_init(void *channel,
                                   const hfp_i2s_output_config_t *unused)
{
    (void)channel;
    (void)unused;
    fake.channel_init_calls++;
    return fake.channel_init_result;
}

static esp_err_t fake_channel_enable(void *channel)
{
    (void)channel;
    fake.channel_enable_calls++;
    return fake.channel_enable_result;
}

static esp_err_t fake_channel_disable(void *channel)
{
    (void)channel;
    fake.channel_disable_calls++;
    return fake.channel_disable_result;
}

static esp_err_t fake_channel_delete(void *channel)
{
    (void)channel;
    fake.channel_delete_calls++;
    return fake.channel_delete_result;
}

static esp_err_t fake_task_create(void **task_out)
{
    fake.task_create_calls++;
    if (fake.task_create_result == ESP_OK) *task_out = (void *)0x2222;
    return fake.task_create_result;
}

static esp_err_t fake_task_release(void *task)
{
    (void)task;
    fake.task_release_calls++;
    return fake.task_release_result;
}

static esp_err_t fake_task_wait(void *task, uint32_t timeout_ms)
{
    (void)task;
    (void)timeout_ms;
    fake.task_wait_calls++;
    return fake.task_wait_result;
}

static esp_err_t fake_write(void *channel, const void *data, size_t bytes,
                            size_t *bytes_written, uint32_t timeout_ms)
{
    (void)channel;
    (void)timeout_ms;
    fake.write_calls++;
    fake.last_write_bytes = bytes < sizeof(fake.last_write)
        ? bytes : sizeof(fake.last_write);
    memcpy(fake.last_write, data, fake.last_write_bytes);
    if (bytes_written != NULL) {
        if (fake.write_result != ESP_OK) {
            *bytes_written = 0U;
        } else {
            *bytes_written = fake.short_write && bytes > 0U ? bytes - 1U : bytes;
        }
    }
    return fake.write_result;
}

static hfp_i2s_output_platform_ops_t make_ops(void)
{
    return (hfp_i2s_output_platform_ops_t) {
        .alloc = fake_alloc,
        .free = fake_free,
        .channel_new = fake_channel_new,
        .channel_init_mode = fake_channel_init,
        .channel_enable = fake_channel_enable,
        .channel_disable = fake_channel_disable,
        .channel_delete = fake_channel_delete,
        .task_create = fake_task_create,
        .task_release_start = fake_task_release,
        .task_wait_stopped = fake_task_wait,
        .channel_write = fake_write,
    };
}

static void initialize_component(void)
{
    hfp_i2s_output_platform_ops_t ops = make_ops();
    TEST_ASSERT_EQUAL(ESP_OK, hfp_i2s_output_test_set_platform_ops(&ops));
    TEST_ASSERT_EQUAL(ESP_OK, hfp_i2s_output_init(&config, &owners));
}

void setUp(void)
{
    hfp_i2s_output_test_reset();
    memset(&fake, 0, sizeof(fake));
    fake.channel_new_result = ESP_OK;
    fake.channel_init_result = ESP_OK;
    fake.channel_enable_result = ESP_OK;
    fake.channel_disable_result = ESP_OK;
    fake.channel_delete_result = ESP_OK;
    fake.task_create_result = ESP_OK;
    fake.task_release_result = ESP_OK;
    fake.task_wait_result = ESP_OK;
    fake.write_result = ESP_OK;

    TEST_ASSERT_EQUAL(ESP_OK, hfp_i2s_output_default_config(&config));
    owners = (hfp_i2s_pin_owners_t) {
        .playback_i2s_valid = true,
        .playback_i2s_port = 1,
        .playback_bclk_gpio = 18,
        .playback_ws_gpio = 19,
        .playback_din_gpio = 22,
        .playback_dout_gpio = -1,
        .uart2_enabled = true,
        .uart2_rx_gpio = 16,
        .uart2_tx_gpio = 17,
    };
}

void tearDown(void)
{
    hfp_i2s_output_test_reset();
}

void test_default_config_and_pin_validation(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, hfp_i2s_output_validate_config(&config, &owners));

    hfp_i2s_output_config_t invalid = config;
    invalid.port = 1;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      hfp_i2s_output_validate_config(&invalid, &owners));
    invalid = config;
    invalid.bclk_gpio = 18;
    TEST_ASSERT_NOT_EQUAL(ESP_OK,
                          hfp_i2s_output_validate_config(&invalid, &owners));
    invalid = config;
    invalid.bclk_gpio = 16;
    TEST_ASSERT_NOT_EQUAL(ESP_OK,
                          hfp_i2s_output_validate_config(&invalid, &owners));
    invalid = config;
    invalid.bclk_gpio = 34;
    TEST_ASSERT_NOT_EQUAL(ESP_OK,
                          hfp_i2s_output_validate_config(&invalid, &owners));
    invalid = config;
    invalid.bclk_gpio = 6;
    TEST_ASSERT_NOT_EQUAL(ESP_OK,
                          hfp_i2s_output_validate_config(&invalid, &owners));
    invalid = config;
    invalid.bclk_gpio = 0;
    TEST_ASSERT_NOT_EQUAL(ESP_OK,
                          hfp_i2s_output_validate_config(&invalid, &owners));
    invalid = config;
    invalid.dout_gpio = invalid.ws_gpio;
    TEST_ASSERT_NOT_EQUAL(ESP_OK,
                          hfp_i2s_output_validate_config(&invalid, &owners));
    invalid = config;
    invalid.sample_rate_hz = 8000;
    TEST_ASSERT_NOT_EQUAL(ESP_OK,
                          hfp_i2s_output_validate_config(&invalid, &owners));
}

static void assert_stopped_after_failed_start(esp_err_t expected)
{
    hfp_i2s_output_snapshot_t snapshot;
    TEST_ASSERT_EQUAL(expected, hfp_i2s_output_start(GENERATION, PEER));
    TEST_ASSERT_EQUAL(ESP_OK, hfp_i2s_output_get_snapshot(&snapshot));
    TEST_ASSERT_EQUAL(HFP_I2S_OUTPUT_STOPPED, snapshot.state);
    TEST_ASSERT_EQUAL_UINT64(1, snapshot.start_failures);
    TEST_ASSERT_EQUAL(expected, snapshot.last_error);
}

void test_ring_allocation_failures_roll_back(void)
{
    fake.fail_alloc_call = 1;
    initialize_component();
    assert_stopped_after_failed_start(ESP_ERR_NO_MEM);
    TEST_ASSERT_EQUAL_INT(0, fake.free_calls);

    hfp_i2s_output_test_reset();
    memset(&fake, 0, sizeof(fake));
    fake.channel_new_result = fake.channel_init_result = ESP_OK;
    fake.channel_enable_result = fake.channel_disable_result = ESP_OK;
    fake.channel_delete_result = fake.task_create_result = ESP_OK;
    fake.task_release_result = fake.task_wait_result = fake.write_result = ESP_OK;
    fake.fail_alloc_call = 2;
    initialize_component();
    assert_stopped_after_failed_start(ESP_ERR_NO_MEM);
    TEST_ASSERT_EQUAL_INT(1, fake.free_calls);
}

void test_channel_allocation_failure_rolls_back(void)
{
    fake.channel_new_result = ESP_FAIL;
    initialize_component();
    assert_stopped_after_failed_start(ESP_FAIL);
    TEST_ASSERT_EQUAL_INT(2, fake.free_calls);
    TEST_ASSERT_EQUAL_INT(0, fake.channel_delete_calls);
}

void test_mode_init_failure_rolls_back(void)
{
    fake.channel_init_result = ESP_FAIL;
    initialize_component();
    assert_stopped_after_failed_start(ESP_FAIL);
    TEST_ASSERT_EQUAL_INT(1, fake.channel_delete_calls);
    TEST_ASSERT_EQUAL_INT(2, fake.free_calls);
}

void test_task_create_failure_rolls_back(void)
{
    fake.task_create_result = ESP_ERR_NO_MEM;
    initialize_component();
    assert_stopped_after_failed_start(ESP_ERR_NO_MEM);
    TEST_ASSERT_EQUAL_INT(0, fake.task_wait_calls);
    TEST_ASSERT_EQUAL_INT(1, fake.channel_delete_calls);
    TEST_ASSERT_EQUAL_INT(2, fake.free_calls);
}

void test_channel_enable_failure_stops_task_and_rolls_back(void)
{
    fake.channel_enable_result = ESP_FAIL;
    initialize_component();
    assert_stopped_after_failed_start(ESP_FAIL);
    TEST_ASSERT_EQUAL_INT(1, fake.task_release_calls);
    TEST_ASSERT_EQUAL_INT(1, fake.task_wait_calls);
    TEST_ASSERT_EQUAL_INT(1, fake.channel_delete_calls);
    TEST_ASSERT_EQUAL_INT(2, fake.free_calls);
}

void test_start_rollback_timeout_returns_timeout_and_quarantines(void)
{
    hfp_i2s_output_snapshot_t snapshot;
    fake.channel_enable_result = ESP_FAIL;
    fake.task_wait_result = ESP_ERR_TIMEOUT;
    initialize_component();

    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT,
                      hfp_i2s_output_start(GENERATION, PEER));
    TEST_ASSERT_EQUAL(ESP_OK, hfp_i2s_output_get_snapshot(&snapshot));
    TEST_ASSERT_EQUAL(HFP_I2S_OUTPUT_QUARANTINED, snapshot.state);
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, snapshot.last_error);
    TEST_ASSERT_EQUAL_UINT64(1, snapshot.start_failures);
    TEST_ASSERT_EQUAL_INT(0, fake.channel_delete_calls);
    TEST_ASSERT_EQUAL_INT(0, fake.free_calls);
}

void test_start_cleanup_failure_returns_cleanup_error_and_quarantines(void)
{
    hfp_i2s_output_snapshot_t snapshot;
    fake.channel_init_result = ESP_FAIL;
    fake.channel_delete_result = ESP_ERR_INVALID_STATE;
    initialize_component();

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      hfp_i2s_output_start(GENERATION, PEER));
    TEST_ASSERT_EQUAL(ESP_OK, hfp_i2s_output_get_snapshot(&snapshot));
    TEST_ASSERT_EQUAL(HFP_I2S_OUTPUT_QUARANTINED, snapshot.state);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, snapshot.last_error);
    TEST_ASSERT_EQUAL_UINT64(1, snapshot.start_failures);
    TEST_ASSERT_EQUAL_INT(1, fake.channel_delete_calls);
    TEST_ASSERT_EQUAL_INT(0, fake.free_calls);
}

void test_start_stop_are_idempotent_for_same_session(void)
{
    hfp_i2s_output_snapshot_t snapshot;
    initialize_component();
    TEST_ASSERT_EQUAL(ESP_OK, hfp_i2s_output_start(GENERATION, PEER));
    TEST_ASSERT_EQUAL(ESP_OK, hfp_i2s_output_start(GENERATION, PEER));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      hfp_i2s_output_start(GENERATION + 1U, PEER));
    TEST_ASSERT_EQUAL(ESP_OK, hfp_i2s_output_stop(config.stop_timeout_ms));
    TEST_ASSERT_EQUAL(ESP_OK, hfp_i2s_output_stop(config.stop_timeout_ms));
    TEST_ASSERT_EQUAL(ESP_OK, hfp_i2s_output_get_snapshot(&snapshot));
    TEST_ASSERT_EQUAL(HFP_I2S_OUTPUT_STOPPED, snapshot.state);
    TEST_ASSERT_EQUAL_UINT64(1, snapshot.start_calls);
    TEST_ASSERT_EQUAL_INT(1, fake.channel_disable_calls);
    TEST_ASSERT_EQUAL_INT(1, fake.channel_delete_calls);
    TEST_ASSERT_EQUAL_INT(2, fake.free_calls);
}

void test_stop_timeout_quarantines_without_freeing_live_resources(void)
{
    hfp_i2s_output_snapshot_t snapshot;
    initialize_component();
    TEST_ASSERT_EQUAL(ESP_OK, hfp_i2s_output_start(GENERATION, PEER));
    fake.task_wait_result = ESP_ERR_TIMEOUT;
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, hfp_i2s_output_stop(10U));
    TEST_ASSERT_EQUAL(ESP_OK, hfp_i2s_output_get_snapshot(&snapshot));
    TEST_ASSERT_EQUAL(HFP_I2S_OUTPUT_QUARANTINED, snapshot.state);
    TEST_ASSERT_TRUE(snapshot.quarantined);
    TEST_ASSERT_EQUAL_UINT64(1, snapshot.stop_timeouts);
    TEST_ASSERT_EQUAL_INT(0, fake.channel_disable_calls);
    TEST_ASSERT_EQUAL_INT(0, fake.channel_delete_calls);
    TEST_ASSERT_EQUAL_INT(0, fake.free_calls);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, hfp_i2s_output_deinit());
}

void test_stale_generation_and_cvsd_duplication(void)
{
    const int16_t input[] = {1, -2, 32767};
    const int16_t expected[] = {1, 1, -2, -2, 32767, 32767};
    int16_t output[6] = {0};
    hfp_i2s_output_snapshot_t snapshot;

    initialize_component();
    TEST_ASSERT_EQUAL(ESP_OK, hfp_i2s_output_start(GENERATION, PEER));
    TEST_ASSERT_FALSE(hfp_i2s_output_push_cvsd(input, 3U, GENERATION - 1U));
    TEST_ASSERT_TRUE(hfp_i2s_output_push_cvsd(input, 3U, GENERATION));
    TEST_ASSERT_EQUAL(sizeof(output),
                      hfp_i2s_output_test_read_pcm(output, sizeof(output),
                                                   GENERATION));
    TEST_ASSERT_EQUAL_INT16_ARRAY(expected, output, 6U);
    TEST_ASSERT_FALSE(hfp_i2s_output_push_cvsd(NULL, 3U, GENERATION));
    TEST_ASSERT_FALSE(hfp_i2s_output_push_cvsd(input, 0U, GENERATION));
    TEST_ASSERT_EQUAL(ESP_OK, hfp_i2s_output_get_snapshot(&snapshot));
    TEST_ASSERT_EQUAL_UINT64(1, snapshot.stale_pushes);
    TEST_ASSERT_EQUAL_UINT64(2, snapshot.invalid_pushes);
}

void test_ring_full_rejects_entire_cvsd_frame(void)
{
    int16_t input[HFP_I2S_CVSD_MAX_INPUT_SAMPLES];
    int16_t output[HFP_I2S_CVSD_MAX_INPUT_SAMPLES * 4U];
    for (size_t i = 0; i < HFP_I2S_CVSD_MAX_INPUT_SAMPLES; ++i) {
        input[i] = (int16_t)i;
    }
    config.ring_bytes = 1024U;
    initialize_component();
    TEST_ASSERT_EQUAL(ESP_OK, hfp_i2s_output_start(GENERATION, PEER));
    TEST_ASSERT_TRUE(hfp_i2s_output_push_cvsd(
        input, HFP_I2S_CVSD_MAX_INPUT_SAMPLES, GENERATION));
    TEST_ASSERT_TRUE(hfp_i2s_output_push_cvsd(
        input, HFP_I2S_CVSD_MAX_INPUT_SAMPLES, GENERATION));
    TEST_ASSERT_FALSE(hfp_i2s_output_push_cvsd(
        input, HFP_I2S_CVSD_MAX_INPUT_SAMPLES, GENERATION));
    TEST_ASSERT_EQUAL(sizeof(output),
                      hfp_i2s_output_test_read_pcm(output, sizeof(output),
                                                   GENERATION));
}

void test_writer_counts_silence_and_faults_after_bounded_failures(void)
{
    hfp_i2s_output_snapshot_t snapshot;
    size_t requested_bytes = config.writer_samples * sizeof(int16_t);
    config.underflow_degraded_threshold = 1U;
    initialize_component();
    TEST_ASSERT_EQUAL(ESP_OK, hfp_i2s_output_start(GENERATION, PEER));
    TEST_ASSERT_EQUAL(ESP_OK, hfp_i2s_output_test_writer_once());
    TEST_ASSERT_EQUAL(requested_bytes, fake.last_write_bytes);
    for (size_t i = 0; i < fake.last_write_bytes; ++i) {
        TEST_ASSERT_EQUAL_UINT8(0, fake.last_write[i]);
    }
    TEST_ASSERT_EQUAL(ESP_OK, hfp_i2s_output_get_snapshot(&snapshot));
    TEST_ASSERT_EQUAL_UINT64(1, snapshot.silence_intervals);
    TEST_ASSERT_EQUAL_UINT64(config.writer_samples, snapshot.silence_samples);
    TEST_ASSERT_TRUE(snapshot.degraded);
    TEST_ASSERT_EQUAL_UINT64(1, snapshot.degraded_events);

    fake.short_write = true;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE,
                      hfp_i2s_output_test_writer_once());
    fake.short_write = false;
    fake.write_result = ESP_ERR_TIMEOUT;
    for (uint32_t i = 1U; i < config.max_consecutive_write_failures; ++i) {
        TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT,
                          hfp_i2s_output_test_writer_once());
    }
    TEST_ASSERT_EQUAL(ESP_OK, hfp_i2s_output_get_snapshot(&snapshot));
    TEST_ASSERT_EQUAL(HFP_I2S_OUTPUT_FAULTED, snapshot.state);
    TEST_ASSERT_EQUAL_UINT64(config.max_consecutive_write_failures,
                             snapshot.write_failures);
    TEST_ASSERT_EQUAL_UINT64(1, snapshot.short_writes);
    TEST_ASSERT_EQUAL_UINT64(
        1U + ((uint64_t)config.max_consecutive_write_failures - 1U) *
            requested_bytes,
        snapshot.write_lost_bytes);
    TEST_ASSERT_EQUAL_INT(1, fake.channel_disable_calls);
    fake.task_wait_result = ESP_OK;
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT,
                      hfp_i2s_output_stop(config.stop_timeout_ms));
    TEST_ASSERT_EQUAL(ESP_OK, hfp_i2s_output_deinit());
}

void test_cleanup_failure_quarantines(void)
{
    hfp_i2s_output_snapshot_t snapshot;
    initialize_component();
    TEST_ASSERT_EQUAL(ESP_OK, hfp_i2s_output_start(GENERATION, PEER));
    fake.channel_disable_result = ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL, hfp_i2s_output_stop(config.stop_timeout_ms));
    TEST_ASSERT_EQUAL(ESP_OK, hfp_i2s_output_get_snapshot(&snapshot));
    TEST_ASSERT_EQUAL(HFP_I2S_OUTPUT_QUARANTINED, snapshot.state);
    TEST_ASSERT_EQUAL_INT(0, fake.free_calls);
}
