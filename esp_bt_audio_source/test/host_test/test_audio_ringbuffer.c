/**
 * @file test_audio_ringbuffer.c
 * @brief Unit tests for audio ring buffer (SPSC)
 *
 * Tests cover:
 * - Basic operations (init, write, read, capacity queries)
 * - Edge cases (empty, full, partial operations)
 * - Wrap-around behavior
 * - Peak tracking
 * - Stress scenarios
 *
 * CODE_REVIEW6 Phase 1, Task 1.2
 */

#include "unity.h"
#include "audio_ringbuffer.h"
#include <string.h>

/* Test fixtures */
static audio_rb_t *rb = NULL;

void setUp(void)
{
    /* Each test starts fresh */
    rb = NULL;
}

void tearDown(void)
{
    /* Clean up after each test */
    if (rb != NULL) {
        audio_rb_deinit(rb);
        rb = NULL;
    }
}

//-----------------------------------------------------------------------------
// Basic operations
//-----------------------------------------------------------------------------

void test_rb_init_and_capacity(void)
{
    const size_t capacity = 1024;
    esp_err_t err = audio_rb_init(&rb, capacity, false);
    
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_NOT_NULL(rb);
    TEST_ASSERT_EQUAL_UINT(capacity, audio_rb_capacity(rb));
    TEST_ASSERT_EQUAL_UINT(capacity, audio_rb_available_to_write(rb));
    TEST_ASSERT_EQUAL_UINT(0, audio_rb_available_to_read(rb));
    TEST_ASSERT_EQUAL_UINT(0, audio_rb_peak_used(rb));
}

void test_rb_write_and_read_simple(void)
{
    const size_t capacity = 256;
    TEST_ASSERT_EQUAL(ESP_OK, audio_rb_init(&rb, capacity, false));
    
    /* Write some data */
    uint8_t write_data[64];
    for (size_t i = 0; i < sizeof(write_data); i++) {
        write_data[i] = (uint8_t)(i & 0xFF);
    }
    
    size_t written = audio_rb_write(rb, write_data, sizeof(write_data));
    TEST_ASSERT_EQUAL_UINT(sizeof(write_data), written);
    TEST_ASSERT_EQUAL_UINT(sizeof(write_data), audio_rb_available_to_read(rb));
    TEST_ASSERT_EQUAL_UINT(capacity - sizeof(write_data), audio_rb_available_to_write(rb));
    
    /* Read it back */
    uint8_t read_data[64];
    size_t read = audio_rb_read(rb, read_data, sizeof(read_data));
    TEST_ASSERT_EQUAL_UINT(sizeof(read_data), read);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(write_data, read_data, sizeof(write_data));
    
    /* Ring should be empty now */
    TEST_ASSERT_EQUAL_UINT(0, audio_rb_available_to_read(rb));
    TEST_ASSERT_EQUAL_UINT(capacity, audio_rb_available_to_write(rb));
}

void test_rb_wrap_around(void)
{
    const size_t capacity = 128;
    TEST_ASSERT_EQUAL(ESP_OK, audio_rb_init(&rb, capacity, false));
    
    /* Write data that will wrap around */
    uint8_t write_data[100];
    for (size_t i = 0; i < sizeof(write_data); i++) {
        write_data[i] = (uint8_t)(i & 0xFF);
    }
    
    /* First write: fills most of buffer */
    size_t written = audio_rb_write(rb, write_data, sizeof(write_data));
    TEST_ASSERT_EQUAL_UINT(sizeof(write_data), written);
    
    /* Read half */
    uint8_t read_data1[50];
    size_t read = audio_rb_read(rb, read_data1, sizeof(read_data1));
    TEST_ASSERT_EQUAL_UINT(sizeof(read_data1), read);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(write_data, read_data1, sizeof(read_data1));
    
    /* Write more data (this will wrap around) */
    uint8_t write_data2[60];
    for (size_t i = 0; i < sizeof(write_data2); i++) {
        write_data2[i] = (uint8_t)((i + 200) & 0xFF);
    }
    written = audio_rb_write(rb, write_data2, sizeof(write_data2));
    TEST_ASSERT_EQUAL_UINT(sizeof(write_data2), written);
    
    /* Read remaining data from first write */
    uint8_t read_data2[50];
    read = audio_rb_read(rb, read_data2, sizeof(read_data2));
    TEST_ASSERT_EQUAL_UINT(sizeof(read_data2), read);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(write_data + 50, read_data2, sizeof(read_data2));
    
    /* Read wrapped data */
    uint8_t read_data3[60];
    read = audio_rb_read(rb, read_data3, sizeof(read_data3));
    TEST_ASSERT_EQUAL_UINT(sizeof(read_data3), read);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(write_data2, read_data3, sizeof(write_data2));
}

