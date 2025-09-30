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

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_osi_mem_dbg_basic_record_and_clean);
    RUN_TEST(test_osi_mem_dbg_ignore_null_and_unknown_clean);
    RUN_TEST(test_osi_mem_dbg_sections_track_max_size);
    return UNITY_END();
}
