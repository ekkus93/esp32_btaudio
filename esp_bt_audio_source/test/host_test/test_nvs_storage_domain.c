/* test_nvs_storage_domain.c — UT-4
 *
 * Exercises the nvs_storage domain layer against the REAL in-memory platform store.
 * Unlike test_nvs_storage_errors.c (which overrides the weak nvs_storage_* wrappers
 * for fault injection and so never touches the real store), this links the real
 * nvs_storage.c + platform_shim_host with the wrappers intact — covering the typed
 * accessors and, importantly, the paired-device list add/remove/shift logic that the
 * mock-backed suites skip (47.7% baseline).
 *
 * State is a process-global store; setUp erases it for isolation. */
#include "unity.h"
#include "nvs_storage.h"
#include "platform_storage.h"
#include <string.h>

void setUp(void)
{
    TEST_ASSERT_EQUAL_INT(ESP_OK, platform_storage_init());
    TEST_ASSERT_EQUAL_INT(ESP_OK, platform_storage_erase());
}

void tearDown(void)
{
    platform_storage_erase();
}

/* --- volume --- */

void test_volume_missing_then_roundtrip(void)
{
    uint8_t v = 0;
    TEST_ASSERT_EQUAL_INT(ESP_ERR_NOT_FOUND, nvs_storage_get_volume(&v));
    TEST_ASSERT_EQUAL_INT(ESP_OK, nvs_storage_set_volume(77));
    TEST_ASSERT_EQUAL_INT(ESP_OK, nvs_storage_get_volume(&v));
    TEST_ASSERT_EQUAL_UINT8(77, v);
}

void test_volume_null_arg(void)
{
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, nvs_storage_get_volume(NULL));
}

/* --- audio autostart --- */

void test_autostart_roundtrip_and_missing(void)
{
    uint8_t a = 9;
    /* Missing key returns the raw platform NOT_FOUND (not remapped like get_volume);
     * the contract is simply "non-OK → caller defaults to enabled". */
    TEST_ASSERT_NOT_EQUAL(ESP_OK, nvs_storage_get_audio_autostart(&a));
    TEST_ASSERT_EQUAL_INT(ESP_OK, nvs_storage_set_audio_autostart(1));
    TEST_ASSERT_EQUAL_INT(ESP_OK, nvs_storage_get_audio_autostart(&a));
    TEST_ASSERT_EQUAL_UINT8(1, a);
    TEST_ASSERT_EQUAL_INT(ESP_OK, nvs_storage_set_audio_autostart(0));
    TEST_ASSERT_EQUAL_INT(ESP_OK, nvs_storage_get_audio_autostart(&a));
    TEST_ASSERT_EQUAL_UINT8(0, a);
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, nvs_storage_get_audio_autostart(NULL));
}

/* --- i2s pins --- */

void test_i2s_pins_default_when_unset(void)
{
    int bclk = 5, ws = 5, din = 5, dout = 5;
    /* NOT_FOUND for each key is expected → all default to -1, overall ESP_OK */
    TEST_ASSERT_EQUAL_INT(ESP_OK, nvs_storage_get_i2s_pins(&bclk, &ws, &din, &dout));
    TEST_ASSERT_EQUAL_INT(-1, bclk);
    TEST_ASSERT_EQUAL_INT(-1, ws);
    TEST_ASSERT_EQUAL_INT(-1, din);
    TEST_ASSERT_EQUAL_INT(-1, dout);
}

void test_i2s_pins_roundtrip(void)
{
    TEST_ASSERT_EQUAL_INT(ESP_OK, nvs_storage_set_i2s_pins(26, 25, 22, 21));
    int bclk = 0, ws = 0, din = 0, dout = 0;
    TEST_ASSERT_EQUAL_INT(ESP_OK, nvs_storage_get_i2s_pins(&bclk, &ws, &din, &dout));
    TEST_ASSERT_EQUAL_INT(26, bclk);
    TEST_ASSERT_EQUAL_INT(25, ws);
    TEST_ASSERT_EQUAL_INT(22, din);
    TEST_ASSERT_EQUAL_INT(21, dout);
}

