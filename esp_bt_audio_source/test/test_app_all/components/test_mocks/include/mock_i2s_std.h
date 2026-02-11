/* Thin compatibility header for test_app build: forward to the repository's test mock header.
 * This avoids duplicating the full mock implementation; it simply includes the existing
 * mock header located under esp_bt_audio_source/test/host_test/mocks.
 */

#pragma once

#include <mock_i2s_std.h>
