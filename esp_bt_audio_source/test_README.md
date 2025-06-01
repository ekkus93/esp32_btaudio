# ESP32 Bluetooth Audio Source Unit Tests

This document describes how to build and run the unit tests for the Bluetooth audio source project using the ESP-IDF Unity test framework.

## Prerequisites

- ESP-IDF installed and set up ([ESP-IDF Getting Started Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-staridf.py buildted/))
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

   The test application will display a menu of available tests.

4. To run a specific test, enter the test number and press Enter:

   ```
   Enter test for running.
   6
   ```

## Troubleshooting

### Queue Assertion Failure

If you encounter the following error during application startup:

```
assert failed: xQueueReceive queue.c:1531 (( pxQueue ))

Backtrace: 0x40081bd5:0x3ffd6410 0x4008ed3d:0x3ffd6430 0x400953c1:0x3ffd6450 0x4008f466:0x3ffd6570 0x400da049:0x3ffd65b0 0x4008f761:0x3ffd65f0

Rebooting...
```

This indicates a problem with FreeRTOS queue operations during Bluetooth initialization. The error occurs in the Bluetooth application task handler (`bt_app_task_handler` in `bt_app_core.c` line 65) where it's trying to receive from a queue that is NULL or not properly initialized.

To resolve this issue:

1. Check the `bt_app_task_start_up` function in `bt_app_core.c` to ensure the queue is created before the task starts
2. Verify that `bt_app_work_dispatch` is not called before queue initialization
3. Make sure to properly initialize all FreeRTOS components in the right order
4. Add additional error checking around queue creation and usage

You can debug this issue further by:

```
idf.py menuconfig
```

Then modify these specific settings:

1. Component config → FreeRTOS:
   - Increase "configMINIMAL_STACK_SIZE" to 2048 (from 1536)
   - If you see "ISR stack size", increase it to at least 2096 bytes (valid range is 2096-32768)
   - Look for any timer task stack size option and increase it to at least 3072

2. Component config → Bluetooth:
   - Browse through the Bluetooth controller settings and look for any stack size parameters
   - In the Bluedroid section, find the Bluedroid Host task stack size (exact name varies by ESP-IDF version)
   - Increase any stack size values you find by at least 50%
   - Enable any available memory debugging options
   - Make sure Classic Bluetooth is enabled for A2DP functionality

3. Debug crash settings:
   - Component config → Core dump
     - Set "Data destination" to UART (this will send core dumps to the serial console)
   - Component config → ESP System Settings
     - For "Panic handler behaviour", you can choose either:
       - "Print registers and halt" - prints registers but requires manual reset
       - "Print registers and reboot" - automatically restarts after crash (better for repeated testing)
     
   The settings you chose ("Print registers and halt" and Core dump to UART) are good for initial debugging as they'll let you see the crash details in the console

After making these changes, rebuild and flash the application:

```
idf.py build flash monitor
```

### Task Watchdog Timer Warnings

When running the test application, you will see Task Watchdog Timer (TWDT) warnings in the console while the test menu is waiting for input:

```
E (5290) task_wdt: Task watchdog got triggered. The following tasks/users did not reset the watchdog in time:
E (5290) task_wdt:  - IDLE0 (CPU 0)
```

**These warnings are normal** and occur because:
- The main task is blocked waiting for user input from the serial console
- During this time, it cannot feed the watchdog timer
- The warnings do not affect the functionality of the tests and will continue to appear at regular intervals
- The warnings will temporarily stop during test execution and resume when waiting for the next input

**Important**: The watchdog warnings will reappear after a test completes successfully. This is expected behavior and does not indicate a crash. The ESP32 is still running normally and waiting for your next input.

You can disable these warnings (optional) by modifying the Task Watchdog Timer configuration in menuconfig:
```
Component config → ESP System Settings → Task Watchdog → [disable] Initialize Task Watchdog Timer on startup
```

## Example Test Execution

Here's an example of running test #1 (Bluetooth scan):

```
Enter test for running.
1

Running Bluetooth scan starts successfully...
I (5340) BT_STUB: Stub: Starting BT scan
I (10340) BT_STUB: Stub: Stopping BT scan
I (10340) BT_STUB: Stub: Getting discovered device count: 3
./main/test_app_main.c:13:Bluetooth scan starts successfully:PASS
Test ran in 5025ms

-----------------------
1 Tests 0 Failures 0 Ignored 
OK
Enter next test, or 'enter' to see menu
```

## Adding More Tests

- Add new test cases to `test_app_main.c` using the `TEST_CASE` macro.
- Rebuild and flash as above to run new tests.

## References

- [ESP-IDF Unit Testing Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/unit-tests.html)
- [ESP-IDF Task Watchdog Timer](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/wdts.html#task-watchdog-timer)
- [ESP-IDF FreeRTOS API Reference](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/freertos.html)