void test_rb_available_counts_correct(void)
{
    const size_t capacity = 512;
    TEST_ASSERT_EQUAL(ESP_OK, audio_rb_init(&rb, capacity, false));
    
    /* Write 200 bytes */
    uint8_t data[200];
    memset(data, 0xAA, sizeof(data));
    audio_rb_write(rb, data, sizeof(data));
    
    TEST_ASSERT_EQUAL_UINT(200, audio_rb_available_to_read(rb));
    TEST_ASSERT_EQUAL_UINT(capacity - 200, audio_rb_available_to_write(rb));
    
    /* Read 100 bytes */
    audio_rb_read(rb, data, 100);
    
    TEST_ASSERT_EQUAL_UINT(100, audio_rb_available_to_read(rb));
    TEST_ASSERT_EQUAL_UINT(capacity - 100, audio_rb_available_to_write(rb));
    
    /* Read remaining 100 bytes */
    audio_rb_read(rb, data, 100);
    
    TEST_ASSERT_EQUAL_UINT(0, audio_rb_available_to_read(rb));
    TEST_ASSERT_EQUAL_UINT(capacity, audio_rb_available_to_write(rb));
}

void test_rb_peak_tracking(void)
{
    const size_t capacity = 1024;
    TEST_ASSERT_EQUAL(ESP_OK, audio_rb_init(&rb, capacity, false));
    
    uint8_t data[200];  /* Large enough for all operations */
    memset(data, 0x55, sizeof(data));
    
    /* Write 100 bytes */
    audio_rb_write(rb, data, 100);
    TEST_ASSERT_EQUAL_UINT(100, audio_rb_peak_used(rb));
    
    /* Write another 200 bytes (total 300) */
    audio_rb_write(rb, data, 100);
    audio_rb_write(rb, data, 100);
    TEST_ASSERT_EQUAL_UINT(300, audio_rb_peak_used(rb));
    
    /* Read 150 bytes (leaves 150) */
    audio_rb_read(rb, data, 150);
    TEST_ASSERT_EQUAL_UINT(300, audio_rb_peak_used(rb));  /* Peak doesn't decrease */
    
    /* Write 400 more bytes (total 550) */
    for (int i = 0; i < 4; i++) {
        audio_rb_write(rb, data, 100);
    }
    TEST_ASSERT_EQUAL_UINT(550, audio_rb_peak_used(rb));
    
    /* Reset stats */
    audio_rb_reset_stats(rb);
    TEST_ASSERT_EQUAL_UINT(550, audio_rb_peak_used(rb));  /* Reset to current, not zero */
}

//-----------------------------------------------------------------------------
// Edge cases
//-----------------------------------------------------------------------------

void test_rb_write_when_full_returns_zero(void)
{
    const size_t capacity = 128;
    TEST_ASSERT_EQUAL(ESP_OK, audio_rb_init(&rb, capacity, false));
    
    uint8_t data[128];
    memset(data, 0x11, sizeof(data));
    
    /* Fill buffer completely */
    size_t written = audio_rb_write(rb, data, capacity);
    TEST_ASSERT_EQUAL_UINT(capacity, written);
    TEST_ASSERT_EQUAL_UINT(0, audio_rb_available_to_write(rb));
    
    /* Try to write more (should fail) */
    written = audio_rb_write(rb, data, 10);
    TEST_ASSERT_EQUAL_UINT(0, written);
}

void test_rb_read_when_empty_returns_zero(void)
{
    const size_t capacity = 128;
    TEST_ASSERT_EQUAL(ESP_OK, audio_rb_init(&rb, capacity, false));
    
    uint8_t data[64];
    
    /* Try to read from empty buffer */
    size_t read = audio_rb_read(rb, data, sizeof(data));
    TEST_ASSERT_EQUAL_UINT(0, read);
}

