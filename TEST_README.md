# Testing the ESP32 BT Audio Source Without Real Bluetooth Devices

This document explains how to test the ESP32 Bluetooth Audio Source project without having access to real Bluetooth devices.

## Test Mode

The application includes a test mode that can be enabled by setting `TEST_MODE_NO_BT_DEVICE` to 1 in `main.c`. When this mode is active:

1. If no Bluetooth devices are found after several discovery attempts, the application automatically enters test mode
2. In test mode, the application generates a continuous middle C tone (261.63 Hz)
3. No actual Bluetooth connection is needed

## How to Use Test Mode

1. Make sure `TEST_MODE_NO_BT_DEVICE` is set to 1 in `main.c`
2. Build and flash the application
3. Monitor serial output
4. Wait for the application to enter test mode after several failed discovery attempts

## Expected Output

When the app enters test mode, you should see:

