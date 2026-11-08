#ifndef BT_IDF_COMPAT_H
#define BT_IDF_COMPAT_H

/*
 * ESP-IDF 6.x A2DP API compatibility.
 *
 * IDF 6.0 removed the deprecated A2DP names (see docs/en/migration-guides/
 * release-6.x/6.0/bluetooth-classic.rst). Map them onto the names that
 * replaced them so shared code keeps compiling.
 *
 * This is behavior-preserving. In IDF 5.5.1 the same names were already
 * deprecated aliases:
 *   - ESP_A2D_AUDIO_STATE_STOPPED and ESP_A2D_AUDIO_STATE_REMOTE_SUSPEND
 *     were defined as ESP_A2D_AUDIO_STATE_SUSPEND (esp_a2dp_api.h), and the
 *     stack only ever reported SUSPEND (btc_av.c).
 *   - esp_a2d_media_ctrl() rewrote ESP_A2D_MEDIA_CTRL_STOP to
 *     ESP_A2D_MEDIA_CTRL_SUSPEND before use (esp_a2dp_api.c).
 *
 * Include after esp_a2dp_api.h. No effect on IDF 5.x or host builds.
 */

#if defined(ESP_PLATFORM)
#include "esp_idf_version.h"

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
#define ESP_A2D_AUDIO_STATE_REMOTE_SUSPEND ESP_A2D_AUDIO_STATE_SUSPEND
#define ESP_A2D_AUDIO_STATE_STOPPED ESP_A2D_AUDIO_STATE_SUSPEND
#define ESP_A2D_MEDIA_CTRL_STOP ESP_A2D_MEDIA_CTRL_SUSPEND
#endif
#endif /* ESP_PLATFORM */

#endif /* BT_IDF_COMPAT_H */
