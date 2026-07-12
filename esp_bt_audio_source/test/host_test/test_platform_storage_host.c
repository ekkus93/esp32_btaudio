/* test_platform_storage_host.c — UT-1
 *
 * Exercises the REAL host in-memory key/value store
 * (components/platform_shim/platform_storage_host.c), which the mock-backed NVS
 * suites shadow and therefore never execute (0% baseline coverage).
 *
 * Links platform_shim_host (the real object), not a mock. State is a process-global
 * namespace list, so setUp/tearDown erase it to isolate cases and stay leak-clean. */
#include "unity.h"
#include "platform_storage.h"
#include <string.h>

static platform_storage_handle_t open_ns(const char *name)
{
    platform_storage_handle_t h = 0;
    TEST_ASSERT_EQUAL_INT(ESP_OK, platform_storage_open(name, PLATFORM_STORAGE_READWRITE, &h));
    TEST_ASSERT_NOT_EQUAL(0, h);
    return h;
}

void setUp(void)
{
    TEST_ASSERT_EQUAL_INT(ESP_OK, platform_storage_init());
    TEST_ASSERT_EQUAL_INT(ESP_OK, platform_storage_erase()); /* start clean */
}

void tearDown(void)
{
    platform_storage_erase(); /* free everything → valgrind-clean */
}

/* --- lifecycle / namespaces --- */

void test_init_and_commit_are_ok(void)
{
    platform_storage_handle_t h = open_ns("ns_init");
    TEST_ASSERT_EQUAL_INT(ESP_OK, platform_storage_commit(h));
    TEST_ASSERT_EQUAL_INT(ESP_OK, platform_storage_close(h));
}

void test_open_reopen_returns_same_namespace(void)
{
    platform_storage_handle_t a = open_ns("dup");
    platform_storage_handle_t b = open_ns("dup"); /* find_namespace hit */
    TEST_ASSERT_EQUAL(a, b);
}

void test_open_readonly_mode_also_creates(void)
{
    platform_storage_handle_t h = 0;
    TEST_ASSERT_EQUAL_INT(ESP_OK, platform_storage_open("ro", PLATFORM_STORAGE_READONLY, &h));
    TEST_ASSERT_NOT_EQUAL(0, h);
}

void test_namespaces_are_independent(void)
{
    platform_storage_handle_t a = open_ns("nsA");
    platform_storage_handle_t b = open_ns("nsB");
    TEST_ASSERT_NOT_EQUAL(a, b);

    TEST_ASSERT_EQUAL_INT(ESP_OK, platform_storage_set_i32(a, "k", 111));
    TEST_ASSERT_EQUAL_INT(ESP_OK, platform_storage_set_i32(b, "k", 222));

    int32_t va = 0, vb = 0;
    TEST_ASSERT_EQUAL_INT(ESP_OK, platform_storage_get_i32(a, "k", &va));
    TEST_ASSERT_EQUAL_INT(ESP_OK, platform_storage_get_i32(b, "k", &vb));
    TEST_ASSERT_EQUAL_INT32(111, va);
    TEST_ASSERT_EQUAL_INT32(222, vb);
}

void test_find_namespace_walks_list(void)
{
    /* Create three; reopen the first-created (list tail) to force a walk. */
    platform_storage_handle_t first = open_ns("first");
    open_ns("second");
    open_ns("third");
    platform_storage_handle_t again = open_ns("first");
    TEST_ASSERT_EQUAL(first, again);
}

/* --- i32 --- */

void test_i32_roundtrip_and_missing(void)
{
    platform_storage_handle_t h = open_ns("i32");
    int32_t v = 0;
    TEST_ASSERT_EQUAL_INT(PLATFORM_ERR_STORAGE_NOT_FOUND, platform_storage_get_i32(h, "missing", &v));

    TEST_ASSERT_EQUAL_INT(ESP_OK, platform_storage_set_i32(h, "a", 42));
    TEST_ASSERT_EQUAL_INT(ESP_OK, platform_storage_get_i32(h, "a", &v));
    TEST_ASSERT_EQUAL_INT32(42, v);
}