void test_rb_partial_write_when_insufficient_space(void)
{
    const size_t capacity = 100;
    TEST_ASSERT_EQUAL(ESP_OK, audio_rb_init(&rb, capacity, false));
    
    uint8_t data[150];
    for (size_t i = 0; i < sizeof(data); i++) {
        data[i] = (uint8_t)(i & 0xFF);
    }
    
    /* Try to write more than capacity (should write partial) */
    size_t written = audio_rb_write(rb, data, sizeof(data));
    TEST_ASSERT_EQUAL_UINT(capacity, written);  /* Only capacity bytes written */
    
    /* Verify correct data */
    uint8_t read_data[100];
    size_t read = audio_rb_read(rb, read_data, sizeof(read_data));
    TEST_ASSERT_EQUAL_UINT(capacity, read);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data, read_data, capacity);
}

void test_rb_partial_read_when_insufficient_data(void)
{
    const size_t capacity = 256;
    TEST_ASSERT_EQUAL(ESP_OK, audio_rb_init(&rb, capacity, false));
    
    uint8_t write_data[50];
    for (size_t i = 0; i < sizeof(write_data); i++) {
        write_data[i] = (uint8_t)(i & 0xFF);
    }
    
    audio_rb_write(rb, write_data, sizeof(write_data));
    
    /* Try to read more than available */
    uint8_t read_data[100];
    size_t read = audio_rb_read(rb, read_data, sizeof(read_data));
    TEST_ASSERT_EQUAL_UINT(50, read);  /* Only 50 bytes available */
    TEST_ASSERT_EQUAL_UINT8_ARRAY(write_data, read_data, 50);
}

//-----------------------------------------------------------------------------
// Stress tests
//-----------------------------------------------------------------------------

void test_rb_alternating_write_read_many_times(void)
{
    const size_t capacity = 512;
    TEST_ASSERT_EQUAL(ESP_OK, audio_rb_init(&rb, capacity, false));
    
    uint8_t write_data[64];
    uint8_t read_data[64];
    
    /* Alternate writing and reading many times */
    for (int iteration = 0; iteration < 100; iteration++) {
        /* Fill write buffer with iteration-specific pattern */
        for (size_t i = 0; i < sizeof(write_data); i++) {
            write_data[i] = (uint8_t)((iteration + i) & 0xFF);
        }
        
        /* Write */
        size_t written = audio_rb_write(rb, write_data, sizeof(write_data));
        TEST_ASSERT_EQUAL_UINT(sizeof(write_data), written);
        
        /* Read */
        size_t read = audio_rb_read(rb, read_data, sizeof(read_data));
        TEST_ASSERT_EQUAL_UINT(sizeof(read_data), read);
        
        /* Verify data integrity */
        TEST_ASSERT_EQUAL_UINT8_ARRAY(write_data, read_data, sizeof(write_data));
    }
    
    /* Buffer should be empty after all iterations */
    TEST_ASSERT_EQUAL_UINT(0, audio_rb_available_to_read(rb));
}

void test_rb_split_writes_across_wrap(void)
{
    const size_t capacity = 200;
    TEST_ASSERT_EQUAL(ESP_OK, audio_rb_init(&rb, capacity, false));
    
    uint8_t write_data[150];
    for (size_t i = 0; i < sizeof(write_data); i++) {
        write_data[i] = (uint8_t)(i & 0xFF);
    }
    
    /* Fill buffer near capacity */
    audio_rb_write(rb, write_data, 150);
    
    /* Read most of it (leaves 50 bytes, head near end) */
    uint8_t read_data[100];
    audio_rb_read(rb, read_data, 100);
    
    /* Write data that will split across wrap point */
    uint8_t write_data2[160];
    for (size_t i = 0; i < sizeof(write_data2); i++) {
        write_data2[i] = (uint8_t)((i + 100) & 0xFF);
    }
    
    size_t written = audio_rb_write(rb, write_data2, sizeof(write_data2));
    TEST_ASSERT_EQUAL_UINT(150, written);  /* Only 150 bytes free (200 capacity - 50 remaining) */
    
    /* Read everything back and verify */
    uint8_t final_read[200];
    
    /* Read remaining 50 from first write */
    size_t read = audio_rb_read(rb, final_read, 50);
    TEST_ASSERT_EQUAL_UINT(50, read);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(write_data + 100, final_read, 50);
    
    /* Read 150 from second write (wrapped data) */
    read = audio_rb_read(rb, final_read, 150);
    TEST_ASSERT_EQUAL_UINT(150, read);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(write_data2, final_read, 150);
}

