/* RADIO-1c: host tests for the pure station store. */
#include "unity.h"
#include "station_store.h"
#include <string.h>
#include <stdio.h>

void setUp(void) {}
void tearDown(void) {}

void test_add_and_get(void)
{
    station_store_t s;
    station_store_init(&s);
    int idx = -1;
    station_result_t r = station_store_add(&s, "SomaFM", "http://somafm.com/groovesalad.pls", &idx);
    TEST_ASSERT_EQUAL_INT(STATION_OK, r);
    TEST_ASSERT_EQUAL_INT(0, idx);
    TEST_ASSERT_EQUAL_INT(1, s.count);
    TEST_ASSERT_EQUAL_STRING("SomaFM", s.items[0].name);
    TEST_ASSERT_EQUAL_STRING("http://somafm.com/groovesalad.pls", s.items[0].url);
    TEST_ASSERT_EQUAL_UINT32(1, s.items[0].id);
}

void test_stable_ids_increment(void)
{
    station_store_t s;
    station_store_init(&s);
    int idx;

    idx = -1;
    station_store_add(&s, "A", "http://a/s", &idx);
    TEST_ASSERT_EQUAL_UINT32(1, s.items[0].id);

    idx = -1;
    station_store_add(&s, "B", "http://b/s", &idx);
    TEST_ASSERT_EQUAL_UINT32(2, s.items[1].id);

    idx = -1;
    station_store_add(&s, "C", "http://c/s", &idx);
    TEST_ASSERT_EQUAL_UINT32(3, s.items[2].id);

    /* IDs don't change on update */
    TEST_ASSERT_TRUE(station_store_update(&s, 0, "A2", "http://a2/s") == STATION_OK);
    TEST_ASSERT_EQUAL_UINT32(1, s.items[0].id);
}

void test_blank_name_defaults_to_host(void)
{
    station_store_t s;
    station_store_init(&s);
    int idx;
    station_store_add(&s, "", "https://ice6.somafm.com:8080/dronezone", &idx);
    TEST_ASSERT_EQUAL_STRING("ice6.somafm.com", s.items[0].name);

    station_store_add(&s, NULL, "http://example.org/stream", &idx);
    TEST_ASSERT_EQUAL_STRING("example.org", s.items[1].name);
}

void test_reject_invalid_url(void)
{
    station_store_t s;
    station_store_init(&s);
    int idx;
    TEST_ASSERT_EQUAL_INT(STATION_ERR_INVALID_URL, station_store_add(&s, "x", "ftp://bad/stream", &idx));
    TEST_ASSERT_EQUAL_INT(STATION_ERR_INVALID_URL, station_store_add(&s, "x", "", &idx));
    TEST_ASSERT_EQUAL_INT(STATION_ERR_INVALID_URL, station_store_add(&s, "x", "somafm.com/no-scheme", &idx));
    TEST_ASSERT_EQUAL_INT(0, s.count);
    TEST_ASSERT_FALSE(station_url_valid(NULL));
    TEST_ASSERT_TRUE(station_url_valid("HTTPS://X"));  /* scheme is case-insensitive */
}

void test_dedupe_exact_url(void)
{
    station_store_t s;
    station_store_init(&s);
    int idx;
    idx = -1;
    station_store_add(&s, "A", "http://x/s", &idx);
    TEST_ASSERT_EQUAL_INT(STATION_ERR_DUPLICATE, station_store_add(&s, "B", "http://x/s", &idx));  /* dup */
    TEST_ASSERT_EQUAL_INT(1, s.count);
    TEST_ASSERT_EQUAL_INT(0, station_store_find(&s, "http://x/s"));
    TEST_ASSERT_EQUAL_INT(-1, station_store_find(&s, "http://y/s"));
}

