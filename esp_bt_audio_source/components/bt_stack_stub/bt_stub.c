#include "sdkconfig.h"

#if !defined(CONFIG_BT_ENABLED) || (CONFIG_BT_ENABLED == 0)

#include <stdbool.h>
#include <stdlib.h>

#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"
#include "esp_bt.h"
#include "esp_bt_device.h"
#include "esp_bt_main.h"
#include "esp_err.h"
#include "esp_gap_bt_api.h"
#include "esp_log.h"
#include "osi/allocator.h"

static const char *TAG = "bt_stub";
static esp_avrc_rn_evt_cap_mask_t s_local_evt_cap = {0};
static esp_avrc_rn_evt_cap_mask_t s_peer_evt_cap = {0};
static uint8_t s_device_addr[ESP_BD_ADDR_LEN] = {0};

static void log_stub_call(const char *func)
{
    ESP_LOGW(TAG, "%s called with CONFIG_BT_DISABLED; returning stubbed success", func);
}

esp_err_t esp_bt_controller_init(esp_bt_controller_config_t *cfg)
{
    (void)cfg;
    log_stub_call(__func__);
    return ESP_OK;
}

esp_err_t esp_bt_controller_enable(esp_bt_mode_t mode)
{
    (void)mode;
    log_stub_call(__func__);
    return ESP_OK;
}

esp_err_t esp_bt_controller_disable(void)
{
    log_stub_call(__func__);
    return ESP_OK;
}

esp_err_t esp_bt_controller_deinit(void)
{
    log_stub_call(__func__);
    return ESP_OK;
}

esp_err_t esp_bt_controller_mem_release(esp_bt_mode_t mode)
{
    (void)mode;
    log_stub_call(__func__);
    return ESP_OK;
}

esp_err_t esp_bluedroid_init(void)
{
    log_stub_call(__func__);
    return ESP_OK;
}

esp_err_t esp_bluedroid_init_with_cfg(esp_bluedroid_config_t *cfg)
{
    (void)cfg;
    log_stub_call(__func__);
    return ESP_OK;
}

esp_err_t esp_bluedroid_enable(void)
{
    log_stub_call(__func__);
    return ESP_OK;
}

esp_err_t esp_bluedroid_disable(void)
{
    log_stub_call(__func__);
    return ESP_OK;
}

esp_err_t esp_bluedroid_deinit(void)
{
    log_stub_call(__func__);
    return ESP_OK;
}

esp_err_t esp_bt_gap_set_device_name(const char *name)
{
    (void)name;
    log_stub_call(__func__);
    return ESP_OK;
}

esp_err_t esp_bt_gap_register_callback(esp_bt_gap_cb_t callback)
{
    (void)callback;
    log_stub_call(__func__);
    return ESP_OK;
}

esp_err_t esp_bt_gap_set_scan_mode(esp_bt_connection_mode_t c_mode, esp_bt_discovery_mode_t d_mode)
{
    (void)c_mode;
    (void)d_mode;
    log_stub_call(__func__);
    return ESP_OK;
}

esp_err_t esp_bt_gap_start_discovery(esp_bt_inq_mode_t mode, uint8_t inq_len, uint8_t num_rsps)
{
    (void)mode;
    (void)inq_len;
    (void)num_rsps;
    log_stub_call(__func__);
    return ESP_OK;
}

esp_err_t esp_bt_gap_cancel_discovery(void)
{
    log_stub_call(__func__);
    return ESP_OK;
}

esp_err_t esp_bt_gap_remove_bond_device(esp_bd_addr_t bd_addr)
{
    (void)bd_addr;
    log_stub_call(__func__);
    return ESP_OK;
}

esp_err_t esp_bt_gap_get_device_name(void)
{
    log_stub_call(__func__);
    return ESP_OK;
}

esp_err_t esp_bt_gap_set_pin(esp_bt_pin_type_t pin_type, uint8_t pin_code_len, esp_bt_pin_code_t pin_code)
{
    (void)pin_type;
    (void)pin_code_len;
    (void)pin_code;
    log_stub_call(__func__);
    return ESP_OK;
}

esp_err_t esp_bt_gap_ssp_confirm_reply(esp_bd_addr_t bd_addr, bool accept)
{
    (void)bd_addr;
    (void)accept;
    log_stub_call(__func__);
    return ESP_OK;
}

esp_err_t esp_bt_gap_pin_reply(esp_bd_addr_t bd_addr, bool accept, uint8_t pin_code_len, esp_bt_pin_code_t pin_code)
{
    (void)bd_addr;
    (void)accept;
    (void)pin_code_len;
    (void)pin_code;
    log_stub_call(__func__);
    return ESP_OK;
}

