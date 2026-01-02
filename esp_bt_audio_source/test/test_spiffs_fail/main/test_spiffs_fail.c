#include "unity.h"
#include "esp_spiffs.h"
#include "esp_vfs.h"
#include "esp_partition.h"
#include <stdio.h>
#include <sys/stat.h>

static void unmount_spiffs(const char *label) {
    if (label) {
        (void)esp_vfs_spiffs_unregister(label);
    }
}

static const esp_partition_t *find_spiffs_partition(const char *label) {
    return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, label);
}

static void erase_spiffs_partition(const char *label) {
    const esp_partition_t *part = find_spiffs_partition(label);
    TEST_ASSERT_NOT_NULL_MESSAGE(part, "SPIFFS partition not found");
    esp_err_t err = esp_partition_erase_range(part, 0, part->size);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, err, "Failed to erase SPIFFS partition");
}

static void test_mount_missing_partition_should_fail(void) {
    const char *label = "spiffs_missing";
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs_missing",
        .partition_label = label,
        .max_files = 4,
        .format_if_mount_failed = false,
    };

    esp_err_t err = esp_vfs_spiffs_register(&conf);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(ESP_OK, err, "Mount should fail when partition label is absent");
    unmount_spiffs(label);
}

static void test_mount_blank_partition_should_fail_without_format(void) {
    const char *label = "spiffs";
    erase_spiffs_partition(label);
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = label,
        .max_files = 4,
        .format_if_mount_failed = false,
    };

    esp_err_t err = esp_vfs_spiffs_register(&conf);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(ESP_OK, err, "Mount should fail on blank/corrupt image when format is disabled");
    unmount_spiffs(label);
}

static void test_mount_blank_partition_then_format_and_recover(void) {
    const char *label = "spiffs";
    const char *path = "/spiffs/recovery.txt";
    erase_spiffs_partition(label);

    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = label,
        .max_files = 4,
        .format_if_mount_failed = false,
    };

    esp_err_t err = esp_vfs_spiffs_register(&conf);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(ESP_OK, err, "Initial mount should fail on blank partition");
    unmount_spiffs(label);

    conf.format_if_mount_failed = true;
    err = esp_vfs_spiffs_register(&conf);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, err, "Mount should succeed when formatting is allowed");

    FILE *f = fopen(path, "w");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, "Should create file after successful mount");
    TEST_ASSERT_GREATER_OR_EQUAL_INT_MESSAGE(0, fputs("ok", f), "Write should succeed");
    fclose(f);

    struct stat st;
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, stat(path, &st), "File should stat after write");
    TEST_ASSERT_GREATER_THAN_INT_MESSAGE(0, st.st_size, "File size should be non-zero");

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, remove(path), "File cleanup should succeed");
    unmount_spiffs(label);
}

void setUp(void) {}
void tearDown(void) {}

int app_main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_mount_missing_partition_should_fail);
    RUN_TEST(test_mount_blank_partition_should_fail_without_format);
    RUN_TEST(test_mount_blank_partition_then_format_and_recover);
    return UNITY_END();
}
