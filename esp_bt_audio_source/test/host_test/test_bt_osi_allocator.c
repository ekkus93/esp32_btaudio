#include "unity.h"
#include "osi/allocator.h"
#include <string.h>

void setUp(void) {
    /* Initialize the debug bookkeeping table */
    osi_mem_dbg_init();
}

void tearDown(void) {
    /* nothing */
}

void test_osi_mem_dbg_basic_record_and_clean(void) {
    uint32_t before = osi_mem_dbg_get_current_size();
    TEST_ASSERT_EQUAL_UINT32(0, before);

    /* allocate via macros so record is called */
    char *p = (char *)osi_malloc(16);
    TEST_ASSERT_NOT_NULL(p);
    memset(p, 0xaa, 16);

    uint32_t after = osi_mem_dbg_get_current_size();
    TEST_ASSERT_EQUAL_UINT32(16, after);

    /* free and ensure size goes back to zero */
    osi_free(p);
    uint32_t final = osi_mem_dbg_get_current_size();
    TEST_ASSERT_EQUAL_UINT32(0, final);
}

void test_osi_mem_dbg_ignore_null_and_unknown_clean(void) {
    /* free(NULL) should be ignored */
    osi_free(NULL);

    /* cleaning an unknown pointer should not crash; we can't assert log here but ensure current size remains 0 */
    int dummy;
    osi_mem_dbg_clean(&dummy, __func__, __LINE__);
    TEST_ASSERT_EQUAL_UINT32(0, osi_mem_dbg_get_current_size());
}

void test_osi_mem_dbg_sections_track_max_size(void) {
    osi_mem_dbg_init();
    osi_men_dbg_set_section_start(0);

    char *a = (char *)osi_malloc(8);
    TEST_ASSERT_NOT_NULL(a);
    char *b = (char *)osi_malloc(24);
    TEST_ASSERT_NOT_NULL(b);

    /* current size should be 32 */
    TEST_ASSERT_EQUAL_UINT32(32, osi_mem_dbg_get_current_size());

    osi_men_dbg_set_section_end(0);
    uint32_t sec_max = osi_mem_dbg_get_max_size_section(0);
    TEST_ASSERT_TRUE(sec_max >= 32);

    osi_free(a);
    osi_free(b);
}

void test_osi_mem_dbg_get_max_size_and_entry_count(void) {
    osi_mem_dbg_init();
    TEST_ASSERT_EQUAL_UINT32(0, osi_mem_dbg_get_max_size());
    TEST_ASSERT_EQUAL_UINT32(0, osi_mem_dbg_get_entry_count());

    char *a = (char *)osi_malloc(8);
    char *b = (char *)osi_malloc(24);
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_EQUAL_UINT32(2, osi_mem_dbg_get_entry_count());
    TEST_ASSERT_EQUAL_UINT32(32, osi_mem_dbg_get_max_size());

    osi_free(a);
    /* Freeing one entry must not lower the lifetime max-size watermark. */
    TEST_ASSERT_EQUAL_UINT32(32, osi_mem_dbg_get_max_size());
    TEST_ASSERT_EQUAL_UINT32(1, osi_mem_dbg_get_entry_count());

    osi_free(b);
}

void test_osi_strdup_copies_string(void) {
    char *dup = osi_strdup("hello");
    TEST_ASSERT_NOT_NULL(dup);
    TEST_ASSERT_EQUAL_STRING("hello", dup);
    osi_free(dup);
}

void test_osi_mem_dbg_show_does_not_crash(void) {
    osi_mem_dbg_init();
    char *a = (char *)osi_malloc(4);
    TEST_ASSERT_NOT_NULL(a);
    /* No return value to assert; this exercises the diagnostic dump path. */
    osi_mem_dbg_show();
    osi_free(a);
}

void test_osi_mem_dbg_section_restart_and_end_without_start(void) {
    osi_mem_dbg_init();

    /* Starting an already-started section logs a warning but does not fail. */
    osi_men_dbg_set_section_start(1);
    osi_men_dbg_set_section_start(1);
    osi_men_dbg_set_section_end(1);

    /* Ending a section that was never started logs an error but does not
     * crash and leaves the section's max size at its default. */
    TEST_ASSERT_EQUAL_UINT32(0, osi_mem_dbg_get_max_size_section(2));
    osi_men_dbg_set_section_end(2);
    TEST_ASSERT_EQUAL_UINT32(0, osi_mem_dbg_get_max_size_section(2));
}

void test_osi_mem_dbg_section_out_of_range_index_is_rejected(void) {
    osi_mem_dbg_init();
    /* OSI_MEM_DBG_MAX_SECTION_NUM is 5; index 5 is out of range. */
    osi_men_dbg_set_section_start(5);
    osi_men_dbg_set_section_end(5);
    TEST_ASSERT_EQUAL_UINT32(0, osi_mem_dbg_get_max_size_section(5));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_osi_mem_dbg_basic_record_and_clean);
    RUN_TEST(test_osi_mem_dbg_ignore_null_and_unknown_clean);
    RUN_TEST(test_osi_mem_dbg_sections_track_max_size);
    RUN_TEST(test_osi_mem_dbg_get_max_size_and_entry_count);
    RUN_TEST(test_osi_strdup_copies_string);
    RUN_TEST(test_osi_mem_dbg_show_does_not_crash);
    RUN_TEST(test_osi_mem_dbg_section_restart_and_end_without_start);
    RUN_TEST(test_osi_mem_dbg_section_out_of_range_index_is_rejected);
    return UNITY_END();
}
