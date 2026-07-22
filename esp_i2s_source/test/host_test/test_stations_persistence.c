/*
 * test_stations_persistence — FIX3 Phase 5A: CRC-32, V2 blob validation,
 * and V1->V2 legacy migration. Pure logic (stations_persist_core.c),
 * host-testable directly — no NVS/ESP-IDF mocking needed.
 */
#include "unity.h"
#include "stations_persist_core.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ---- CRC-32 ---- */

void test_crc32_known_answer(void)
{
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, stations_crc32_ieee("123456789", 9));
}

void test_crc32_empty_input(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, stations_crc32_ieee("", 0));
}

void test_crc32_detects_one_byte_corruption(void)
{
    char buf[16] = "hello world!!!!";
    uint32_t original = stations_crc32_ieee(buf, sizeof(buf));
    buf[3] ^= 0x01;
    uint32_t corrupted = stations_crc32_ieee(buf, sizeof(buf));
    TEST_ASSERT_NOT_EQUAL(original, corrupted);
}

void test_crc32_detects_corruption_at_every_position(void)
{
    char buf[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint32_t original = stations_crc32_ieee(buf, sizeof(buf));
    for (size_t i = 0; i < sizeof(buf); i++) {
        char tmp[8];
        memcpy(tmp, buf, sizeof(buf));
        tmp[i] = (char)(tmp[i] ^ 0xFF);
        TEST_ASSERT_NOT_EQUAL_MESSAGE(original, stations_crc32_ieee(tmp, sizeof(tmp)),
                                      "corruption at this byte position went undetected");
    }
}

/* ---- stations_build_blob / stations_blob_validate ---- */

static void make_valid_store(station_store_t *s, int count)
{
    station_store_init(s);
    for (int i = 0; i < count; i++) {
        char name[STATION_NAME_MAX];
        char url[STATION_URL_MAX];
        snprintf(name, sizeof(name), "Station %d", i);
        snprintf(url, sizeof(url), "http://example.com/stream%d.mp3", i);
        int idx = -1;
        TEST_ASSERT_EQUAL(STATION_OK, station_store_add(s, name, url, &idx));
    }
}

void test_blob_roundtrip_valid(void)
{
    station_store_t store;
    make_valid_store(&store, 3);
    stations_blob_v2_t blob;
    stations_build_blob(&store, &blob);
    TEST_ASSERT_EQUAL(STATIONS_BLOB_OK, stations_blob_validate(&blob, sizeof(blob)));
}

void test_blob_rejects_wrong_size(void)
{
    station_store_t store;
    make_valid_store(&store, 1);
    stations_blob_v2_t blob;
    stations_build_blob(&store, &blob);
    TEST_ASSERT_EQUAL(STATIONS_BLOB_BAD_SIZE, stations_blob_validate(&blob, sizeof(blob) - 1));
}

void test_blob_rejects_bad_magic(void)
{
    station_store_t store;
    make_valid_store(&store, 1);
    stations_blob_v2_t blob;
    stations_build_blob(&store, &blob);
    blob.magic = 0xdeadbeef;
    TEST_ASSERT_EQUAL(STATIONS_BLOB_BAD_MAGIC, stations_blob_validate(&blob, sizeof(blob)));
}

void test_blob_rejects_bad_version(void)
{
    station_store_t store;
    make_valid_store(&store, 1);
    stations_blob_v2_t blob;
    stations_build_blob(&store, &blob);
    blob.version = 99;
    TEST_ASSERT_EQUAL(STATIONS_BLOB_BAD_VERSION, stations_blob_validate(&blob, sizeof(blob)));
}

void test_blob_rejects_bad_header_size(void)
{
    station_store_t store;
    make_valid_store(&store, 1);
    stations_blob_v2_t blob;
    stations_build_blob(&store, &blob);
    blob.header_size = 0;
    TEST_ASSERT_EQUAL(STATIONS_BLOB_BAD_HEADER_SIZE, stations_blob_validate(&blob, sizeof(blob)));
}

void test_blob_rejects_bad_payload_size(void)
{
    station_store_t store;
    make_valid_store(&store, 1);
    stations_blob_v2_t blob;
    stations_build_blob(&store, &blob);
    blob.payload_size = 4;
    TEST_ASSERT_EQUAL(STATIONS_BLOB_BAD_PAYLOAD_SIZE, stations_blob_validate(&blob, sizeof(blob)));
}

void test_blob_rejects_corrupted_payload_via_crc(void)
{
    station_store_t store;
    make_valid_store(&store, 2);
    stations_blob_v2_t blob;
    stations_build_blob(&store, &blob);
    blob.store.items[0].name[0] ^= 0x01;   /* corrupt payload after CRC computed */
    TEST_ASSERT_EQUAL(STATIONS_BLOB_BAD_CRC, stations_blob_validate(&blob, sizeof(blob)));
}

void test_blob_rejects_bad_count(void)
{
    station_store_t store;
    make_valid_store(&store, 1);
    stations_blob_v2_t blob;
    stations_build_blob(&store, &blob);
    blob.store.count = -1;
    blob.crc32 = stations_crc32_ieee(&blob.store, sizeof(blob.store));
    TEST_ASSERT_EQUAL(STATIONS_BLOB_BAD_COUNT, stations_blob_validate(&blob, sizeof(blob)));

    blob.store.count = STATION_MAX + 1;
    blob.crc32 = stations_crc32_ieee(&blob.store, sizeof(blob.store));
    TEST_ASSERT_EQUAL(STATIONS_BLOB_BAD_COUNT, stations_blob_validate(&blob, sizeof(blob)));
}

void test_blob_rejects_unterminated_name(void)
{
    station_store_t store;
    make_valid_store(&store, 1);
    stations_blob_v2_t blob;
    stations_build_blob(&store, &blob);
    memset(blob.store.items[0].name, 'A', sizeof(blob.store.items[0].name));
    blob.crc32 = stations_crc32_ieee(&blob.store, sizeof(blob.store));
    TEST_ASSERT_EQUAL(STATIONS_BLOB_BAD_STRING, stations_blob_validate(&blob, sizeof(blob)));
}

void test_blob_rejects_unterminated_url(void)
{
    station_store_t store;
    make_valid_store(&store, 1);
    stations_blob_v2_t blob;
    stations_build_blob(&store, &blob);
    memset(blob.store.items[0].url, 'A', sizeof(blob.store.items[0].url));
    blob.crc32 = stations_crc32_ieee(&blob.store, sizeof(blob.store));
    TEST_ASSERT_EQUAL(STATIONS_BLOB_BAD_STRING, stations_blob_validate(&blob, sizeof(blob)));
}

void test_blob_rejects_zero_id(void)
{
    station_store_t store;
    make_valid_store(&store, 1);
    stations_blob_v2_t blob;
    stations_build_blob(&store, &blob);
    blob.store.items[0].id = STATION_ID_NONE;
    blob.crc32 = stations_crc32_ieee(&blob.store, sizeof(blob.store));
    TEST_ASSERT_EQUAL(STATIONS_BLOB_BAD_ID, stations_blob_validate(&blob, sizeof(blob)));
}

void test_blob_rejects_duplicate_id(void)
{
    station_store_t store;
    make_valid_store(&store, 2);
    stations_blob_v2_t blob;
    stations_build_blob(&store, &blob);
    blob.store.items[1].id = blob.store.items[0].id;
    blob.crc32 = stations_crc32_ieee(&blob.store, sizeof(blob.store));
    TEST_ASSERT_EQUAL(STATIONS_BLOB_DUPLICATE_ID, stations_blob_validate(&blob, sizeof(blob)));
}

void test_blob_rejects_duplicate_url(void)
{
    station_store_t store;
    make_valid_store(&store, 2);
    stations_blob_v2_t blob;
    stations_build_blob(&store, &blob);
    strcpy(blob.store.items[1].url, blob.store.items[0].url);
    blob.crc32 = stations_crc32_ieee(&blob.store, sizeof(blob.store));
    TEST_ASSERT_EQUAL(STATIONS_BLOB_DUPLICATE_URL, stations_blob_validate(&blob, sizeof(blob)));
}

void test_blob_rejects_bad_url_syntax(void)
{
    station_store_t store;
    make_valid_store(&store, 1);
    stations_blob_v2_t blob;
    stations_build_blob(&store, &blob);
    strcpy(blob.store.items[0].url, "not-a-url");
    blob.crc32 = stations_crc32_ieee(&blob.store, sizeof(blob.store));
    TEST_ASSERT_EQUAL(STATIONS_BLOB_BAD_URL, stations_blob_validate(&blob, sizeof(blob)));
}

void test_blob_rejects_next_id_not_greater_than_max(void)
{
    station_store_t store;
    make_valid_store(&store, 2);
    stations_blob_v2_t blob;
    stations_build_blob(&store, &blob);
    blob.store.next_id = blob.store.items[1].id;   /* must be strictly greater */
    blob.next_id = blob.store.next_id;
    blob.crc32 = stations_crc32_ieee(&blob.store, sizeof(blob.store));
    TEST_ASSERT_EQUAL(STATIONS_BLOB_BAD_NEXT_ID, stations_blob_validate(&blob, sizeof(blob)));
}

void test_blob_rejects_header_next_id_store_mismatch(void)
{
    station_store_t store;
    make_valid_store(&store, 1);
    stations_blob_v2_t blob;
    stations_build_blob(&store, &blob);
    blob.next_id = blob.store.next_id + 5;   /* header/store disagree */
    TEST_ASSERT_EQUAL(STATIONS_BLOB_BAD_NEXT_ID, stations_blob_validate(&blob, sizeof(blob)));
}

/* ---- V1 -> V2 migration ---- */

static void make_v1_blob(stations_blob_v1_t *v1, int count)
{
    memset(v1, 0, sizeof(*v1));
    v1->magic = STATIONS_V1_MAGIC;
    v1->count = count;
    for (int i = 0; i < count; i++) {
        snprintf(v1->items[i].name, sizeof(v1->items[i].name), "Legacy %d", i);
        snprintf(v1->items[i].url, sizeof(v1->items[i].url),
                "http://example.com/legacy%d.mp3", i);
    }
}

void test_migrate_v1_valid(void)
{
    stations_blob_v1_t v1;
    make_v1_blob(&v1, 3);
    station_store_t out;
    TEST_ASSERT_TRUE(stations_migrate_v1(&v1, sizeof(v1), &out));
    TEST_ASSERT_EQUAL(3, out.count);
    TEST_ASSERT_EQUAL(4u, out.next_id);
}

void test_migrate_v1_index_zero_maps_to_stable_id_one(void)
{
    stations_blob_v1_t v1;
    make_v1_blob(&v1, 2);
    station_store_t out;
    TEST_ASSERT_TRUE(stations_migrate_v1(&v1, sizeof(v1), &out));
    TEST_ASSERT_EQUAL(1u, out.items[0].id);
    TEST_ASSERT_EQUAL(2u, out.items[1].id);
}

void test_migrate_v1_preserves_order_names_urls(void)
{
    stations_blob_v1_t v1;
    make_v1_blob(&v1, 2);
    station_store_t out;
    TEST_ASSERT_TRUE(stations_migrate_v1(&v1, sizeof(v1), &out));
    TEST_ASSERT_EQUAL_STRING("Legacy 0", out.items[0].name);
    TEST_ASSERT_EQUAL_STRING("http://example.com/legacy1.mp3", out.items[1].url);
}

void test_migrate_v1_rejects_bad_magic(void)
{
    stations_blob_v1_t v1;
    make_v1_blob(&v1, 1);
    v1.magic = 0;
    station_store_t out;
    TEST_ASSERT_FALSE(stations_migrate_v1(&v1, sizeof(v1), &out));
}

void test_migrate_v1_rejects_wrong_size(void)
{
    stations_blob_v1_t v1;
    make_v1_blob(&v1, 1);
    station_store_t out;
    TEST_ASSERT_FALSE(stations_migrate_v1(&v1, sizeof(v1) - 1, &out));
}

void test_migrate_v1_rejects_bad_count(void)
{
    stations_blob_v1_t v1;
    make_v1_blob(&v1, 1);
    v1.count = -1;
    station_store_t out;
    TEST_ASSERT_FALSE(stations_migrate_v1(&v1, sizeof(v1), &out));

    v1.count = STATION_MAX + 1;
    TEST_ASSERT_FALSE(stations_migrate_v1(&v1, sizeof(v1), &out));
}

void test_migrate_v1_rejects_unterminated_string(void)
{
    stations_blob_v1_t v1;
    make_v1_blob(&v1, 1);
    memset(v1.items[0].name, 'A', sizeof(v1.items[0].name));
    station_store_t out;
    TEST_ASSERT_FALSE(stations_migrate_v1(&v1, sizeof(v1), &out));
}

void test_migrate_v1_rejects_duplicate_url(void)
{
    stations_blob_v1_t v1;
    make_v1_blob(&v1, 2);
    strcpy(v1.items[1].url, v1.items[0].url);
    station_store_t out;
    TEST_ASSERT_FALSE(stations_migrate_v1(&v1, sizeof(v1), &out));
}

void test_migrate_v1_empty_store(void)
{
    stations_blob_v1_t v1;
    make_v1_blob(&v1, 0);
    station_store_t out;
    TEST_ASSERT_TRUE(stations_migrate_v1(&v1, sizeof(v1), &out));
    TEST_ASSERT_EQUAL(0, out.count);
    TEST_ASSERT_EQUAL(1u, out.next_id);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_crc32_known_answer);
    RUN_TEST(test_crc32_empty_input);
    RUN_TEST(test_crc32_detects_one_byte_corruption);
    RUN_TEST(test_crc32_detects_corruption_at_every_position);
    RUN_TEST(test_blob_roundtrip_valid);
    RUN_TEST(test_blob_rejects_wrong_size);
    RUN_TEST(test_blob_rejects_bad_magic);
    RUN_TEST(test_blob_rejects_bad_version);
    RUN_TEST(test_blob_rejects_bad_header_size);
    RUN_TEST(test_blob_rejects_bad_payload_size);
    RUN_TEST(test_blob_rejects_corrupted_payload_via_crc);
    RUN_TEST(test_blob_rejects_bad_count);
    RUN_TEST(test_blob_rejects_unterminated_name);
    RUN_TEST(test_blob_rejects_unterminated_url);
    RUN_TEST(test_blob_rejects_zero_id);
    RUN_TEST(test_blob_rejects_duplicate_id);
    RUN_TEST(test_blob_rejects_duplicate_url);
    RUN_TEST(test_blob_rejects_bad_url_syntax);
    RUN_TEST(test_blob_rejects_next_id_not_greater_than_max);
    RUN_TEST(test_blob_rejects_header_next_id_store_mismatch);
    RUN_TEST(test_migrate_v1_valid);
    RUN_TEST(test_migrate_v1_index_zero_maps_to_stable_id_one);
    RUN_TEST(test_migrate_v1_preserves_order_names_urls);
    RUN_TEST(test_migrate_v1_rejects_bad_magic);
    RUN_TEST(test_migrate_v1_rejects_wrong_size);
    RUN_TEST(test_migrate_v1_rejects_bad_count);
    RUN_TEST(test_migrate_v1_rejects_unterminated_string);
    RUN_TEST(test_migrate_v1_rejects_duplicate_url);
    RUN_TEST(test_migrate_v1_empty_store);
    return UNITY_END();
}
