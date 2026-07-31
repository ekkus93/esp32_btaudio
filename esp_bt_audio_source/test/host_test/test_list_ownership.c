#include "unity.h"
#include "osi/list.h"
#include "osi/allocator.h"

static bool freed = false;

static void my_free(void *p) {
    osi_free(p);  // Actually free the memory to prevent ASan leak
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

static void *alloc_payloads(int n, void **out) {
    for (int i = 0; i < n; i++) {
        out[i] = osi_calloc(sizeof(int));
        TEST_ASSERT_NOT_NULL(out[i]);
    }
    return NULL;
}

void test_list_length_and_is_empty(void) {
    list_t *l = list_new(my_free);
    TEST_ASSERT_NOT_NULL(l);
    TEST_ASSERT_TRUE(list_is_empty(l));
    TEST_ASSERT_EQUAL_UINT(0, list_length(l));

    void *p = osi_calloc(4);
    TEST_ASSERT_TRUE(list_append(l, p));
    TEST_ASSERT_FALSE(list_is_empty(l));
    TEST_ASSERT_EQUAL_UINT(1, list_length(l));

    list_free(l);
}

void test_list_front_and_back(void) {
    list_t *l = list_new(my_free);
    void *payloads[3];
    alloc_payloads(3, payloads);

    TEST_ASSERT_TRUE(list_append(l, payloads[0]));
    TEST_ASSERT_TRUE(list_append(l, payloads[1]));
    TEST_ASSERT_TRUE(list_append(l, payloads[2]));

    TEST_ASSERT_EQUAL_PTR(payloads[0], list_front(l));
    TEST_ASSERT_EQUAL_PTR(payloads[2], list_back(l));
    TEST_ASSERT_EQUAL_PTR(list_back_node(l), list_get_node(l, payloads[2]));

    list_free(l);
}

void test_list_prepend_adds_to_head(void) {
    list_t *l = list_new(my_free);
    void *payloads[2];
    alloc_payloads(2, payloads);

    TEST_ASSERT_TRUE(list_append(l, payloads[0]));
    TEST_ASSERT_TRUE(list_prepend(l, payloads[1]));

    TEST_ASSERT_EQUAL_PTR(payloads[1], list_front(l));
    TEST_ASSERT_EQUAL_PTR(payloads[0], list_back(l));
    TEST_ASSERT_EQUAL_UINT(2, list_length(l));

    list_free(l);
}

void test_list_insert_after_middle_and_tail(void) {
    list_t *l = list_new(my_free);
    void *payloads[3];
    alloc_payloads(3, payloads);

    TEST_ASSERT_TRUE(list_append(l, payloads[0]));
    list_node_t *head = list_begin(l);

    /* Insert after the tail: new node must become the tail. */
    TEST_ASSERT_TRUE(list_insert_after(l, head, payloads[1]));
    TEST_ASSERT_EQUAL_PTR(payloads[1], list_back(l));

    /* Insert after the (now middle) head node again: order is 0,2,1. */
    TEST_ASSERT_TRUE(list_insert_after(l, head, payloads[2]));
    TEST_ASSERT_EQUAL_UINT(3, list_length(l));
    TEST_ASSERT_EQUAL_PTR(payloads[0], list_front(l));
    TEST_ASSERT_EQUAL_PTR(payloads[2], list_node(list_next(list_begin(l))));
    TEST_ASSERT_EQUAL_PTR(payloads[1], list_back(l));

    list_free(l);
}

void test_list_contains_and_get_node(void) {
    list_t *l = list_new(my_free);
    void *payloads[2];
    alloc_payloads(2, payloads);
    int not_in_list = 0;

    TEST_ASSERT_TRUE(list_append(l, payloads[0]));
    TEST_ASSERT_TRUE(list_append(l, payloads[1]));

    TEST_ASSERT_TRUE(list_contains(l, payloads[1]));
    TEST_ASSERT_FALSE(list_contains(l, &not_in_list));
    TEST_ASSERT_NOT_NULL(list_get_node(l, payloads[1]));
    TEST_ASSERT_NULL(list_get_node(l, &not_in_list));

    list_free(l);
}

void test_list_iteration_walks_in_order(void) {
    list_t *l = list_new(my_free);
    void *payloads[3];
    alloc_payloads(3, payloads);
    TEST_ASSERT_TRUE(list_append(l, payloads[0]));
    TEST_ASSERT_TRUE(list_append(l, payloads[1]));
    TEST_ASSERT_TRUE(list_append(l, payloads[2]));

    int index = 0;
    for (list_node_t *node = list_begin(l); node != list_end(l); node = list_next(node)) {
        TEST_ASSERT_EQUAL_PTR(payloads[index], list_node(node));
        index++;
    }
    TEST_ASSERT_EQUAL_INT(3, index);

    list_free(l);
}

static bool stop_at_second(void *data, void *context) {
    int *count = (int *)context;
    (*count)++;
    return *count < 2;
}

void test_list_foreach_stops_on_false_return(void) {
    list_t *l = list_new(my_free);
    void *payloads[3];
    alloc_payloads(3, payloads);
    TEST_ASSERT_TRUE(list_append(l, payloads[0]));
    TEST_ASSERT_TRUE(list_append(l, payloads[1]));
    TEST_ASSERT_TRUE(list_append(l, payloads[2]));

    int visited = 0;
    list_node_t *stopped_at = list_foreach(l, stop_at_second, &visited);
    TEST_ASSERT_EQUAL_INT(2, visited);
    TEST_ASSERT_EQUAL_PTR(payloads[1], list_node(stopped_at));

    list_free(l);
}

void test_list_clear_resets_to_empty(void) {
    list_t *l = list_new(my_free);
    void *payloads[2];
    alloc_payloads(2, payloads);
    TEST_ASSERT_TRUE(list_append(l, payloads[0]));
    TEST_ASSERT_TRUE(list_append(l, payloads[1]));

    list_clear(l);
    TEST_ASSERT_TRUE(freed);
    TEST_ASSERT_TRUE(list_is_empty(l));
    TEST_ASSERT_EQUAL_UINT(0, list_length(l));
    TEST_ASSERT_EQUAL_PTR(list_begin(l), list_end(l));

    list_free(l);
}

void test_list_remove_from_middle_and_tail_updates_links(void) {
    list_t *l = list_new(my_free);
    void *payloads[3];
    alloc_payloads(3, payloads);
    TEST_ASSERT_TRUE(list_append(l, payloads[0]));
    TEST_ASSERT_TRUE(list_append(l, payloads[1]));
    TEST_ASSERT_TRUE(list_append(l, payloads[2]));

    /* Remove the middle element; head and tail must be unaffected. */
    TEST_ASSERT_TRUE(list_remove(l, payloads[1]));
    TEST_ASSERT_EQUAL_UINT(2, list_length(l));
    TEST_ASSERT_EQUAL_PTR(payloads[0], list_front(l));
    TEST_ASSERT_EQUAL_PTR(payloads[2], list_back(l));

    /* Remove the tail; the new tail must be the previous element. */
    TEST_ASSERT_TRUE(list_remove(l, payloads[2]));
    TEST_ASSERT_EQUAL_PTR(payloads[0], list_back(l));

    /* Removing something not present returns false and leaves list intact. */
    int missing = 0;
    TEST_ASSERT_FALSE(list_remove(l, &missing));
    TEST_ASSERT_EQUAL_UINT(1, list_length(l));

    list_free(l);
}

void test_list_delete_from_tail_preserves_ownership_and_updates_tail(void) {
    list_t *l = list_new(my_free);
    void *payloads[2];
    alloc_payloads(2, payloads);
    TEST_ASSERT_TRUE(list_append(l, payloads[0]));
    TEST_ASSERT_TRUE(list_append(l, payloads[1]));

    /* Delete the tail node without freeing its payload. */
    TEST_ASSERT_TRUE(list_delete(l, payloads[1]));
    TEST_ASSERT_FALSE(freed);
    TEST_ASSERT_EQUAL_PTR(payloads[0], list_back(l));
    osi_free(payloads[1]);

    list_free(l);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_list_delete_preserves_ownership);
    RUN_TEST(test_list_remove_frees_payload);
    RUN_TEST(test_list_length_and_is_empty);
    RUN_TEST(test_list_front_and_back);
    RUN_TEST(test_list_prepend_adds_to_head);
    RUN_TEST(test_list_insert_after_middle_and_tail);
    RUN_TEST(test_list_contains_and_get_node);
    RUN_TEST(test_list_iteration_walks_in_order);
    RUN_TEST(test_list_foreach_stops_on_false_return);
    RUN_TEST(test_list_clear_resets_to_empty);
    RUN_TEST(test_list_remove_from_middle_and_tail_updates_links);
    RUN_TEST(test_list_delete_from_tail_preserves_ownership_and_updates_tail);
    return UNITY_END();
}