void test_full_store_rejects(void)
{
    station_store_t s;
    station_store_init(&s);
    char url[64];
    int idx;
    for (int i = 0; i < STATION_MAX; i++) {
        snprintf(url, sizeof(url), "http://host/s%d", i);
        idx = -1;
        station_result_t r = station_store_add(&s, NULL, url, &idx);
        TEST_ASSERT_EQUAL_INT(STATION_OK, r);
        TEST_ASSERT_EQUAL_INT(i, idx);
    }
    TEST_ASSERT_EQUAL_INT(STATION_MAX, s.count);
    idx = -1;
    station_result_t r = station_store_add(&s, "over", "http://host/over", &idx);
    TEST_ASSERT_EQUAL_INT(STATION_ERR_FULL, r);
}

void test_update(void)
{
    station_store_t s;
    station_store_init(&s);
    station_store_add(&s, "A", "http://a/s", NULL);
    station_store_add(&s, "B", "http://b/s", NULL);
    TEST_ASSERT_TRUE(station_store_update(&s, 0, "A2", "http://a2/s") == STATION_OK);
    TEST_ASSERT_EQUAL_STRING("A2", s.items[0].name);
    TEST_ASSERT_EQUAL_STRING("http://a2/s", s.items[0].url);
    /* editing to another entry's URL is rejected */
    TEST_ASSERT_TRUE(station_store_update(&s, 0, "A3", "http://b/s") == STATION_ERR_DUPLICATE);
    /* editing to the same URL (self) is fine */
    TEST_ASSERT_TRUE(station_store_update(&s, 0, "A4", "http://a2/s") == STATION_OK);
    /* bad index / bad url */
    TEST_ASSERT_TRUE(station_store_update(&s, 5, "x", "http://z/s") == STATION_ERR_NOT_FOUND);
    TEST_ASSERT_TRUE(station_store_update(&s, 0, "x", "nope") == STATION_ERR_INVALID_URL);
}

void test_remove_shifts(void)
{
    station_store_t s;
    station_store_init(&s);
    station_store_add(&s, "A", "http://a/s", NULL);
    station_store_add(&s, "B", "http://b/s", NULL);
    station_store_add(&s, "C", "http://c/s", NULL);
    TEST_ASSERT_TRUE(station_store_remove(&s, 1) == STATION_OK);
    TEST_ASSERT_EQUAL_INT(2, s.count);
    TEST_ASSERT_EQUAL_STRING("A", s.items[0].name);
    TEST_ASSERT_EQUAL_STRING("C", s.items[1].name);
    TEST_ASSERT_TRUE(station_store_remove(&s, 5) == STATION_ERR_NOT_FOUND);
}

void test_move_swaps_neighbours(void)
{
    station_store_t s;
    station_store_init(&s);
    station_store_add(&s, "A", "http://a/s", NULL);
    station_store_add(&s, "B", "http://b/s", NULL);
    station_store_add(&s, "C", "http://c/s", NULL);

    /* Record IDs */
    uint32_t id_a = s.items[0].id;
    uint32_t id_b = s.items[1].id;
    uint32_t id_c = s.items[2].id;

    /* Move B (idx 1) up -> B, A, C */
    TEST_ASSERT_TRUE(station_store_move(&s, 1, -1) == STATION_OK);
    TEST_ASSERT_EQUAL_STRING("B", s.items[0].name);
    TEST_ASSERT_EQUAL_UINT32(id_b, s.items[0].id);  /* B keeps its ID */
    TEST_ASSERT_EQUAL_STRING("A", s.items[1].name);
    TEST_ASSERT_EQUAL_UINT32(id_a, s.items[1].id);  /* A keeps its ID */
    TEST_ASSERT_EQUAL_STRING("C", s.items[2].name);
    TEST_ASSERT_EQUAL_UINT32(id_c, s.items[2].id);  /* C keeps its ID */

    /* Move A (now idx 1) down -> B, C, A */
    TEST_ASSERT_TRUE(station_store_move(&s, 1, 1) == STATION_OK);
    TEST_ASSERT_EQUAL_STRING("B", s.items[0].name);
    TEST_ASSERT_EQUAL_STRING("C", s.items[1].name);
    TEST_ASSERT_EQUAL_STRING("A", s.items[2].name);
    TEST_ASSERT_EQUAL_INT(3, s.count);
}