void test_i2s_pins_partial_null_pointers(void)
{
    TEST_ASSERT_EQUAL_INT(ESP_OK, nvs_storage_set_i2s_pins(1, 2, 3, 4));
    int din = 0;
    /* only ask for din; others NULL and must be skipped without crashing */
    TEST_ASSERT_EQUAL_INT(ESP_OK, nvs_storage_get_i2s_pins(NULL, NULL, &din, NULL));
    TEST_ASSERT_EQUAL_INT(3, din);
}

/* --- strings: device_name / default_pin / last_mac --- */

void test_device_name_roundtrip_and_args(void)
{
    char buf[64];
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, nvs_storage_get_device_name(NULL, sizeof(buf)));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, nvs_storage_get_device_name(buf, 0));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, nvs_storage_set_device_name(NULL));

    TEST_ASSERT_NOT_EQUAL(ESP_OK, nvs_storage_get_device_name(buf, sizeof(buf))); /* unset → not found */
    TEST_ASSERT_EQUAL_INT(ESP_OK, nvs_storage_set_device_name("Speaker-1"));
    TEST_ASSERT_EQUAL_INT(ESP_OK, nvs_storage_get_device_name(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("Speaker-1", buf);
}

void test_default_pin_roundtrip_and_args(void)
{
    char buf[16];
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, nvs_storage_get_default_pin(NULL, sizeof(buf)));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, nvs_storage_set_default_pin(NULL));
    TEST_ASSERT_EQUAL_INT(ESP_OK, nvs_storage_set_default_pin("4321"));
    TEST_ASSERT_EQUAL_INT(ESP_OK, nvs_storage_get_default_pin(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("4321", buf);
}

void test_last_mac_set_get_clear(void)
{
    char buf[24];
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, nvs_storage_set_last_connected_mac(NULL));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, nvs_storage_set_last_connected_mac(""));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, nvs_storage_get_last_connected_mac(NULL, sizeof(buf)));

    /* clear when absent → treated as success */
    TEST_ASSERT_EQUAL_INT(ESP_OK, nvs_storage_clear_last_connected_mac());

    TEST_ASSERT_EQUAL_INT(ESP_OK, nvs_storage_set_last_connected_mac("AA:BB:CC:DD:EE:FF"));
    TEST_ASSERT_EQUAL_INT(ESP_OK, nvs_storage_get_last_connected_mac(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FF", buf);

    TEST_ASSERT_EQUAL_INT(ESP_OK, nvs_storage_clear_last_connected_mac());
    TEST_ASSERT_NOT_EQUAL(ESP_OK, nvs_storage_get_last_connected_mac(buf, sizeof(buf)));
}

/* --- paired devices --- */

void test_paired_count_missing(void)
{
    int count = 99;
    TEST_ASSERT_EQUAL_INT(ESP_ERR_NOT_FOUND, nvs_storage_get_paired_count(&count));
    TEST_ASSERT_EQUAL_INT(0, count);
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, nvs_storage_get_paired_count(NULL));
}

void test_paired_add_and_get_by_index(void)
{
    TEST_ASSERT_EQUAL_INT(ESP_OK, nvs_storage_add_paired_device("AA:BB:CC:DD:EE:FF", "Phone"));
    int count = 0;
    TEST_ASSERT_EQUAL_INT(ESP_OK, nvs_storage_get_paired_count(&count));
    TEST_ASSERT_EQUAL_INT(1, count);

    char mac[24], name[64];
    TEST_ASSERT_EQUAL_INT(ESP_OK, nvs_storage_get_paired_device_by_index(0, mac, sizeof(mac), name, sizeof(name)));
    TEST_ASSERT_EQUAL_STRING("aa:bb:cc:dd:ee:ff", mac); /* format_mac_str emits lowercase */
    TEST_ASSERT_EQUAL_STRING("Phone", name);
}

