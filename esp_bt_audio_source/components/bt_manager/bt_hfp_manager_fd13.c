#include "bt_hfp_manager.h"

#include <string.h>

#include "bt_app_core.h"
#include "bt_hfp_audio.h"
#include "hfp_i2s_output.h"

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_system.h"
#endif

#ifdef UNIT_TEST
static bt_hfp_manager_diagnostics_platform_ops_t s_test_ops;
static bool s_test_ops_set;
#endif

static bool unavailable_result(esp_err_t err)
{
    return err == ESP_ERR_NOT_FOUND || err == ESP_ERR_INVALID_STATE ||
           err == ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t capture_heap(bt_hfp_manager_diagnostics_t *snapshot)
{
#ifdef UNIT_TEST
    if (s_test_ops_set) {
        snapshot->heap_available = true;
        snapshot->free_internal_bytes = s_test_ops.free_internal_bytes();
        snapshot->minimum_free_heap_bytes_lifetime =
            s_test_ops.minimum_free_heap_bytes_lifetime();
        snapshot->largest_internal_free_block_bytes =
            s_test_ops.largest_internal_free_block_bytes();
        return ESP_OK;
    }
#endif
#ifdef ESP_PLATFORM
    snapshot->heap_available = true;
    snapshot->free_internal_bytes =
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    snapshot->minimum_free_heap_bytes_lifetime =
        esp_get_minimum_free_heap_size();
    snapshot->largest_internal_free_block_bytes =
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    return ESP_OK;
#else
    snapshot->heap_available = false;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

static esp_err_t get_hfp_stack_min_free(size_t *out)
{
#ifdef UNIT_TEST
    if (s_test_ops_set) {
        return s_test_ops.hfp_app_task_min_free_stack_bytes_lifetime(out);
    }
#endif
    return bt_app_task_get_stack_high_water_mark(out);
}

static esp_err_t get_i2s_stack_min_free(size_t *out)
{
#ifdef UNIT_TEST
    if (s_test_ops_set) {
        return s_test_ops.i2s_writer_task_min_free_stack_bytes_lifetime(out);
    }
#endif
    return hfp_i2s_output_get_stack_high_water_mark(out);
}

static esp_err_t capture_optional_stack(
    esp_err_t result,
    bool *available,
    size_t *value)
{
    if (result == ESP_OK) {
        *available = true;
        return ESP_OK;
    }
    if (unavailable_result(result)) {
        *available = false;
        *value = 0U;
        return ESP_OK;
    }
    return result;
}

esp_err_t bt_manager_hfp_get_diagnostics(bt_hfp_manager_diagnostics_t *out)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;

    bt_hfp_manager_status_t status;
    esp_err_t err = bt_manager_hfp_get_status(&status);
    if (err != ESP_OK) return err;

    bt_hfp_manager_diagnostics_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));

    err = capture_heap(&snapshot);
    if (err != ESP_OK && !unavailable_result(err)) return err;

    err = capture_optional_stack(
        get_hfp_stack_min_free(
            &snapshot.hfp_app_task_min_free_stack_bytes_lifetime),
        &snapshot.hfp_app_task_stack_available,
        &snapshot.hfp_app_task_min_free_stack_bytes_lifetime);
    if (err != ESP_OK) return err;

    err = capture_optional_stack(
        get_i2s_stack_min_free(
            &snapshot.i2s_writer_task_min_free_stack_bytes_lifetime),
        &snapshot.i2s_writer_task_stack_available,
        &snapshot.i2s_writer_task_min_free_stack_bytes_lifetime);
    if (err != ESP_OK) return err;

    bt_hfp_audio_snapshot_t callback;
    err = bt_hfp_audio_get_snapshot(&callback);
    if (err == ESP_OK) {
        snapshot.incoming_callback_available = true;
        snapshot.incoming_callback_budget_us =
            BT_HFP_AUDIO_CALLBACK_BUDGET_US;
        snapshot.incoming_callback_last_us = callback.callback_last_us;
        snapshot.incoming_callback_max_us_lifetime = callback.callback_max_us;
        snapshot.incoming_callback_over_budget_lifetime =
            callback.callback_over_budget;
    } else if (unavailable_result(err)) {
        snapshot.incoming_callback_available = false;
    } else {
        return err;
    }

    *out = snapshot;
    return ESP_OK;
}

#ifdef UNIT_TEST
esp_err_t bt_manager_hfp_fd13_test_set_platform_ops(
    const bt_hfp_manager_diagnostics_platform_ops_t *ops)
{
    if (ops == NULL || ops->free_internal_bytes == NULL ||
        ops->minimum_free_heap_bytes_lifetime == NULL ||
        ops->largest_internal_free_block_bytes == NULL ||
        ops->hfp_app_task_min_free_stack_bytes_lifetime == NULL ||
        ops->i2s_writer_task_min_free_stack_bytes_lifetime == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_test_ops = *ops;
    s_test_ops_set = true;
    return ESP_OK;
}

void bt_manager_hfp_fd13_test_reset_platform_ops(void)
{
    memset(&s_test_ops, 0, sizeof(s_test_ops));
    s_test_ops_set = false;
}
#endif
