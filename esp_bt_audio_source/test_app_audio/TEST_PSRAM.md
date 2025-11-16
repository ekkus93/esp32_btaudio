On-device PSRAM integration tests

This directory contains a Unity-based on-device test that verifies PSRAM allocation behavior
and that the audio processor places its large buffers into PSRAM when available.

How to run (example):

1) Build, flash and monitor the test app (replace <PORT> with your serial port):

```bash
cd esp_bt_audio_source/test_app_audio
idf.py -p /dev/ttyUSB0 build flash monitor
```

2) The Unity test runner in the test app will execute the audio processor test suite.
   Look for the test group "audio processor" and the new tests:
   - test_heap_psram_simple
   - test_audio_processor_psram_allocations

Notes and expectations:
- On boards with PSRAM available and enabled in sdkconfig, the tests expect SPIRAM
  allocations to consume SPIRAM pool bytes when allowed.
- If PSRAM is not present the tests will assert that DRAM is used instead.
- The tests are intentionally conservative when checking "which pool" changed;
  they only require that either the SPIRAM or DRAM free-size decreased after
  allocations.

If you want the tests to run in a different order or to be gated behind a
Kconfig option (for example to skip on boards without PSRAM), modify the
CMakeLists.txt or add runtime checks using `esp_psram_is_initialized()`.