void test_i32_overwrite_and_boundaries(void)
{
    platform_storage_handle_t h = open_ns("i32b");
    int32_t v = 0;
    TEST_ASSERT_EQUAL_INT(ESP_OK, platform_storage_set_i32(h, "k", 7));
    TEST_ASSERT_EQUAL_INT(ESP_OK, platform_storage_set_i32(h, "k", -9)); /* overwrite, no free */
    TEST_ASSERT_EQUAL_INT(ESP_OK, platform_storage_get_i32(h, "k", &v));
    TEST_ASSERT_EQUAL_INT32(-9, v);

    TEST_ASSERT_EQUAL_INT(ESP_OK, platform_storage_set_i32(h, "min", INT32_MIN));
    TEST_ASSERT_EQUAL_INT(ESP_OK, platform_storage_set_i32(h, "max", INT32_MAX));
    TEST_ASSERT_EQUAL_INT(ESP_OK, platform_storage_get_i32(h, "min", &v));
    TEST_ASSERT_EQUAL_INT32(INT32_MIN, v);
    TEST_ASSERT_EQUAL_INT(ESP_OK, platform_storage_get_i32(h, "max", &v));
    TEST_ASSERT_EQUAL_INT32(INT32_MAX, v);
}

/* --- str --- */

void test_str_roundtrip_query_and_overwrite(void)
{
    platform_storage_handle_t h = open_ns("str");

    TEST_ASSERT_EQUAL_INT(ESP_OK, platform_storage_set_str(h, "name", "hello"));

    /* query-length mode (NULL out buffer) */
    size_t len = 0;
    TEST_ASSERT_EQUAL_INT(ESP_OK, platform_storage_get_str(h, "name", NULL, &len));
    TEST_ASSERT_EQUAL_size_t(6, len); /* "hello" + NUL */

    char buf[16];
    len = sizeof(buf);
    TEST_ASSERT_EQUAL_INT(ESP_OK, platform_storage_get_str(h, "name", buf, &len));
    TEST_ASSERT_EQUAL_STRING("hello", buf);
    TEST_ASSERT_EQUAL_size_t(6, len);

    /* overwrite shorter -> longer (frees old str_val) */
    TEST_ASSERT_EQUAL_INT(ESP_OK, platform_storage_set_str(h, "name", "a-much-longer-value"));
    len = sizeof(buf) * 4;
    char big[64];
    len = sizeof(big);
    TEST_ASSERT_EQUAL_INT(ESP_OK, platform_storage_get_str(h, "name", big, &len));
    TEST_ASSERT_EQUAL_STRING("a-much-longer-value", big);
}

void test_str_empty_value(void)
{
    platform_storage_handle_t h = open_ns("stre");
    TEST_ASSERT_EQUAL_INT(ESP_OK, platform_storage_set_str(h, "e", ""));
    char buf[8];
    size_t len = sizeof(buf);
    TEST_ASSERT_EQUAL_INT(ESP_OK, platform_storage_get_str(h, "e", buf, &len));
    TEST_ASSERT_EQUAL_STRING("", buf);
    TEST_ASSERT_EQUAL_size_t(1, len);
}

void test_str_buffer_too_small_reports_required_length(void)
{
    platform_storage_handle_t h = open_ns("strs");
    TEST_ASSERT_EQUAL_INT(ESP_OK, platform_storage_set_str(h, "k", "abcdef"));
    char buf[3];
    size_t len = sizeof(buf); /* 3 < 7 required */
    TEST_ASSERT_EQUAL_INT(PLATFORM_ERR_STORAGE_INVALID_LENGTH,
                          platform_storage_get_str(h, "k", buf, &len));
    TEST_ASSERT_EQUAL_size_t(7, len); /* required length reported back */
}

void test_str_missing_key(void)
{
    platform_storage_handle_t h = open_ns("strm");
    char buf[8];
    size_t len = sizeof(buf);
    TEST_ASSERT_EQUAL_INT(PLATFORM_ERR_STORAGE_NOT_FOUND,
                          platform_storage_get_str(h, "nope", buf, &len));
}

/* --- blob --- */

