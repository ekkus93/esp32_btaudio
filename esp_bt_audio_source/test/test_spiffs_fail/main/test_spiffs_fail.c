#include "unity.h"
#include "esp_spiffs.h"
#include "esp_vfs.h"
#include "esp_partition.h"
#include <stdio.h>
#include <sys/stat.h>
#include <errno.h>

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

static void test_mount_failure_leaves_vfs_unregistered(void) {
    const char *label = "spiffs";
    erase_spiffs_partition(label);

    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = label,
        .max_files = 4,
        .format_if_mount_failed = false,
    };

    esp_err_t err = esp_vfs_spiffs_register(&conf);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(ESP_OK, err, "Mount should fail on blank image without format");

    struct stat st;
    errno = 0;
    int rc = stat("/spiffs/should_not_exist", &st);
    TEST_ASSERT_EQUAL_INT_MESSAGE(-1, rc, "stat should fail when VFS is not mounted");
    TEST_ASSERT_EQUAL_INT_MESSAGE(ENOENT, errno, "stat error should be ENOENT after failed mount");

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

static void test_mount_with_format_reports_sizes(void) {
    const char *label = "spiffs";
    erase_spiffs_partition(label);

    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = label,
        .max_files = 4,
        .format_if_mount_failed = true,
    };

    esp_err_t err = esp_vfs_spiffs_register(&conf);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, err, "Mount should succeed with format");

    size_t total = 0;
    size_t used = 0;
    err = esp_spiffs_info(label, &total, &used);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, err, "esp_spiffs_info should succeed after mount");
    TEST_ASSERT_GREATER_THAN_size_t_MESSAGE(0, total, "Total size should be non-zero");
    TEST_ASSERT_GREATER_OR_EQUAL_size_t_MESSAGE(0, used, "Used size should be reported");

    unmount_spiffs(label);
}

static void test_remount_preserves_file_after_format(void) {
    const char *label = "spiffs";
    const char *path = "/spiffs/keep.txt";
    erase_spiffs_partition(label);

    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = label,
        .max_files = 4,
        .format_if_mount_failed = true,
    };

    esp_err_t err = esp_vfs_spiffs_register(&conf);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, err, "Initial mount should format and succeed");

    FILE *f = fopen(path, "w");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, "File should open for write after format");
    TEST_ASSERT_GREATER_OR_EQUAL_INT_MESSAGE(0, fputs("persist", f), "Write should succeed");
    fclose(f);
    unmount_spiffs(label);

    conf.format_if_mount_failed = false;
    err = esp_vfs_spiffs_register(&conf);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, err, "Remount should succeed without reformat");

    struct stat st;
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, stat(path, &st), "File should persist across remount");
    TEST_ASSERT_GREATER_THAN_INT_MESSAGE(0, st.st_size, "Persisted file should have content");

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, remove(path), "Cleanup should succeed");
    unmount_spiffs(label);
}

void setUp(void) {}
void tearDown(void) {}

int app_main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_mount_missing_partition_should_fail);
    RUN_TEST(test_mount_blank_partition_should_fail_without_format);
    RUN_TEST(test_mount_failure_leaves_vfs_unregistered);
    RUN_TEST(test_mount_blank_partition_then_format_and_recover);
    RUN_TEST(test_mount_with_format_reports_sizes);
    RUN_TEST(test_remount_preserves_file_after_format);
    return UNITY_END();
}
