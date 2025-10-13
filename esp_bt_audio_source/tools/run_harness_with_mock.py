#!/usr/bin/env python3
"""
Run the pairing harness in a software-only mock mode for CI.

This script creates a pseudo-terminal pair. It runs a tiny mock device on the
master side which simulates the command interface and deterministic pairing
sequence, while invoking the real `host_pairing_driver.py` against the slave
side. The harness will see the master side messages as if they came from a real
device.

Usage:
  python3 esp_bt_audio_source/tools/run_harness_with_mock.py --mac AA:BB:CC:DD:EE:FF --timeout 10

This is intended for CI usage so the workflow can be executed on cloud runners
without serial hardware.
"""
import argparse
import os
import pty
import sys
import time
import threading
import subprocess


def mock_device(master_fd, mac, passkey='1234', delay=0.5):
    """Simple mock device that speaks the same protocol used by the firmware."""
    w = os.fdopen(master_fd, 'wb', buffering=0)

    def write_line(s):
        w.write((s + '\r\n').encode())
        # small flush delay
        time.sleep(0.01)
    try:
        # initial idle log (reduced verbosity to avoid flooding monitors)
        # The firmware previously printed zero-byte read lines; this mock used to
        # emulate that, but it makes serial logs noisy. Emit a single READY line
        # instead so test harnesses can detect the mock is up without flooding.
        write_line('INFO|MOCK|READY')
        time.sleep(delay)
        # respond to DEBUG MOCK_ON / MOCK_ADD / MOCK_PAIR sequence
        # Simulate acknowledging MOCK_ON
        # Consume any input without assigning to a local variable
        os.read(master_fd, 1024)
    except Exception:
        pass

    # loop and interact
    try:
        while True:
            # read input line (blocking)
            try:
                data = os.read(master_fd, 1024)
            except OSError:
                break
            if not data:
                time.sleep(0.05)
                continue
            txt = data.decode(errors='ignore')
            txt = txt.strip()
            # echo raw receive
            write_line(f"INFO|RAW|RECV|{txt}")
            if txt.startswith('DEBUG MOCK_ON'):
                write_line('OK|DEBUG|MOCK_ON')
            elif txt.startswith('DEBUG MOCK_ADD'):
                # echo OK
                # e.g. DEBUG MOCK_ADD AA:BB:CC:DD:EE:FF,MockDevice
                parts = txt.split(' ', 2)
                if len(parts) >= 3:
                    payload = parts[2]
                    write_line(f'OK|DEBUG|MOCK_ADD|{payload}')
                else:
                    write_line('ERR|DEBUG|MOCK_ADD_MISSING')
            elif txt.startswith('DEBUG MOCK_PAIR'):
                # Emit CONFIRM event with passkey
                parts = txt.split(' ', 2)
                payload = parts[2] if len(parts) >= 3 else mac
                # Emit confirm event
                # passkey: send last two bytes portion
                write_line(f'EVENT|PAIR|CONFIRM|{payload},{passkey[-4:]}')
                write_line(f'OK|DEBUG|MOCK_PAIR_STARTED|{payload}')
            elif 'CMD|CONFIRM_PIN|1' in txt or txt.startswith('CMD|CONFIRM_PIN|1'):
                # simulate accepted
                # extract mac if present in previous event; for simplicity just use provided mac
                write_line(f'OK|CONFIRM_PIN|MOCK_ACCEPTED|{mac}')
                write_line(f'EVENT|PAIR|SUCCESS|{mac}')
                # done
                break
            elif 'CMD|ENTER_PIN|' in txt:
                # simulate PIN accepted
                write_line(f'OK|ENTER_PIN|MOCK_SENT|{mac}')
                write_line(f'EVENT|PAIR|SUCCESS|{mac}')
                break
            else:
                # unknown - ignore
                pass
            time.sleep(0.01)
    except Exception:
        pass
    finally:
        try:
            w.close()
        except Exception:
            pass


def run(args):
    master, slave = pty.openpty()
    slave_name = os.ttyname(slave)

    # start mock device thread
    t = threading.Thread(target=mock_device, args=(master, args.mac, args.passkey, 0.1), daemon=True)
    t.start()

    # run the real harness pointing at the slave pty
    cmd = [
        sys.executable,
        'esp_bt_audio_source/tools/host_pairing_driver.py',
        '--port', slave_name,
        '--mac', args.mac,
        '--timeout', str(args.timeout),
        '--log-dir', args.log_dir,
    ]
    print('Running:', ' '.join(cmd))
    p = subprocess.run(cmd, capture_output=False)
    return p.returncode


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--mac', default='AA:BB:CC:DD:EE:FF')
    parser.add_argument('--passkey', default='1234')
    parser.add_argument('--timeout', type=int, default=10)
    parser.add_argument('--log-dir', default='build/pairing_e2e_manual_logs')
    args = parser.parse_args()
    rc = run(args)
    sys.exit(rc)