void test_move_edges_and_bad_args_rejected(void)
{
    station_store_t s;
    station_store_init(&s);
    station_store_add(&s, "A", "http://a/s", NULL);
    station_store_add(&s, "B", "http://b/s", NULL);

    TEST_ASSERT_TRUE(station_store_move(&s, 0, -1) == STATION_ERR_NOT_FOUND);  /* first up: no room */
    TEST_ASSERT_TRUE(station_store_move(&s, 1, 1) == STATION_ERR_NOT_FOUND);   /* last down: no room */
    TEST_ASSERT_TRUE(station_store_move(&s, 5, 1) == STATION_ERR_NOT_FOUND);   /* bad index */
    TEST_ASSERT_TRUE(station_store_move(&s, 0, 2) == STATION_ERR_INVALID_ARG);   /* bad delta */
    /* Order unchanged after all rejections. */
    TEST_ASSERT_EQUAL_STRING("A", s.items[0].name);
    TEST_ASSERT_EQUAL_STRING("B", s.items[1].name);
}

void test_find_by_id(void)
{
    station_store_t s;
    station_store_init(&s);
    station_store_add(&s, "A", "http://a/s", NULL);
    station_store_add(&s, "B", "http://b/s", NULL);
    station_store_add(&s, "C", "http://c/s", NULL);

    TEST_ASSERT_EQUAL_INT(0, station_store_index_by_id(&s, 1));
    TEST_ASSERT_EQUAL_INT(1, station_store_index_by_id(&s, 2));
    TEST_ASSERT_EQUAL_INT(2, station_store_index_by_id(&s, 3));
    TEST_ASSERT_EQUAL_INT(-1, station_store_index_by_id(&s, 0));
    TEST_ASSERT_EQUAL_INT(-1, station_store_index_by_id(&s, 99));
}

void test_name_too_long(void)
{
    station_store_t s;
    station_store_init(&s);
    char name[STATION_NAME_MAX + 1];
    memset(name, 'A', sizeof(name));
    name[STATION_NAME_MAX] = '\0';

    int idx;
    station_result_t r = station_store_add(&s, name, "http://x/s", &idx);
    TEST_ASSERT_EQUAL_INT(STATION_ERR_TOO_LONG, r);
}

void test_url_too_long(void)
{
    station_store_t s;
    station_store_init(&s);
    char url[STATION_URL_MAX + 1];
    memset(url, 'A', sizeof(url));
    url[STATION_URL_MAX] = '\0';

    int idx;
    station_result_t r = station_store_add(&s, "x", url, &idx);
    TEST_ASSERT_EQUAL_INT(STATION_ERR_TOO_LONG, r);
}

void test_validate_url_control_chars(void)
{
    /* URL with control character should be rejected */
    station_result_t r = station_validate_url("http://host/s\x01/stream");
    TEST_ASSERT_EQUAL_INT(STATION_ERR_INVALID_URL, r);

    /* Valid URL */
    r = station_validate_url("http://host/stream");
    TEST_ASSERT_EQUAL_INT(STATION_OK, r);

    /* Empty URL */
    r = station_validate_url("");
    TEST_ASSERT_EQUAL_INT(STATION_ERR_INVALID_URL, r);

    /* NULL URL */
    r = station_validate_url(NULL);
    TEST_ASSERT_EQUAL_INT(STATION_ERR_INVALID_ARG, r);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_add_and_get);
    RUN_TEST(test_stable_ids_increment);
    RUN_TEST(test_blank_name_defaults_to_host);
    RUN_TEST(test_reject_invalid_url);
    RUN_TEST(test_dedupe_exact_url);
    RUN_TEST(test_full_store_rejects);
    RUN_TEST(test_update);
    RUN_TEST(test_remove_shifts);
    RUN_TEST(test_move_swaps_neighbours);
    RUN_TEST(test_move_edges_and_bad_args_rejected);
    RUN_TEST(test_find_by_id);
    RUN_TEST(test_name_too_long);
    RUN_TEST(test_url_too_long);
    RUN_TEST(test_validate_url_control_chars);
    return UNITY_END();
}