uint8_t *esp_bt_gap_resolve_eir_data(uint8_t *eir, esp_bt_eir_type_t type, uint8_t *length)
{
    (void)eir;
    (void)type;
    if (length != NULL) {
        *length = 0;
    }
    log_stub_call(__func__);
    return NULL;
}

esp_err_t esp_a2d_register_callback(esp_a2d_cb_t callback)
{
    (void)callback;
    log_stub_call(__func__);
    return ESP_OK;
}

esp_err_t esp_a2d_source_register_data_callback(esp_a2d_source_data_cb_t callback)
{
    (void)callback;
    log_stub_call(__func__);
    return ESP_OK;
}

esp_err_t esp_a2d_source_init(void)
{
    log_stub_call(__func__);
    return ESP_OK;
}

esp_err_t esp_a2d_source_deinit(void)
{
    log_stub_call(__func__);
    return ESP_OK;
}

esp_err_t esp_a2d_source_connect(esp_bd_addr_t remote_bda)
{
    (void)remote_bda;
    log_stub_call(__func__);
    return ESP_OK;
}

esp_err_t esp_a2d_source_disconnect(esp_bd_addr_t remote_bda)
{
    (void)remote_bda;
    log_stub_call(__func__);
    return ESP_OK;
}

esp_err_t esp_a2d_media_ctrl(esp_a2d_media_ctrl_t ctrl)
{
    (void)ctrl;
    log_stub_call(__func__);
    return ESP_OK;
}

esp_err_t esp_avrc_ct_init(void)
{
    log_stub_call(__func__);
    return ESP_OK;
}

esp_err_t esp_avrc_ct_register_callback(esp_avrc_ct_cb_t callback)
{
    (void)callback;
    log_stub_call(__func__);
    return ESP_OK;
}

esp_err_t esp_avrc_tg_set_rn_evt_cap(const esp_avrc_rn_evt_cap_mask_t *evt_set)
{
    if (evt_set != NULL) {
        s_local_evt_cap = *evt_set;
    }
    log_stub_call(__func__);
    return ESP_OK;
}

esp_err_t esp_avrc_tg_get_rn_evt_cap(esp_avrc_rn_evt_cap_t cap, esp_avrc_rn_evt_cap_mask_t *evt_set)
{
    (void)cap;
    if (evt_set != NULL) {
        *evt_set = s_local_evt_cap;
    }
    log_stub_call(__func__);
    return ESP_OK;
}

bool esp_avrc_rn_evt_bit_mask_operation(esp_avrc_bit_mask_op_t op, esp_avrc_rn_evt_cap_mask_t *events,
                                         esp_avrc_rn_event_ids_t event_id)
{
    if (events == NULL) {
        return false;
    }

    const uint16_t bit = (uint16_t)(1U << event_id);

    switch (op) {
    case ESP_AVRC_BIT_MASK_OP_SET:
        events->bits |= bit;
        return true;
    case ESP_AVRC_BIT_MASK_OP_CLEAR:
        events->bits &= (uint16_t)(~bit);
        return true;
    case ESP_AVRC_BIT_MASK_OP_TEST:
        return (events->bits & bit) != 0;
    default:
        return false;
    }
}

esp_err_t esp_avrc_ct_send_get_rn_capabilities_cmd(uint8_t tl)
{
    (void)tl;
    s_peer_evt_cap = s_local_evt_cap;
    log_stub_call(__func__);
    return ESP_OK;
}

esp_err_t esp_avrc_ct_send_register_notification_cmd(uint8_t tl, uint8_t event_id, uint32_t event_parameter)
{
    (void)tl;
    (void)event_id;
    (void)event_parameter;
    log_stub_call(__func__);
    return ESP_OK;
}

esp_err_t esp_avrc_ct_send_set_absolute_volume_cmd(uint8_t tl, uint8_t volume)
{
    (void)tl;
    (void)volume;
    log_stub_call(__func__);
    return ESP_OK;
}

esp_err_t esp_avrc_tg_send_rn_rsp(esp_avrc_rn_event_ids_t event_id, esp_avrc_rn_rsp_t rsp, esp_avrc_rn_param_t *param)
{
    (void)event_id;
    (void)rsp;
    (void)param;
    log_stub_call(__func__);
    return ESP_OK;
}

const uint8_t *esp_bt_dev_get_address(void)
{
    log_stub_call(__func__);
    return s_device_addr;
}

void *osi_malloc_func(size_t size)
{
    log_stub_call(__func__);
    return malloc(size);
}

void osi_free_func(void *ptr)
{
    log_stub_call(__func__);
    free(ptr);
}

#endif /* !CONFIG_BT_ENABLED */