void test_paired_add_without_name(void)
{
    TEST_ASSERT_EQUAL_INT(ESP_OK, nvs_storage_add_paired_device("11:22:33:44:55:66", NULL));
    char mac[24], name[64];
    TEST_ASSERT_EQUAL_INT(ESP_OK, nvs_storage_get_paired_device_by_index(0, mac, sizeof(mac), name, sizeof(name)));
    TEST_ASSERT_EQUAL_STRING("11:22:33:44:55:66", mac);
    TEST_ASSERT_EQUAL_STRING("", name); /* no name stored → empty */
}

void test_paired_add_duplicate_is_noop(void)
{
    TEST_ASSERT_EQUAL_INT(ESP_OK, nvs_storage_add_paired_device("AA:BB:CC:DD:EE:FF", "A"));
    /* same bytes, different case → binary duplicate, count unchanged */
    TEST_ASSERT_EQUAL_INT(ESP_OK, nvs_storage_add_paired_device("aa:bb:cc:dd:ee:ff", "A-again"));
    int count = 0;
    TEST_ASSERT_EQUAL_INT(ESP_OK, nvs_storage_get_paired_count(&count));
    TEST_ASSERT_EQUAL_INT(1, count);
}

void test_paired_add_invalid_mac(void)
{
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, nvs_storage_add_paired_device(NULL, "x"));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, nvs_storage_add_paired_device("not-a-mac", "x"));
}

void test_paired_remove_middle_compacts_list(void)
{
    TEST_ASSERT_EQUAL_INT(ESP_OK, nvs_storage_add_paired_device("11:11:11:11:11:11", "one"));
    TEST_ASSERT_EQUAL_INT(ESP_OK, nvs_storage_add_paired_device("22:22:22:22:22:22", "two"));
    TEST_ASSERT_EQUAL_INT(ESP_OK, nvs_storage_add_paired_device("33:33:33:33:33:33", "three"));

    /* remove the middle → list shifts down, count becomes 2 */
    TEST_ASSERT_EQUAL_INT(ESP_OK, nvs_storage_remove_paired_device("22:22:22:22:22:22"));
    int count = 0;
    TEST_ASSERT_EQUAL_INT(ESP_OK, nvs_storage_get_paired_count(&count));
    TEST_ASSERT_EQUAL_INT(2, count);

    char mac[24], name[64];
    TEST_ASSERT_EQUAL_INT(ESP_OK, nvs_storage_get_paired_device_by_index(0, mac, sizeof(mac), name, sizeof(name)));
    TEST_ASSERT_EQUAL_STRING("11:11:11:11:11:11", mac);
    TEST_ASSERT_EQUAL_STRING("one", name);
    /* index 1 is now the former index-2 entry (shifted down) */
    TEST_ASSERT_EQUAL_INT(ESP_OK, nvs_storage_get_paired_device_by_index(1, mac, sizeof(mac), name, sizeof(name)));
    TEST_ASSERT_EQUAL_STRING("33:33:33:33:33:33", mac);
    TEST_ASSERT_EQUAL_STRING("three", name);
}

void test_paired_remove_last_clears_count_key(void)
{
    TEST_ASSERT_EQUAL_INT(ESP_OK, nvs_storage_add_paired_device("44:44:44:44:44:44", "solo"));
    TEST_ASSERT_EQUAL_INT(ESP_OK, nvs_storage_remove_paired_device("44:44:44:44:44:44"));
    int count = 99;
    /* count key erased when it hits 0 → get_paired_count reports NOT_FOUND */
    TEST_ASSERT_EQUAL_INT(ESP_ERR_NOT_FOUND, nvs_storage_get_paired_count(&count));
    TEST_ASSERT_EQUAL_INT(0, count);
}

void test_paired_remove_absent_and_invalid(void)
{
    TEST_ASSERT_EQUAL_INT(ESP_ERR_NOT_FOUND, nvs_storage_remove_paired_device("55:55:55:55:55:55")); /* empty list */
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, nvs_storage_remove_paired_device(NULL));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, nvs_storage_remove_paired_device("bogus"));

    TEST_ASSERT_EQUAL_INT(ESP_OK, nvs_storage_add_paired_device("66:66:66:66:66:66", "x"));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_NOT_FOUND, nvs_storage_remove_paired_device("77:77:77:77:77:77")); /* present list, absent mac */
}

