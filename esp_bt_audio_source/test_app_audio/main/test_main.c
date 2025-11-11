#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "unity.h"
#include "test_app_main.h"
#include "esp_spiffs.h"
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include "esp_partition.h"

static const char *TAG = "TEST_MAIN_AUDIO";
extern void app_test_main(void);

static void run_audio_tests(void)
{
    ESP_LOGI(TAG, "========== STARTING AUDIO TESTS ==========");
    printf("\n\n----- UNITY TEST START -----\n");
    UNITY_BEGIN();
    app_test_main();
    int result = UNITY_END();
    printf("--- SUMMARY ---\n");
    printf("----- UNITY TEST COMPLETE: %s -----\n", result ? "FAIL" : "PASS");
    printf("-------- AUDIO TEST SUMMARY --------\n");
    printf("Tests run    : %d\n", Unity.NumberOfTests);
    printf("Tests passed : %d\n", Unity.NumberOfTests - Unity.TestFailures);
    printf("Tests failed : %d\n", Unity.TestFailures);
    printf("--------------------------------------\n");
    ESP_LOGI(TAG, "-------- AUDIO TEST SUMMARY --------");
    ESP_LOGI(TAG, "Tests run     : %d", Unity.NumberOfTests);
    ESP_LOGI(TAG, "Tests passed  : %d", Unity.NumberOfTests - Unity.TestFailures);
    ESP_LOGI(TAG, "Tests failed  : %d", Unity.TestFailures);
    ESP_LOGI(TAG, "--------------------------------------");
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting audio-focused Unity test suite");
    esp_log_level_set("AUDIO_PROC", ESP_LOG_DEBUG);
    /* Attempt to mount the repository-provided SPIFFS partition so tests that
     * open /spiffs/ files (for example worker_long_norm.wav) can access them.
     * This is intentionally a best-effort mount; if it fails we continue so
     * other tests can run and failure will be visible in test output. */
    {
        /* Check partition table first so we can see whether a partition labeled
         * "spiffs" actually exists at runtime (diagnose partition/label mismatch). */
        esp_partition_t *p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                    ESP_PARTITION_SUBTYPE_DATA_SPIFFS,
                                                    "spiffs");
        if (p) {
            ESP_LOGI(TAG, "Found partition 'spiffs' at offset 0x%08x size 0x%08x",
                     (unsigned int)p->address, (unsigned int)p->size);
        } else {
            ESP_LOGW(TAG, "Partition 'spiffs' not found via esp_partition_find_first()");
        }

        esp_vfs_spiffs_conf_t conf = {
            .base_path = "/spiffs",
            .partition_label = "spiffs",
            .max_files = 5,
            .format_if_mount_failed = false,
        };
        esp_err_t _err = esp_vfs_spiffs_register(&conf);
        if (_err == ESP_OK) {
            ESP_LOGI(TAG, "SPIFFS mounted at /spiffs");
            /* Additional diagnostics: try to open the expected test file right away */
            FILE *f = fopen("/spiffs/worker_long_norm.wav", "rb");
            if (f) {
                ESP_LOGI(TAG, "SPIFFS test file opened successfully");
                /* Diagnostic: print file size and a short hex peek of the
                 * beginning bytes so device logs can verify the flashed
                 * SPIFFS image matches the repository asset. */
                if (fseek(f, 0, SEEK_END) == 0) {
                    long sz = ftell(f);
                    if (sz >= 0) {
                        ESP_LOGI(TAG, "SPIFFS file size: %ld bytes", sz);
                        /* Read first 32 bytes (or file size) */
                        long to_read = sz < 32 ? sz : 32;
                        if (to_read > 0) {
                            if (fseek(f, 0, SEEK_SET) == 0) {
                                unsigned char buf[32] = {0};
                                size_t r = fread(buf, 1, (size_t)to_read, f);
                                if (r > 0) {
                                    /* Print as hex string */
                                    char hx[3 * 32 + 1];
                                    hx[0] = '\0';
                                    for (size_t i = 0; i < r; ++i) {
                                        char tmp[4];
                                        snprintf(tmp, sizeof(tmp), "%02x ", buf[i]);
                                        strncat(hx, tmp, sizeof(hx) - strlen(hx) - 1);
                                    }
                                    ESP_LOGI(TAG, "SPIFFS file head (%zu bytes): %s", r, hx);
                                } else {
                                    ESP_LOGW(TAG, "SPIFFS diag: fread returned 0 when peeking file head");
                                }
                            } else {
                                ESP_LOGW(TAG, "SPIFFS diag: fseek rewind failed");
                            }
                        }
                    } else {
                        ESP_LOGW(TAG, "SPIFFS diag: ftell returned negative value");
                    }
                } else {
                    ESP_LOGW(TAG, "SPIFFS diag: fseek end failed");
                }
                fclose(f);

                /* POSIX-level diagnostic: open the file via open()/read() to
                 * differentiate stdio wrapper issues from lower-level VFS
                 * behavior. */
                int fd = open("/spiffs/worker_long_norm.wav", O_RDONLY);
                if (fd >= 0) {
                    ESP_LOGI(TAG, "SPIFFS POSIX open succeeded (fd=%d)", fd);
                    unsigned char posix_buf[32] = {0};
                    ssize_t rr = read(fd, posix_buf, sizeof(posix_buf));
                    if (rr > 0) {
                        char hx[3 * 32 + 1];
                        hx[0] = '\0';
                        for (ssize_t i = 0; i < rr && i < (ssize_t)sizeof(posix_buf); ++i) {
                            char tmp[4];
                            snprintf(tmp, sizeof(tmp), "%02x ", posix_buf[i]);
                            strncat(hx, tmp, sizeof(hx) - strlen(hx) - 1);
                        }
                        ESP_LOGI(TAG, "SPIFFS POSIX head (%zd bytes): %s", rr, hx);
                    } else if (rr == 0) {
                        ESP_LOGW(TAG, "SPIFFS POSIX read returned 0 bytes");
                    } else {
                        int saved_errno = errno;
                        ESP_LOGW(TAG, "SPIFFS POSIX read failed: errno=%d (%s)",
                                 saved_errno, strerror(saved_errno));
                    }
                    close(fd);
                } else {
                    int saved_errno = errno;
                    ESP_LOGW(TAG, "SPIFFS POSIX open failed: errno=%d (%s)",
                             saved_errno, strerror(saved_errno));
                }
            } else {
                ESP_LOGW(TAG, "SPIFFS test file open failed: errno=%d (%s)", errno, strerror(errno));
            }
            /* Report filesystem usage statistics to help detect format/size issues */
            size_t total = 0, used = 0;
            esp_err_t info_err = esp_spiffs_info("spiffs", &total, &used);
            if (info_err == ESP_OK) {
                ESP_LOGI(TAG, "SPIFFS info: total=%u used=%u", (unsigned int)total, (unsigned int)used);
            } else {
                ESP_LOGW(TAG, "esp_vfs_spiffs_info failed: %s (%d)", esp_err_to_name(info_err), (int)info_err);
            }
        } else {
            ESP_LOGW(TAG, "SPIFFS mount failed: %s (%d)", esp_err_to_name(_err), (int)_err);
        }
    }
    run_audio_tests();
    printf("======== OVERALL TEST SUMMARY ========\n");
    printf("Tests run    : %d\n", Unity.NumberOfTests);
    printf("Tests passed : %d\n", Unity.NumberOfTests - Unity.TestFailures);
    printf("Tests failed : %d\n", Unity.TestFailures);
    printf("Success rate : %.1f%%\n", (Unity.NumberOfTests > 0) ?
           ((Unity.NumberOfTests - Unity.TestFailures) * 100.0f / Unity.NumberOfTests) : 0.0f);
    printf("=====================================\n");
    ESP_LOGI(TAG, "======== OVERALL TEST SUMMARY ========");
    ESP_LOGI(TAG, "Tests run     : %d", Unity.NumberOfTests);
    ESP_LOGI(TAG, "Tests passed  : %d", Unity.NumberOfTests - Unity.TestFailures);
    ESP_LOGI(TAG, "Tests failed  : %d", Unity.TestFailures);
    ESP_LOGI(TAG, "Success rate  : %.1f%%", (Unity.NumberOfTests > 0) ?
             ((Unity.NumberOfTests - Unity.TestFailures) * 100.0f / Unity.NumberOfTests) : 0.0f);
    ESP_LOGI(TAG, "=====================================");
    ESP_LOGI(TAG, "All tests completed. Test application will now enter idle loop.");
    printf("\n\n*** ENTERING IDLE LOOP - TESTS COMPLETE ***\n\n");
    /* Print a deterministic, machine-parseable final summary line so
       test runners that rely on a numeric summary can detect completion.
       Format matches the runner's expected regex: "<N> Tests <F> Failures <I> Ignored" */
    printf("%d Tests %d Failures %d Ignored\n", Unity.NumberOfTests, Unity.TestFailures, 0);
    /* Also emit a clear token for humans/diagnostics */
    printf("TEST_RUN_COMPLETE: %d %d %d\n", Unity.NumberOfTests, Unity.TestFailures, 0);
    fflush(stdout);
    /* Give the UART a moment to drain before entering the idle loop */
    vTaskDelay(200 / portTICK_PERIOD_MS);

    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        printf(".");
        fflush(stdout);
    }
}