void test_blob_roundtrip_query_and_zeros(void)
{
    platform_storage_handle_t h = open_ns("blob");
    const uint8_t in[5] = {0x00, 0xFF, 0x00, 0x7F, 0x80};

    TEST_ASSERT_EQUAL_INT(ESP_OK, platform_storage_set_blob(h, "b", in, sizeof(in)));

    size_t len = 0;
    TEST_ASSERT_EQUAL_INT(ESP_OK, platform_storage_get_blob(h, "b", NULL, &len));
    TEST_ASSERT_EQUAL_size_t(sizeof(in), len);

    uint8_t out[5];
    memset(out, 0xAA, sizeof(out));
    len = sizeof(out);
    TEST_ASSERT_EQUAL_INT(ESP_OK, platform_storage_get_blob(h, "b", out, &len));
    TEST_ASSERT_EQUAL_size_t(sizeof(in), len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(in, out, sizeof(in));
}

void test_blob_overwrite_frees_old(void)
{
    platform_storage_handle_t h = open_ns("blob2");
    const uint8_t a[3] = {1, 2, 3};
    const uint8_t b[6] = {9, 8, 7, 6, 5, 4};
    TEST_ASSERT_EQUAL_INT(ESP_OK, platform_storage_set_blob(h, "k", a, sizeof(a)));
    TEST_ASSERT_EQUAL_INT(ESP_OK, platform_storage_set_blob(h, "k", b, sizeof(b))); /* frees a */

    uint8_t out[6];
    size_t len = sizeof(out);
    TEST_ASSERT_EQUAL_INT(ESP_OK, platform_storage_get_blob(h, "k", out, &len));
    TEST_ASSERT_EQUAL_size_t(sizeof(b), len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(b, out, sizeof(b));
}

void test_blob_buffer_too_small(void)
{
    platform_storage_handle_t h = open_ns("blob3");
    const uint8_t in[4] = {1, 2, 3, 4};
    TEST_ASSERT_EQUAL_INT(ESP_OK, platform_storage_set_blob(h, "k", in, sizeof(in)));

    uint8_t out[2];
    size_t len = sizeof(out); /* 2 < 4 */
    TEST_ASSERT_EQUAL_INT(PLATFORM_ERR_STORAGE_INVALID_LENGTH,
                          platform_storage_get_blob(h, "k", out, &len));
    TEST_ASSERT_EQUAL_size_t(4, len);
}

void test_blob_missing_key(void)
{
    platform_storage_handle_t h = open_ns("blob4");
    uint8_t out[4];
    size_t len = sizeof(out);
    TEST_ASSERT_EQUAL_INT(PLATFORM_ERR_STORAGE_NOT_FOUND,
                          platform_storage_get_blob(h, "nope", out, &len));
}

/* --- type mismatch --- */

void test_type_mismatch_reads_report_not_found(void)
{
    platform_storage_handle_t h = open_ns("mix");
    TEST_ASSERT_EQUAL_INT(ESP_OK, platform_storage_set_i32(h, "k", 5));

    char sbuf[8];
    size_t slen = sizeof(sbuf);
    int32_t iv = 0;
    uint8_t bbuf[8];
    size_t blen = sizeof(bbuf);

    /* key exists but as i32 → str/blob reads must not alias it */
    TEST_ASSERT_EQUAL_INT(PLATFORM_ERR_STORAGE_NOT_FOUND, platform_storage_get_str(h, "k", sbuf, &slen));
    TEST_ASSERT_EQUAL_INT(PLATFORM_ERR_STORAGE_NOT_FOUND, platform_storage_get_blob(h, "k", bbuf, &blen));

    /* changing type in place: i32 → str (create_or_get_entry reuses the entry) */
    TEST_ASSERT_EQUAL_INT(ESP_OK, platform_storage_set_str(h, "k", "now-a-string"));
    TEST_ASSERT_EQUAL_INT(PLATFORM_ERR_STORAGE_NOT_FOUND, platform_storage_get_i32(h, "k", &iv));
}

/* --- erase_key --- */

void test_erase_key_head_middle_and_absent(void)
{
    platform_storage_handle_t h = open_ns("er");
    /* entries are prepended, so insertion order k1,k2,k3 → list head is k3 */
    TEST_ASSERT_EQUAL_INT(ESP_OK, platform_storage_set_i32(h, "k1", 1));
    TEST_ASSERT_EQUAL_INT(ESP_OK, platform_storage_set_i32(h, "k2", 2));
    TEST_ASSERT_EQUAL_INT(ESP_OK, platform_storage_set_i32(h, "k3", 3));

    /* erase head (k3): the `prev == NULL` branch */
    TEST_ASSERT_EQUAL_INT(ESP_OK, platform_storage_erase_key(h, "k3"));
    /* erase middle (k1 is now tail; k2 middle): the `prev != NULL` branch */
    TEST_ASSERT_EQUAL_INT(ESP_OK, platform_storage_erase_key(h, "k1"));

    int32_t v = 0;
    TEST_ASSERT_EQUAL_INT(PLATFORM_ERR_STORAGE_NOT_FOUND, platform_storage_get_i32(h, "k3", &v));
    TEST_ASSERT_EQUAL_INT(PLATFORM_ERR_STORAGE_NOT_FOUND, platform_storage_get_i32(h, "k1", &v));
    TEST_ASSERT_EQUAL_INT(ESP_OK, platform_storage_get_i32(h, "k2", &v));
    TEST_ASSERT_EQUAL_INT32(2, v);

    /* absent key */
    TEST_ASSERT_EQUAL_INT(PLATFORM_ERR_STORAGE_NOT_FOUND, platform_storage_erase_key(h, "ghost"));
}

void test_erase_key_frees_str_and_blob_types(void)
{
    platform_storage_handle_t h = open_ns("erx");
    TEST_ASSERT_EQUAL_INT(ESP_OK, platform_storage_set_str(h, "s", "value"));
    TEST_ASSERT_EQUAL_INT(ESP_OK, platform_storage_set_blob(h, "b", "\x01\x02", 2));
    TEST_ASSERT_EQUAL_INT(ESP_OK, platform_storage_erase_key(h, "s")); /* frees str_val */
    TEST_ASSERT_EQUAL_INT(ESP_OK, platform_storage_erase_key(h, "b")); /* frees blob data */
}

/* --- whole-store erase --- */

void test_erase_clears_all_then_missing(void)
{
    platform_storage_handle_t h = open_ns("wipe");
    TEST_ASSERT_EQUAL_INT(ESP_OK, platform_storage_set_i32(h, "k", 1));
    TEST_ASSERT_EQUAL_INT(ESP_OK, platform_storage_erase());

    /* re-open (fresh namespace) and confirm the key is gone; do NOT reuse old handle */
    platform_storage_handle_t h2 = open_ns("wipe");
    int32_t v = 0;
    TEST_ASSERT_EQUAL_INT(PLATFORM_ERR_STORAGE_NOT_FOUND, platform_storage_get_i32(h2, "k", &v));
}

/* --- NULL handle rejection (if(!ns) branch in every accessor) --- */

void test_null_handle_rejected(void)
{
    platform_storage_handle_t bad = 0;
    int32_t v = 0;
    char sbuf[4];
    size_t slen = sizeof(sbuf);
    uint8_t bbuf[4];
    size_t blen = sizeof(bbuf);

    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, platform_storage_get_i32(bad, "k", &v));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, platform_storage_set_i32(bad, "k", 1));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, platform_storage_get_str(bad, "k", sbuf, &slen));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, platform_storage_set_str(bad, "k", "x"));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, platform_storage_get_blob(bad, "k", bbuf, &blen));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, platform_storage_set_blob(bad, "k", bbuf, blen));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, platform_storage_erase_key(bad, "k"));
}

