/* Host tests for nvs_storage mock and API behavior */
#include "unity.h"
#include "nvs_storage.h"
#include <string.h>

void setUp(void) {
    // clear mock storage before each test
    nvs_storage_clear_paired_devices();
}

void tearDown(void) {
}

void test_add_and_get_paired_device(void) {
    TEST_ASSERT_EQUAL(ESP_OK, nvs_storage_add_paired_device("aa:bb:cc:11:22:33", "Speaker"));
    int count = 0;
    TEST_ASSERT_EQUAL(ESP_OK, nvs_storage_get_paired_count(&count));
    TEST_ASSERT_EQUAL(1, count);
    char mac[32];
    char name[32];
    TEST_ASSERT_EQUAL(ESP_OK, nvs_storage_get_paired_device_by_index(0, mac, sizeof(mac), name, sizeof(name)));
    TEST_ASSERT_EQUAL_STRING("aa:bb:cc:11:22:33", mac);
    TEST_ASSERT_EQUAL_STRING("Speaker", name);
}

void test_duplicate_add_is_ignored(void) {
    TEST_ASSERT_EQUAL(ESP_OK, nvs_storage_add_paired_device("AA:BB:CC:11:22:33", "Speaker1"));
    TEST_ASSERT_EQUAL(ESP_OK, nvs_storage_add_paired_device("aa:bb:cc:11:22:33", "Speaker2"));
    int count = 0;
    TEST_ASSERT_EQUAL(ESP_OK, nvs_storage_get_paired_count(&count));
    TEST_ASSERT_EQUAL(1, count);
    char name[32];
    TEST_ASSERT_EQUAL(ESP_OK, nvs_storage_get_paired_device_by_index(0, (char[32]){0}, 32, name, sizeof(name)));
}

void test_remove_paired_device(void) {
    TEST_ASSERT_EQUAL(ESP_OK, nvs_storage_add_paired_device("11:22:33:44:55:66", "Phone"));
    TEST_ASSERT_EQUAL(ESP_OK, nvs_storage_add_paired_device("aa:bb:cc:11:22:33", "Speaker"));
    int count = 0;
    TEST_ASSERT_EQUAL(ESP_OK, nvs_storage_get_paired_count(&count));
    TEST_ASSERT_EQUAL(2, count);
    TEST_ASSERT_EQUAL(ESP_OK, nvs_storage_remove_paired_device("11:22:33:44:55:66"));
    TEST_ASSERT_EQUAL(ESP_OK, nvs_storage_get_paired_count(&count));
    TEST_ASSERT_EQUAL(1, count);
    char mac[32];
    TEST_ASSERT_EQUAL(ESP_OK, nvs_storage_get_paired_device_by_index(0, mac, sizeof(mac), NULL, 0));
    TEST_ASSERT_EQUAL_STRING("aa:bb:cc:11:22:33", mac);
}

void test_clear_paired_devices(void) {
    TEST_ASSERT_EQUAL(ESP_OK, nvs_storage_add_paired_device("11:22:33:44:55:66", "Phone"));
    TEST_ASSERT_EQUAL(ESP_OK, nvs_storage_clear_paired_devices());
    int count = 0;
    TEST_ASSERT_EQUAL(ESP_OK, nvs_storage_get_paired_count(&count));
    TEST_ASSERT_EQUAL(0, count);
}

void test_fill_to_capacity_and_overflow(void) {
    // Fill up to the mock capacity (MOCK_MAX_PAIRED == 20 in the mock)
    char mac[32];
    char name[16];
    for (int i = 0; i < 20; ++i) {
        snprintf(mac, sizeof(mac), "00:11:22:33:44:%02x", i);
        snprintf(name, sizeof(name), "D%02d", i);
        TEST_ASSERT_EQUAL(ESP_OK, nvs_storage_add_paired_device(mac, name));
    }
    int count = 0;
    TEST_ASSERT_EQUAL(ESP_OK, nvs_storage_get_paired_count(&count));
    TEST_ASSERT_EQUAL(20, count);
    // 21st should fail with no memory
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, nvs_storage_add_paired_device("00:11:22:33:44:ff", "Overflow"));
}

void test_invalid_mac_is_rejected(void) {
    // Missing octets
    TEST_ASSERT_NOT_EQUAL(ESP_OK, nvs_storage_add_paired_device("00:11:22:33:44", "Bad"));
    // Non-hex chars
    TEST_ASSERT_NOT_EQUAL(ESP_OK, nvs_storage_add_paired_device("GG:HH:II:JJ:KK:LL", "BadHex"));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_add_and_get_paired_device);
    RUN_TEST(test_duplicate_add_is_ignored);
    RUN_TEST(test_remove_paired_device);
    RUN_TEST(test_clear_paired_devices);
    RUN_TEST(test_fill_to_capacity_and_overflow);
    RUN_TEST(test_invalid_mac_is_rejected);
    return UNITY_END();
}
