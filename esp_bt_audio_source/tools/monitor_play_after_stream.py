#!/usr/bin/env python3
"""
Monitor serial, wait for audio streaming start, then reinit I2S and PLAY WAV.

Usage: python3 tools/monitor_play_after_stream.py <serial-port> <log-file> [--baud 115200] [--i2s_pins 26,25,22,-1] [--wav /spiffs/worker_long_norm.wav]

This script reads the serial port, writes output to the specified log file, and when it sees the
text "Started audio streaming" (or a close variant) it sends the following CLI sequence:
  STOP
  I2S_CONFIG <pins>
  AUDIO_DIAG_PROBE ARM 64
  PLAY <wav>
  AUDIO_DIAG_PROBE DUMP
  AUDIO_DIAG_SUMMARY

It then continues to capture for an additional timeout period and exits.
"""
import argparse
import re
import sys
import time
from datetime import datetime

try:
    import serial
except Exception:
    print("pyserial required. Install with: python3 -m pip install --user pyserial", file=sys.stderr)
    sys.exit(2)

def timestamp():
    return datetime.now().isoformat()

def main():
    p = argparse.ArgumentParser()
    p.add_argument('port')
    p.add_argument('logfile')
    p.add_argument('--baud', default=115200, type=int)
    p.add_argument('--i2s_pins', default='26,25,22,-1')
    p.add_argument('--wav', default='/spiffs/worker_long_norm.wav')
    p.add_argument('--wait_timeout', default=120, type=int, help='seconds to wait for streaming start')
    p.add_argument('--connect', default=None, help='MAC to send as CONNECT <MAC> before waiting')
    p.add_argument('--force_connect_send', action='store_true', help='If set, send the command sequence immediately after issuing CONNECT (useful when device is already connected)')
    p.add_argument('--beep_ms', default=None, type=int, help='If set, send BEEP <ms> after --beep_delay seconds following PLAY')
    p.add_argument('--beep_delay', default=2, type=float, help='Seconds to wait after PLAY before sending BEEP')
    p.add_argument('--only_beep', action='store_true', help='If set, send only BEEP (skip PLAY and related probes)')
    p.add_argument('--post_capture', default=30, type=int, help='seconds to capture after issuing commands')
    args = p.parse_args()

    ser = serial.Serial(args.port, args.baud, timeout=1)
    print(f"Opened {args.port} @ {args.baud}")
    # broaden patterns and ignore ANSI color codes
    pat = re.compile(r"Started audio streaming|Audio streaming started|audio streaming started|Started streaming|Audio state changed: 1|Auto-start after connect", re.IGNORECASE)
    ansi_re = re.compile(r'\x1B\[[0-?]*[ -/]*[@-~]')

    def send_sequence():
        if args.only_beep:
            # For BEEP-only runs send a minimal sequence: STOP, optional I2S_CONFIG,
            # then BEEP. This avoids triggering PLAY-related code paths.
            cmds = [
                'STOP',
            ]
            if args.i2s_pins:
                cmds.append(f'I2S_CONFIG {args.i2s_pins}')
            for c in cmds:
                ser.write((c + '\n').encode('utf-8'))
                fh.write(f"# SENT: {c} at {timestamp()}\n")
                fh.flush()
                time.sleep(0.2)
            # Send beep command (default 200ms if not provided)
            beep_len = args.beep_ms if args.beep_ms is not None else 200
            beep_cmd = f'BEEP {beep_len}'
            ser.write((beep_cmd + '\n').encode('utf-8'))
            fh.write(f"# SENT: {beep_cmd} at {timestamp()}\n")
            fh.flush()
            time.sleep(0.2)
        else:
            cmds = [
                'STOP',
                f'I2S_CONFIG {args.i2s_pins}',
                'AUDIO_DIAG_PROBE ARM 64',
                f'PLAY {args.wav}',
                'AUDIO_DIAG_PROBE DUMP',
                'AUDIO_DIAG_SUMMARY',
            ]
            for c in cmds:
                ser.write((c + '\n').encode('utf-8'))
                fh.write(f"# SENT: {c} at {timestamp()}\n")
                fh.flush()
                time.sleep(0.2)
            # Optionally send a BEEP after a delay (pipeline prefill)
            if args.beep_ms is not None:
                time.sleep(args.beep_delay)
                beep_cmd = f'BEEP {args.beep_ms}'
                ser.write((beep_cmd + '\n').encode('utf-8'))
                fh.write(f"# SENT: {beep_cmd} at {timestamp()}\n")
                fh.flush()
                time.sleep(0.2)
        post_deadline = time.time() + args.post_capture
        while time.time() < post_deadline:
            l = ser.readline().decode('utf-8', errors='replace')
            if l:
                fh.write(f"{timestamp()} {l}")
                fh.flush()
        fh.write(f"# Post-capture complete at {timestamp()}\n")

    # (CONNECT handling moved into the capture context where `fh` is available)


    with open(args.logfile, 'w', buffering=1) as fh:
        fh.write(f"# Monitor started at {timestamp()} on {args.port}\n")
        fh.flush()
        # Optionally initiate a connect from the device under test (we need fh open to log responses)
        if args.connect:
            cmd = f'CONNECT {args.connect}'
            ser.write((cmd + '\n').encode('utf-8'))
            print(f"Sent: {cmd}")
            fh.write(f"# SENT: {cmd} at {timestamp()}\n")
            fh.flush()
            # If requested, force sending the command sequence immediately
            if args.force_connect_send:
                fh.write(f"# force_connect_send enabled; sending commands immediately at {timestamp()}\n")
                fh.flush()
                print("force_connect_send enabled; issuing commands immediately...")
                send_sequence()
                ser.close()
                return
            # watch for an "already connected" or ERR|CONNECT|FAILED| echo and send immediately
            immediate_deadline = time.time() + 3.0
            immediate = False
            while time.time() < immediate_deadline:
                try:
                    pre = ser.readline().decode('utf-8', errors='replace')
                except Exception:
                    pre = ''
                if pre:
                    fh.write(f"{timestamp()} {pre}")
                    fh.flush()
                    low = pre.lower()
                    if 'bt_connect - bt_connect already connected' in low or 'already connected' in low or 'err|connect|failed|' in low:
                        immediate = True
                        break
            if immediate:
                fh.write(f"# Detected already-connected after CONNECT; sending commands immediately at {timestamp()}\n")
                print("Already connected detected; issuing commands immediately...")
                send_sequence()
                ser.close()
                return

        start_time = time.time()
        found = False
        last_line = ''
        while True:
            line = ser.readline().decode('utf-8', errors='replace')
            if not line:
                # keep waiting until timeout
                if (time.time() - start_time) > args.wait_timeout and not found:
                    fh.write(f"# TIMEOUT waiting for streaming start after {args.wait_timeout}s\n")
                    print("Timeout waiting for streaming start; exiting.")
                    break
                continue
            # strip ANSI sequences for matching and for clearer logs
            clean = ansi_re.sub('', line)
            ts = timestamp()
            fh.write(f"{ts} {line}")
            last_line = line
            # flush to disk quickly
            fh.flush()
            if not found and pat.search(clean):
                found = True
                fh.write(f"# Detected streaming-start at {timestamp()}\n")
                print("Detected streaming-start; issuing commands...")
                time.sleep(0.5)
                send_sequence()
                print("Post-capture complete; exiting.")
                break

    ser.close()

if __name__ == '__main__':
    main()