//-----------------------------------------------------------------------------
// Aggressive stress tests (Phase 5)
//-----------------------------------------------------------------------------

void test_rb_stress_random_size_operations(void)
{
    const size_t capacity = 4096;
    TEST_ASSERT_EQUAL(ESP_OK, audio_rb_init(&rb, capacity, false));
    
    uint8_t write_buf[1024];
    uint8_t read_buf[1024];
    uint32_t total_written = 0;
    uint32_t total_read = 0;
    
    /* Seed pattern for verification */
    for (size_t i = 0; i < sizeof(write_buf); i++) {
        write_buf[i] = (uint8_t)(i & 0xFF);
    }
    
    /* 10,000 iterations of random-sized writes and reads */
    for (int iter = 0; iter < 10000; iter++) {
        /* Random write size (1-512 bytes) */
        size_t write_size = 1 + (iter % 512);
        if (write_size > sizeof(write_buf)) write_size = sizeof(write_buf);
        
        size_t written = audio_rb_write(rb, write_buf, write_size);
        total_written += written;
        
        /* Random read size (1-256 bytes) */
        size_t read_size = 1 + ((iter * 7) % 256);
        if (read_size > sizeof(read_buf)) read_size = sizeof(read_buf);
        
        size_t read = audio_rb_read(rb, read_buf, read_size);
        total_read += read;
        
        /* Invariants */
        size_t available = audio_rb_available_to_read(rb);
        size_t free = audio_rb_available_to_write(rb);
        TEST_ASSERT_EQUAL_UINT(capacity, available + free);
        TEST_ASSERT_EQUAL_UINT(total_written - total_read, available);
    }
    
    /* Drain remaining data */
    while (audio_rb_available_to_read(rb) > 0) {
        audio_rb_read(rb, read_buf, sizeof(read_buf));
    }
    
    TEST_ASSERT_EQUAL_UINT(0, audio_rb_available_to_read(rb));
}

void test_rb_stress_fill_drain_cycles(void)
{
    const size_t capacity = 8192;
    TEST_ASSERT_EQUAL(ESP_OK, audio_rb_init(&rb, capacity, false));
    
    uint8_t pattern[1024];
    uint8_t verify[1024];
    
    for (size_t i = 0; i < sizeof(pattern); i++) {
        pattern[i] = (uint8_t)(i & 0xFF);
    }
    
    /* 1000 fill-drain cycles */
    for (int cycle = 0; cycle < 1000; cycle++) {
        /* Fill buffer completely */
        size_t remaining = capacity;
        while (remaining > 0) {
            size_t chunk = (remaining > sizeof(pattern)) ? sizeof(pattern) : remaining;
            size_t written = audio_rb_write(rb, pattern, chunk);
            TEST_ASSERT_EQUAL_UINT(chunk, written);
            remaining -= written;
        }
        
        TEST_ASSERT_EQUAL_UINT(0, audio_rb_available_to_write(rb));
        TEST_ASSERT_EQUAL_UINT(capacity, audio_rb_available_to_read(rb));
        
        /* Drain buffer completely */
        remaining = capacity;
        while (remaining > 0) {
            size_t chunk = (remaining > sizeof(verify)) ? sizeof(verify) : remaining;
            size_t read = audio_rb_read(rb, verify, chunk);
            TEST_ASSERT_EQUAL_UINT(chunk, read);
            remaining -= read;
        }
        
        TEST_ASSERT_EQUAL_UINT(0, audio_rb_available_to_read(rb));
        TEST_ASSERT_EQUAL_UINT(capacity, audio_rb_available_to_write(rb));
    }
    
    /* Peak should be capacity */
    TEST_ASSERT_EQUAL_UINT(capacity, audio_rb_peak_used(rb));
}

