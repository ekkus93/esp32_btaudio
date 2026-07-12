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
    int i = station_store_add(&s, "SomaFM", "http://somafm.com/groovesalad.pls");
    TEST_ASSERT_EQUAL_INT(0, i);
    TEST_ASSERT_EQUAL_INT(1, s.count);
    TEST_ASSERT_EQUAL_STRING("SomaFM", s.items[0].name);
    TEST_ASSERT_EQUAL_STRING("http://somafm.com/groovesalad.pls", s.items[0].url);
}

void test_blank_name_defaults_to_host(void)
{
    station_store_t s;
    station_store_init(&s);
    station_store_add(&s, "", "https://ice6.somafm.com:8080/dronezone");
    TEST_ASSERT_EQUAL_STRING("ice6.somafm.com", s.items[0].name);
    station_store_add(&s, NULL, "http://example.org/stream");
    TEST_ASSERT_EQUAL_STRING("example.org", s.items[1].name);
}

void test_reject_invalid_url(void)
{
    station_store_t s;
    station_store_init(&s);
    TEST_ASSERT_EQUAL_INT(-1, station_store_add(&s, "x", "ftp://bad/stream"));
    TEST_ASSERT_EQUAL_INT(-1, station_store_add(&s, "x", ""));
    TEST_ASSERT_EQUAL_INT(-1, station_store_add(&s, "x", "somafm.com/no-scheme"));
    TEST_ASSERT_EQUAL_INT(0, s.count);
    TEST_ASSERT_FALSE(station_url_valid(NULL));
    TEST_ASSERT_TRUE(station_url_valid("HTTPS://X"));  /* scheme is case-insensitive */
}

void test_dedupe_exact_url(void)
{
    station_store_t s;
    station_store_init(&s);
    TEST_ASSERT_EQUAL_INT(0, station_store_add(&s, "A", "http://x/s"));
    TEST_ASSERT_EQUAL_INT(-1, station_store_add(&s, "B", "http://x/s"));  /* dup */
    TEST_ASSERT_EQUAL_INT(1, s.count);
    TEST_ASSERT_EQUAL_INT(0, station_store_find(&s, "http://x/s"));
    TEST_ASSERT_EQUAL_INT(-1, station_store_find(&s, "http://y/s"));
}

void test_full_store_rejects(void)
{
    station_store_t s;
    station_store_init(&s);
    char url[64];
    for (int i = 0; i < STATION_MAX; i++) {
        snprintf(url, sizeof(url), "http://host/s%d", i);
        TEST_ASSERT_EQUAL_INT(i, station_store_add(&s, NULL, url));
    }
    TEST_ASSERT_EQUAL_INT(STATION_MAX, s.count);
    TEST_ASSERT_EQUAL_INT(-1, station_store_add(&s, "over", "http://host/over"));
}

void test_update(void)
{
    station_store_t s;
    station_store_init(&s);
    station_store_add(&s, "A", "http://a/s");
    station_store_add(&s, "B", "http://b/s");
    TEST_ASSERT_TRUE(station_store_update(&s, 0, "A2", "http://a2/s"));
    TEST_ASSERT_EQUAL_STRING("A2", s.items[0].name);
    TEST_ASSERT_EQUAL_STRING("http://a2/s", s.items[0].url);
    /* editing to another entry's URL is rejected */
    TEST_ASSERT_FALSE(station_store_update(&s, 0, "A3", "http://b/s"));
    /* editing to the same URL (self) is fine */
    TEST_ASSERT_TRUE(station_store_update(&s, 0, "A4", "http://a2/s"));
    /* bad index / bad url */
    TEST_ASSERT_FALSE(station_store_update(&s, 5, "x", "http://z/s"));
    TEST_ASSERT_FALSE(station_store_update(&s, 0, "x", "nope"));
}

void test_remove_shifts(void)
{
    station_store_t s;
    station_store_init(&s);
    station_store_add(&s, "A", "http://a/s");
    station_store_add(&s, "B", "http://b/s");
    station_store_add(&s, "C", "http://c/s");
    TEST_ASSERT_TRUE(station_store_remove(&s, 1));   /* remove B */
    TEST_ASSERT_EQUAL_INT(2, s.count);
    TEST_ASSERT_EQUAL_STRING("A", s.items[0].name);
    TEST_ASSERT_EQUAL_STRING("C", s.items[1].name);
    TEST_ASSERT_FALSE(station_store_remove(&s, 5));  /* bad index */
}

void test_move_swaps_neighbours(void)
{
    station_store_t s;
    station_store_init(&s);
    station_store_add(&s, "A", "http://a/s");
    station_store_add(&s, "B", "http://b/s");
    station_store_add(&s, "C", "http://c/s");

    /* Move B (idx 1) up -> B, A, C */
    TEST_ASSERT_TRUE(station_store_move(&s, 1, -1));
    TEST_ASSERT_EQUAL_STRING("B", s.items[0].name);
    TEST_ASSERT_EQUAL_STRING("A", s.items[1].name);
    TEST_ASSERT_EQUAL_STRING("C", s.items[2].name);

    /* Move A (now idx 1) down -> B, C, A */
    TEST_ASSERT_TRUE(station_store_move(&s, 1, 1));
    TEST_ASSERT_EQUAL_STRING("B", s.items[0].name);
    TEST_ASSERT_EQUAL_STRING("C", s.items[1].name);
    TEST_ASSERT_EQUAL_STRING("A", s.items[2].name);
    TEST_ASSERT_EQUAL_INT(3, s.count);
}

void test_move_edges_and_bad_args_rejected(void)
{
    station_store_t s;
    station_store_init(&s);
    station_store_add(&s, "A", "http://a/s");
    station_store_add(&s, "B", "http://b/s");

    TEST_ASSERT_FALSE(station_store_move(&s, 0, -1));  /* first up: no room */
    TEST_ASSERT_FALSE(station_store_move(&s, 1, 1));   /* last down: no room */
    TEST_ASSERT_FALSE(station_store_move(&s, 5, 1));   /* bad index */
    TEST_ASSERT_FALSE(station_store_move(&s, 0, 2));   /* bad delta */
    /* Order unchanged after all rejections. */
    TEST_ASSERT_EQUAL_STRING("A", s.items[0].name);
    TEST_ASSERT_EQUAL_STRING("B", s.items[1].name);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_add_and_get);
    RUN_TEST(test_blank_name_defaults_to_host);
    RUN_TEST(test_reject_invalid_url);
    RUN_TEST(test_dedupe_exact_url);
    RUN_TEST(test_full_store_rejects);
    RUN_TEST(test_update);
    RUN_TEST(test_remove_shifts);
    RUN_TEST(test_move_swaps_neighbours);
    RUN_TEST(test_move_edges_and_bad_args_rejected);
    return UNITY_END();
}