/* --- growth (many entries) — leak-clean under valgrind --- */

void test_many_entries_grow_and_erase(void)
{
    platform_storage_handle_t h = open_ns("grow");
    char key[16];
    for (int i = 0; i < 64; ++i) {
        snprintf(key, sizeof(key), "key%d", i);
        TEST_ASSERT_EQUAL_INT(ESP_OK, platform_storage_set_i32(h, key, i));
    }
    int32_t v = 0;
    TEST_ASSERT_EQUAL_INT(ESP_OK, platform_storage_get_i32(h, "key63", &v));
    TEST_ASSERT_EQUAL_INT32(63, v);
    /* tearDown's erase frees the whole list */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_and_commit_are_ok);
    RUN_TEST(test_open_reopen_returns_same_namespace);
    RUN_TEST(test_open_readonly_mode_also_creates);
    RUN_TEST(test_namespaces_are_independent);
    RUN_TEST(test_find_namespace_walks_list);
    RUN_TEST(test_i32_roundtrip_and_missing);
    RUN_TEST(test_i32_overwrite_and_boundaries);
    RUN_TEST(test_str_roundtrip_query_and_overwrite);
    RUN_TEST(test_str_empty_value);
    RUN_TEST(test_str_buffer_too_small_reports_required_length);
    RUN_TEST(test_str_missing_key);
    RUN_TEST(test_blob_roundtrip_query_and_zeros);
    RUN_TEST(test_blob_overwrite_frees_old);
    RUN_TEST(test_blob_buffer_too_small);
    RUN_TEST(test_blob_missing_key);
    RUN_TEST(test_type_mismatch_reads_report_not_found);
    RUN_TEST(test_erase_key_head_middle_and_absent);
    RUN_TEST(test_erase_key_frees_str_and_blob_types);
    RUN_TEST(test_erase_clears_all_then_missing);
    RUN_TEST(test_null_handle_rejected);
    RUN_TEST(test_many_entries_grow_and_erase);
    return UNITY_END();
}
