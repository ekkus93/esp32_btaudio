#!/usr/bin/env python3
"""
Capture serial (with per-line timestamps) and audio (arecord) simultaneously.
Saves:
 - tmp/play_audio.wav        (audio capture)
 - tmp/play_serial.log       (timestamped serial lines)
 - tmp/play_start_ts.txt     (epoch ms when audio started)

Usage:
  ./tools/capture_audio_serial.py --serial /dev/ttyUSB0 --baud 115200 --seconds 30

Requires: Python 3, pyserial, arecord (ALSA) available on PATH.
"""
import argparse
import subprocess
import sys
import time
import threading
from datetime import datetime

try:
    import serial
except Exception as e:
    print("This script requires pyserial. Install with: pip install pyserial")
    raise


def epoch_ms():
    return int(time.time() * 1000)


def serial_reader(ser, out_path, stop_event):
    with open(out_path, 'wb') as f:
        while not stop_event.is_set():
            try:
                line = ser.readline()
            except Exception:
                continue
            if not line:
                continue
            ts = epoch_ms()
            # Write timestamped line
            try:
                f.write(f"{ts} ".encode('utf-8'))
                f.write(line)
                f.flush()
            except Exception:
                pass


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--serial', default='/dev/ttyUSB0')
    p.add_argument('--baud', type=int, default=115200)
    p.add_argument('--seconds', type=int, default=30)
    p.add_argument('--audio-file', default='tmp/play_audio.wav')
    p.add_argument('--serial-log', default='tmp/play_serial.log')
    p.add_argument('--start-ts-file', default='tmp/play_start_ts.txt')
    p.add_argument('--arecord-device', default=None, help='arecord -D device name (optional)')
    p.add_argument('--auto-play', action='store_true', help='Send PLAY <file> to serial after SYNC to start playback')
    p.add_argument('--play-file', default='worker_long_norm.wav', help='Filename to send with PLAY when --auto-play is used')
    p.add_argument('--play-delay-ms', type=int, default=50, help='Delay after audio start before sending PLAY (ms)')
    args = p.parse_args()

    # Ensure output dir
    import os
    os.makedirs(os.path.dirname(args.audio_file), exist_ok=True)
    os.makedirs(os.path.dirname(args.serial_log), exist_ok=True)

    # Open serial
    try:
        ser = serial.Serial(args.serial, args.baud, timeout=1)
    except Exception as e:
        print(f"Failed to open serial {args.serial}: {e}")
        sys.exit(2)

    stop_event = threading.Event()
    thr = threading.Thread(target=serial_reader, args=(ser, args.serial_log, stop_event), daemon=True)
    thr.start()

    # Start audio capture
    arecord_cmd = ['arecord', '-f', 'S16_LE', '-r', '44100']
    if args.arecord_device:
        arecord_cmd += ['-D', args.arecord_device]
    arecord_cmd += ['-d', str(args.seconds), args.audio_file]

    # Small sleep to let serial reader warm up
    time.sleep(0.1)

    # Record start timestamp (just before starting audio) and send SYNC over serial
    start_ts = epoch_ms()
    with open(args.start_ts_file, 'w') as f:
        f.write(str(start_ts) + '\n')
    try:
        ser.write((f"SYNC_TS:{start_ts}\n").encode('utf-8'))
    except Exception:
        pass

    print(f"Starting audio capture ({args.seconds}s) -> {args.audio_file}")
    print(f"Serial logging -> {args.serial_log}")
    print(f"Start TS written to {args.start_ts_file}: {start_ts}")

    # Start audio capture in background so we can send PLAY while recording
    try:
        p_audio = subprocess.Popen(arecord_cmd)
    except FileNotFoundError:
        print("arecord not found on PATH. Install ALSA utils.")
        stop_event.set()
        thr.join(timeout=1)
        ser.close()
        sys.exit(2)

    # Optionally send PLAY command shortly after audio capture starts so the recording includes playback
    if args.auto_play:
        # small delay to allow arecord to initialize
        time.sleep(args.play_delay_ms / 1000.0)
        play_cmd = f"PLAY {args.play_file}\n"
        try:
            ser.write(play_cmd.encode('utf-8'))
            print(f"Sent: {play_cmd.strip()}")
        except Exception as e:
            print(f"Failed to send PLAY command: {e}")

    try:
        p_audio.wait()
    except KeyboardInterrupt:
        p_audio.terminate()
        p_audio.wait()

    # Allow a small grace window for serial lines to be flushed
    time.sleep(0.2)
    stop_event.set()
    thr.join(timeout=1)
    ser.close()
    print("Capture complete.")


if __name__ == '__main__':
    main()
