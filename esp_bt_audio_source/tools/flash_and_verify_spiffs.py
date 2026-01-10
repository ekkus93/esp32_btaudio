#!/usr/bin/env python3
"""Flash partition table + SPIFFS + app and verify PARTS/FILES over serial.

Usage:
  ./flash_and_verify_spiffs.py [--port /dev/ttyUSB0] [--spiffs-image main/assets/spiffs/spiffs.bin]

Notes:
- Requires an ESP-IDF environment available at $HOME/esp/esp-idf; the script sources the export script before running idf.py.
- Exits 0 on success (both PARTS and FILES verified), non-zero on failure.
"""
import argparse
import os
import re
import shlex
import subprocess
import sys
import time

try:
    import serial
except Exception:
    print("Missing dependency: pyserial. Install with: pip install pyserial", file=sys.stderr)
    sys.exit(2)


def run_shell(cmd):
    # run in bash with IDF exported already when needed by caller
    print(f"RUN: {cmd}")
    proc = subprocess.run(cmd, shell=True)
    return proc.returncode


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--port', default='/dev/ttyUSB0')
    p.add_argument('--spiffs-image', default='main/assets/spiffs/spiffs.bin')
    p.add_argument('--spiffs-offset', default='0x1C0000')
    p.add_argument('--baud', type=int, default=115200)
    p.add_argument('--project-dir', default='esp_bt_audio_source')
    p.add_argument('--monitor-wait', type=float, default=3.0, help='Seconds to wait for command response')
    args = p.parse_args()

    project_dir = os.path.abspath(args.project_dir)
    spiffs_image = os.path.abspath(os.path.join(project_dir, args.spiffs_image)) if not os.path.isabs(args.spiffs_image) else args.spiffs_image

    if not os.path.isfile(spiffs_image):
        print(f"ERROR: SPIFFS image not found: {spiffs_image}", file=sys.stderr)
        return 3

    # Source IDF and flash app+partition table
    flash_cmd = f". $HOME/esp/esp-idf/export.sh && idf.py -C {shlex.quote(project_dir)} flash"
    rc = run_shell(flash_cmd)
    if rc != 0:
        print("ERROR: idf.py flash failed", file=sys.stderr)
        return 4

    # Write spiffs image to offset
    write_cmd = f". $HOME/esp/esp-idf/export.sh && python -m esptool --chip esp32 --port {shlex.quote(args.port)} --baud 460800 write_flash {args.spiffs_offset} {shlex.quote(spiffs_image)}"
    rc = run_shell(write_cmd)
    if rc != 0:
        print("ERROR: esptool write_flash failed", file=sys.stderr)
        return 5

    # Give device a moment to reboot
    time.sleep(0.8)

    # Open serial and send PARTS and FILES, capture responses
    ser = None
    try:
        ser = serial.Serial(args.port, args.baud, timeout=0.2)
    except Exception as e:
        print(f"ERROR: could not open serial port {args.port}: {e}", file=sys.stderr)
        return 6

    def send_and_collect(cmd, wait):
        ser.reset_input_buffer(); ser.reset_output_buffer()
        ser.write(cmd.encode('utf-8') + b"\r\n")
        deadline = time.time() + wait
        lines = []
        while time.time() < deadline:
            try:
                line = ser.readline()
            except Exception:
                break
            if not line:
                time.sleep(0.01)
                continue
            try:
                s = line.decode('utf-8', errors='replace').rstrip('\r\n')
            except Exception:
                s = repr(line)
            lines.append(s)
        return lines

    print("Sending PARTS")
    parts_lines = send_and_collect('PARTS', args.monitor_wait)
    print('\n'.join(parts_lines))

    print("Sending FILES")
    files_lines = send_and_collect('FILES', args.monitor_wait)
    print('\n'.join(files_lines))

    ser.close()

    # Simple validation: look for OK|PARTS|SUMMARY and OK|FILES|SUMMARY
    parts_ok = any(re.search(r"^OK\|PARTS\|SUMMARY\|COUNT=\d+", l) for l in parts_lines)
    files_ok = any(l.startswith('OK|FILES|SUMMARY') for l in files_lines)

    # Parse FILES|ITEM|<name>,<size> lines and look for at least one .wav entry
    file_items = []
    for l in files_lines:
        m = re.search(r"FILES\|ITEM\|([^,]+),(\d+)", l)
        if m:
            file_items.append(m.group(1).strip())
    wav_present = any(name.lower().endswith('.wav') for name in file_items)

    if parts_ok and files_ok and wav_present:
        print("Verification SUCCESS: PARTS/FILES summaries OK and .wav present")
        return 0
    else:
        print("Verification FAILED:")
        if not parts_ok:
            print(" - PARTS did not return OK summary (captured lines):")
            print('\n'.join(parts_lines))
        if not files_ok:
            print(" - FILES did not return OK summary (captured lines):")
            print('\n'.join(files_lines))
        if not wav_present:
            if file_items:
                print(f" - No .wav entries parsed from FILES items (parsed: {file_items})")
            else:
                print(" - No FILES|ITEM lines found to validate .wav presence")
        return 7


if __name__ == '__main__':
    sys.exit(main())
