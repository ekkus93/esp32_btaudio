#include "unity.h"
#include "unity.h"
#include "osi/list.h"
#include "osi/allocator.h"

static bool freed = false;

static void my_free(void *p) {
    (void)p;
    freed = true;
}

void setUp(void) {
    freed = false;
}

void tearDown(void) {
}

void test_list_delete_preserves_ownership(void) {
    list_t *l = list_new(my_free);
    TEST_ASSERT_NOT_NULL(l);

    void *payload = osi_calloc(16);
    TEST_ASSERT_NOT_NULL(payload);

    TEST_ASSERT_TRUE(list_append(l, payload));
    /* remove the node but do not let list free the payload */
    list_delete(l, payload);
    /* payload should still be allocated */
    TEST_ASSERT_FALSE(freed);
    /* now free payload explicitly */
    osi_free(payload);
    /* sanity */
    TEST_ASSERT_TRUE(true);
    list_free(l);
}

void test_list_remove_frees_payload(void) {
    list_t *l = list_new(my_free);
    TEST_ASSERT_NOT_NULL(l);

    void *payload = osi_calloc(16);
    TEST_ASSERT_NOT_NULL(payload);

    TEST_ASSERT_TRUE(list_append(l, payload));
    /* remove the node and let list free the payload */
    list_remove(l, payload);
    TEST_ASSERT_TRUE(freed);
    list_free(l);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_list_delete_preserves_ownership);
    RUN_TEST(test_list_remove_frees_payload);
    return UNITY_END();
}