void test_paired_get_by_index_bounds_and_args(void)
{
    char mac[24], name[64];
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, nvs_storage_get_paired_device_by_index(-1, mac, sizeof(mac), name, sizeof(name)));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, nvs_storage_get_paired_device_by_index(0, NULL, sizeof(mac), name, sizeof(name)));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, nvs_storage_get_paired_device_by_index(0, mac, 0, name, sizeof(name)));

    TEST_ASSERT_EQUAL_INT(ESP_OK, nvs_storage_add_paired_device("88:88:88:88:88:88", "only"));
    /* index past end → NOT_FOUND (blob missing) */
    TEST_ASSERT_EQUAL_INT(ESP_ERR_NOT_FOUND, nvs_storage_get_paired_device_by_index(5, mac, sizeof(mac), name, sizeof(name)));
    /* NULL name is allowed (name optional) */
    TEST_ASSERT_EQUAL_INT(ESP_OK, nvs_storage_get_paired_device_by_index(0, mac, sizeof(mac), NULL, 0));
    TEST_ASSERT_EQUAL_STRING("88:88:88:88:88:88", mac);
}

void test_paired_clear_all(void)
{
    TEST_ASSERT_EQUAL_INT(ESP_OK, nvs_storage_add_paired_device("99:99:99:99:99:99", "a"));
    TEST_ASSERT_EQUAL_INT(ESP_OK, nvs_storage_add_paired_device("AB:AB:AB:AB:AB:AB", "b"));
    TEST_ASSERT_EQUAL_INT(ESP_OK, nvs_storage_clear_paired_devices());
    int count = 99;
    TEST_ASSERT_EQUAL_INT(ESP_ERR_NOT_FOUND, nvs_storage_get_paired_count(&count));
    TEST_ASSERT_EQUAL_INT(0, count);
    /* clear again on an already-empty store → still ESP_OK */
    TEST_ASSERT_EQUAL_INT(ESP_OK, nvs_storage_clear_paired_devices());
}

void test_paired_add_accepts_contiguous_hex(void)
{
    /* parse_mac_str also accepts 12-char contiguous hex */
    TEST_ASSERT_EQUAL_INT(ESP_OK, nvs_storage_add_paired_device("AABBCCDDEEFF", "contig"));
    int count = 0;
    TEST_ASSERT_EQUAL_INT(ESP_OK, nvs_storage_get_paired_count(&count));
    TEST_ASSERT_EQUAL_INT(1, count);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_volume_missing_then_roundtrip);
    RUN_TEST(test_volume_null_arg);
    RUN_TEST(test_autostart_roundtrip_and_missing);
    RUN_TEST(test_i2s_pins_default_when_unset);
    RUN_TEST(test_i2s_pins_roundtrip);
    RUN_TEST(test_i2s_pins_partial_null_pointers);
    RUN_TEST(test_device_name_roundtrip_and_args);
    RUN_TEST(test_default_pin_roundtrip_and_args);
    RUN_TEST(test_last_mac_set_get_clear);
    RUN_TEST(test_paired_count_missing);
    RUN_TEST(test_paired_add_and_get_by_index);
    RUN_TEST(test_paired_add_without_name);
    RUN_TEST(test_paired_add_duplicate_is_noop);
    RUN_TEST(test_paired_add_invalid_mac);
    RUN_TEST(test_paired_remove_middle_compacts_list);
    RUN_TEST(test_paired_remove_last_clears_count_key);
    RUN_TEST(test_paired_remove_absent_and_invalid);
    RUN_TEST(test_paired_get_by_index_bounds_and_args);
    RUN_TEST(test_paired_clear_all);
    RUN_TEST(test_paired_add_accepts_contiguous_hex);
    return UNITY_END();
}
