#include "bt_duplex_policy_manager_stub.h"

#include <string.h>

#include "bt_hfp_manager.h"
#include "bt_manager_internal.h"

bt_manager_context_t bt_ctx;
bool s_autostart_enabled;

static bool s_locked;
static bt_duplex_mode_t s_configured_mode;

void bt_duplex_policy_manager_stub_reset(bt_duplex_mode_t configured_mode)
{
    memset(&bt_ctx, 0, sizeof(bt_ctx));
    bt_ctx.initialized = true;
    s_locked = false;
    s_configured_mode = configured_mode;
    s_autostart_enabled = false;
}

esp_err_t bt_ctx_lock(uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (s_locked) return ESP_ERR_INVALID_STATE;
    s_locked = true;
    return ESP_OK;
}

void bt_ctx_unlock(void)
{
    s_locked = false;
}

bt_duplex_mode_t bt_manager_hfp_configured_mode_locked(void)
{
    return s_configured_mode;
}

esp_err_t bt_manager_hfp_get_configured_mode(bt_duplex_mode_t *mode_out)
{
    if (mode_out == NULL) return ESP_ERR_INVALID_ARG;
    esp_err_t err = bt_ctx_lock(PLATFORM_WAIT_FOREVER);
    if (err != ESP_OK) return err;
    if (!bt_ctx.initialized) {
        bt_ctx_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    *mode_out = s_configured_mode;
    bt_ctx_unlock();
    return ESP_OK;
}
