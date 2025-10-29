#pragma once

#include <stdbool.h>
#include "bt_source.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Publish connection info gathered by the stub so the shim can serve
 *        bt_get_connection_info callers and notify registered callbacks.
 *
 * @param info Pointer to the latest connection snapshot; must not be NULL.
 */
void bt_connection_shim_publish_info(const bt_connection_info_t *info);

/**
 * @brief Clear cached connection information maintained by the shim.
 */
void bt_connection_shim_clear_info(void);

/**
 * @brief Copy the cached connection information into @p info if available.
 *
 * @param info Output pointer that receives the cached snapshot; must not be NULL.
 */
void bt_connection_shim_get_cached_info(bt_connection_info_t *info);

/**
 * @brief Determine whether a connection callback is currently registered.
 *
 * @return true if a callback is registered, false otherwise.
 */
bool bt_connection_shim_callback_registered(void);

#ifdef __cplusplus
}
#endif
