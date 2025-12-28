#pragma once

#include "esp_err.h"

/* Test-only stub for the Bluetooth manager. The real component is excluded
 * from the audio test app to keep the binary small, so we provide the minimal
 * API surface required by the tests. */
int bt_manager_stop_audio(void);
