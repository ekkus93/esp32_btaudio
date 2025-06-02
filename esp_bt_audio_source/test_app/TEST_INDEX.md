# ESP32 BT Audio Source Test Index

## 1. Bluetooth A2DP Tests (22 tests)

| Test # | Test Function | Description |
|--------|--------------|-------------|
| 1 | test_bluetooth_stack_init | Bluetooth stack initializes successfully |
| 2 | test_bluetooth_scan_start | Bluetooth scan starts successfully |
| 3 | test_bluetooth_scan_discovered_devices | Bluetooth scan reports discovered devices |
| 4 | test_bluetooth_scan_filter_by_type | Bluetooth scan filters by device type |
| 5 | test_bluetooth_scanning_basic | Bluetooth scanning basic functionality |
| 6 | test_bluetooth_scan_device_details | Bluetooth scan returns device details |
| 7 | test_bluetooth_scan_timeout | Bluetooth scan times out properly |
| 8 | test_bluetooth_scan_stop_early | Bluetooth scan can be stopped early |
| 9 | test_bluetooth_connection | Connect to a device by address |
| 10 | test_connect_by_name | Connect to a device by name |
| 11 | test_connection_failure_handling | Handle connection failure gracefully |
| 12 | test_connection_timeout | Handle connection timeout |
| 13 | test_connection_status_info | Get connection status information |
| 14 | test_auto_reconnect | Auto-reconnect when connection drops |
| 15 | test_connect_to_a2dp_sink | Bluetooth connects to A2DP sink |
| 16 | test_a2dp_streaming | A2DP starts and stops streaming |
| 17 | test_a2dp_paired_devices | A2DP remembers paired devices |
| 18 | test_audio_streaming_start_success | Audio streaming starts successfully |
| 19 | test_audio_streaming_stop_success | Audio streaming stops successfully |
| 20 | test_streaming_requires_connection | Audio streaming cannot start when disconnected |
| 21 | test_streaming_pause_resume | Audio streaming can be paused and resumed |
| 22 | test_streaming_state_reporting | Audio streaming state is reported correctly |

## 2. I2S Audio Tests (2 tests)

| Test # | Test Function | Description |
|--------|--------------|-------------|
| 23 | test_i2s_driver_init | I2S driver initializes successfully |
| 24 | test_i2s_standard_mode | I2S standard mode configuration works |

## 3. Audio Pipeline Tests (11 tests)

| Test # | Test Function | Description |
|--------|--------------|-------------|
| 25 | test_audio_buffer_pool_init | Audio buffer pool initialization |
| 26 | test_audio_buffer_allocation | Audio buffer allocation and release |
| 27 | test_audio_single_processing_stage | Single-stage audio processing |
| 28 | test_audio_multi_stage_pipeline | Multi-stage audio processing pipeline |
| 29 | test_audio_buffer_init_basic | Basic audio buffer operations |
| 30 | test_volume_set_get | Volume level setting and retrieval |
| 31 | test_volume_apply_to_samples | Volume application to audio samples |
| 32 | test_volume_mute_unmute | Audio mute/unmute operations |
| 33 | test_configure_sample_rate | Sample rate configuration |
| 34 | test_buffer_sizes_sample_rates | Buffer size calculations for different rates |
| 35 | test_sample_rate_conversion | Sample rate conversion |

## 4. PCM Format Tests (4 tests)

| Test # | Test Function | Description |
|--------|--------------|-------------|
| 36 | test_pcm_16bit_format | 16-bit PCM format handling |
| 37 | test_pcm_24bit_format | 24-bit PCM format handling |
| 38 | test_pcm_endianness | PCM endianness conversion |
| 39 | test_pcm_16bit_to_32bit | Bit depth conversion (16/24/32-bit) |

## 5. I2S Channel Tests (5 tests)

| Test # | Test Function | Description |
|--------|--------------|-------------|
| 40 | test_mono_channel_config | Mono channel configuration |
| 41 | test_stereo_channel_config | Stereo channel configuration |
| 42 | test_stereo_to_mono_conversion | Stereo to mono conversion |
| 43 | test_mono_to_stereo_conversion | Mono to stereo conversion |
| 44 | test_channel_independence | Channel independence |

## Summary
- Total tests: 44
- All tests implemented and passing
