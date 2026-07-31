#include "bt_manager.h"
#include "bt_manager_internal.h"
#include "bt_events_avrc.h"
#include "bt_events_a2dp.h"
#include "bt_hfp_ag.h"
#include "util_safe.h"

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"
#define TAG "BT_MGR"
#else
#include "esp_log.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"
#define TAG "BT_MGR"
#endif

#ifdef ESP_PLATFORM
esp_err_t bt_manager_init_profiles(void)
{
    bool avrc_initialized = false;
    bool a2dp_initialized = false;
    bool hfp_attempted = false;
    esp_err_t ret = esp_avrc_ct_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Initialize AVRCP controller failed: %s", esp_err_to_name(ret));
        return ret;
    }
    avrc_initialized = true;

    ret = esp_avrc_ct_register_callback(bt_events_avrc_callback);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Register AVRCP controller callback failed: %s", esp_err_to_name(ret));
        goto fail;
    }
    ret = esp_a2d_source_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Initialize A2DP source failed: %s", esp_err_to_name(ret));
        goto fail;
    }
    a2dp_initialized = true;
    ret = esp_a2d_register_callback(bt_events_a2dp_callback);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Register A2DP source callback failed: %s", esp_err_to_name(ret));
        goto fail;
    }
    ret = esp_a2d_source_register_data_callback(bt_events_a2dp_data_callback);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Register A2DP data callback failed: %s", esp_err_to_name(ret));
        goto fail;
    }
    hfp_attempted = true;
    ret = bt_hfp_ag_profile_init(BT_HFP_AG_DEFAULT_LIFECYCLE_TIMEOUT_MS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Initialize HFP Audio Gateway failed: %s", esp_err_to_name(ret));
        goto fail;
    }
    return ESP_OK;

fail:
    if (hfp_attempted) {
        esp_err_t cleanup_err = bt_hfp_ag_profile_deinit(2000U);
        if (cleanup_err != ESP_OK && cleanup_err != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "HFP rollback failed: %s", esp_err_to_name(cleanup_err));
        }
    }
    if (a2dp_initialized) {
        esp_err_t cleanup_err = esp_a2d_source_deinit();
        if (cleanup_err != ESP_OK) ESP_LOGE(TAG, "A2DP rollback failed: %s", esp_err_to_name(cleanup_err));
    }
    if (avrc_initialized) {
        esp_err_t cleanup_err = esp_avrc_ct_deinit();
        if (cleanup_err != ESP_OK) ESP_LOGE(TAG, "AVRCP rollback failed: %s", esp_err_to_name(cleanup_err));
    }
    return ret;
}

esp_err_t bt_manager_deinit_profiles(void)
{
    esp_err_t first_error = ESP_OK;
    bt_hfp_ag_snapshot_t hfp_snapshot;
    esp_err_t err = bt_hfp_ag_get_snapshot(&hfp_snapshot);
    if (err == ESP_OK && hfp_snapshot.lifecycle != BT_HFP_AG_LIFECYCLE_UNINITIALIZED) {
        err = bt_hfp_ag_profile_deinit(BT_HFP_AG_DEFAULT_LIFECYCLE_TIMEOUT_MS);
        if (err != ESP_OK && first_error == ESP_OK) first_error = err;
    } else if (err != ESP_OK && err != ESP_ERR_INVALID_STATE && first_error == ESP_OK) {
        first_error = err;
    }
    err = esp_a2d_source_deinit();
    if (err != ESP_OK && first_error == ESP_OK) first_error = err;
    err = esp_avrc_ct_deinit();
    if (err != ESP_OK && first_error == ESP_OK) first_error = err;
    return first_error;
}
#endif

#ifdef UNIT_TEST
esp_err_t bt_manager_test_init_profiles(void)
{
#ifdef ESP_PLATFORM
    return bt_manager_init_profiles();
#else
#ifdef BT_MANAGER_TEST_HFP_PROFILES
    bool avrc_initialized = false;
    bool a2dp_initialized = false;
    bool hfp_attempted = false;
    esp_err_t ret = esp_avrc_ct_init();
    if (ret != ESP_OK) return ret;
    avrc_initialized = true;
    ret = esp_avrc_ct_register_callback(NULL);
    if (ret != ESP_OK) goto host_fail;
    ret = esp_a2d_source_init();
    if (ret != ESP_OK) goto host_fail;
    a2dp_initialized = true;
    ret = esp_a2d_register_callback(NULL);
    if (ret != ESP_OK) goto host_fail;
    ret = esp_a2d_source_register_data_callback(NULL);
    if (ret != ESP_OK) goto host_fail;
    hfp_attempted = true;
    ret = bt_hfp_ag_profile_init(10U);
    if (ret != ESP_OK) goto host_fail;
    return ESP_OK;

host_fail:
    if (hfp_attempted) {
        esp_err_t cleanup_err = bt_hfp_ag_profile_deinit(10U);
        if (cleanup_err != ESP_OK && cleanup_err != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "Host HFP rollback failed: %s", esp_err_to_name(cleanup_err));
        }
    }
    if (a2dp_initialized) (void)esp_a2d_source_deinit();
    if (avrc_initialized) (void)esp_avrc_ct_deinit();
    return ret;
#else
    esp_err_t ret;
    ret = esp_avrc_ct_init();
    if (ret != ESP_OK) return ESP_FAIL;
    ret = esp_avrc_ct_register_callback(NULL);
    if (ret != ESP_OK) return ESP_FAIL;
    ret = esp_a2d_source_init();
    if (ret != ESP_OK) return ESP_FAIL;
    ret = esp_a2d_register_callback(NULL);
    if (ret != ESP_OK) return ESP_FAIL;
    ret = esp_a2d_source_register_data_callback(NULL);
    if (ret != ESP_OK) return ESP_FAIL;
    return ESP_OK;
#endif
#endif
}
#endif
