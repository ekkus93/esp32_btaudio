# ESP32 Bluetooth Audio Source Unit Tests

This document describes how to build and run the unit tests for the Bluetooth audio source project using the ESP-IDF Unity test framework.

## Prerequisites

- ESP-IDF installed and set up ([ESP-IDF Getting Started Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/))
- ESP32 development board connected

## Test Application Location

The test application source is located at:

```
esp_bt_audio_source/test_app/main/test_app_main.c
```

## Building the Test Application

1. Open a terminal and navigate to the test application directory:

   ```
   cd /home/phil/work/esp32/esp32_btaudio/esp_bt_audio_source/test_app
   ```

2. Set up the ESP-IDF environment if not already done:

   ```
   . $IDF_PATH/export.sh
   ```

3. Build the test application:

   ```
   idf.py build
   ```

## Flashing and Running the Tests

1. Connect your ESP32 board via USB.

2. Flash the test application to the board:

   ```
   idf.py -p /dev/ttyUSB0 flash
   ```

   Replace `/dev/ttyUSB0` with your serial port if different.

3. Monitor the serial output to see Unity test results:

   ```
   idf.py -p /dev/ttyUSB0 monitor
   ```

   The test runner will automatically execute all tests in `test_app_main.c` and print the results.

## Example Output

```
Running main() from /path/to/unity/unity_main.c
[==========] Running 3 test(s).
[ RUN      ] Bluetooth stack initializes successfully
[       OK ] Bluetooth stack initializes successfully
[ RUN      ] Parse SCAN command
[       OK ] Parse SCAN command
[ RUN      ] Parse CONNECT command
[       OK ] Parse CONNECT command
[==========] 3 test(s) run.
[  PASSED  ] 3 test(s).
```

## Adding More Tests

- Add new test cases to `test_app_main.c` using the `TEST_CASE` macro.
- Rebuild and flash as above to run new tests.

## References

- [ESP-IDF Unit Testing Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/unit-tests.html)