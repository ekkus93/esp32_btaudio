#!/bin/bash
# Flash fixed firmware to ESP32 to stop watchdog bug
set -e

cd /home/phil/work/esp32/esp32_btaudio/esp_bt_audio_source
. $HOME/esp/esp-idf/export.sh

echo "=== Flashing fixed firmware (autostart disabled) ==="
idf.py -p /dev/ttyUSB0 flash

echo "=== Flash complete! Monitoring boot... ==="
echo "=== Press Ctrl+] to exit monitor ==="
idf.py -p /dev/ttyUSB0 monitor