void test_rb_stress_alternating_small_large(void)
{
    const size_t capacity = 16384;
    TEST_ASSERT_EQUAL(ESP_OK, audio_rb_init(&rb, capacity, false));
    
    uint8_t small_buf[64];
    uint8_t large_buf[4096];
    
    /* Initialize patterns */
    for (size_t i = 0; i < sizeof(small_buf); i++) {
        small_buf[i] = (uint8_t)(i & 0xFF);
    }
    for (size_t i = 0; i < sizeof(large_buf); i++) {
        large_buf[i] = (uint8_t)((i >> 3) & 0xFF);
    }
    
    /* 5000 iterations: alternate small writes with large reads and vice versa */
    for (int iter = 0; iter < 5000; iter++) {
        if (iter % 2 == 0) {
            /* Many small writes, few large reads */
            for (int i = 0; i < 16; i++) {
                audio_rb_write(rb, small_buf, sizeof(small_buf));
            }
            audio_rb_read(rb, large_buf, sizeof(large_buf));
        } else {
            /* Few large writes, many small reads */
            audio_rb_write(rb, large_buf, sizeof(large_buf));
            for (int i = 0; i < 16; i++) {
                audio_rb_read(rb, small_buf, sizeof(small_buf));
            }
        }
        
        /* Verify invariants */
        size_t avail = audio_rb_available_to_read(rb);
        size_t free = audio_rb_available_to_write(rb);
        TEST_ASSERT_EQUAL_UINT(capacity, avail + free);
    }
}

//-----------------------------------------------------------------------------
// NULL/invalid parameter tests
//-----------------------------------------------------------------------------

void test_rb_init_rejects_null_pointer(void)
{
    esp_err_t err = audio_rb_init(NULL, 1024, false);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
}

void test_rb_init_rejects_zero_capacity(void)
{
    esp_err_t err = audio_rb_init(&rb, 0, false);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
}

void test_rb_write_handles_null_rb(void)
{
    uint8_t data[10];
    size_t written = audio_rb_write(NULL, data, sizeof(data));
    TEST_ASSERT_EQUAL_UINT(0, written);
}

void test_rb_read_handles_null_rb(void)
{
    uint8_t data[10];
    size_t read = audio_rb_read(NULL, data, sizeof(data));
    TEST_ASSERT_EQUAL_UINT(0, read);
}

void test_rb_queries_handle_null_rb(void)
{
    TEST_ASSERT_EQUAL_UINT(0, audio_rb_available_to_read(NULL));
    TEST_ASSERT_EQUAL_UINT(0, audio_rb_available_to_write(NULL));
    TEST_ASSERT_EQUAL_UINT(0, audio_rb_capacity(NULL));
    TEST_ASSERT_EQUAL_UINT(0, audio_rb_peak_used(NULL));
}

void test_rb_deinit_handles_null_safely(void)
{
    /* Should not crash */
    audio_rb_deinit(NULL);
}

//-----------------------------------------------------------------------------
// Unity test runner
//-----------------------------------------------------------------------------

void run_all_tests(void)
{
    /* Basic operations */
    RUN_TEST(test_rb_init_and_capacity);
    RUN_TEST(test_rb_write_and_read_simple);
    RUN_TEST(test_rb_wrap_around);
    RUN_TEST(test_rb_available_counts_correct);
    RUN_TEST(test_rb_peak_tracking);
    
    /* Edge cases */
    RUN_TEST(test_rb_write_when_full_returns_zero);
    RUN_TEST(test_rb_read_when_empty_returns_zero);
    RUN_TEST(test_rb_partial_write_when_insufficient_space);
    RUN_TEST(test_rb_partial_read_when_insufficient_data);
    
    /* Stress */
    RUN_TEST(test_rb_alternating_write_read_many_times);
    RUN_TEST(test_rb_split_writes_across_wrap);
    
    /* Aggressive stress (Phase 5) */
    RUN_TEST(test_rb_stress_random_size_operations);
    RUN_TEST(test_rb_stress_fill_drain_cycles);
    RUN_TEST(test_rb_stress_alternating_small_large);
    
    /* NULL/invalid parameters */
    RUN_TEST(test_rb_init_rejects_null_pointer);
    RUN_TEST(test_rb_init_rejects_zero_capacity);
    RUN_TEST(test_rb_write_handles_null_rb);
    RUN_TEST(test_rb_read_handles_null_rb);
    RUN_TEST(test_rb_queries_handle_null_rb);
    RUN_TEST(test_rb_deinit_handles_null_safely);
}

int main(void)
{
    UNITY_BEGIN();
    run_all_tests();
    return UNITY_END();
}
