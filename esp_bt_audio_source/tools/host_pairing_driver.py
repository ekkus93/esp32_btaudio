#!/usr/bin/env python3
"""
Host-side pairing test harness.

Behaviors:
 - Accepts CLI args (--port, --baud, --mac, --timeout, --log-dir)
 - Drives deterministic on-device mock pairing via DEBUG commands
 - Replies to EVENT|PAIR lines using the device quick-path (CMD|...)
 - Writes a detailed log and emits a single JSON result to stdout
 - Exit codes: 0=success, 2=timeout/failure, 1=internal error

Usage: python3 tools/host_pairing_driver.py --port /dev/ttyUSB0 --mac AA:BB:CC:DD:EE:FF
"""
import argparse
import json
import os
import sys
import time
import datetime
import serial


def now_ts():
    return datetime.datetime.utcnow().isoformat() + 'Z'


def run_harness(port, baud, mac, timeout, log_dir):
    os.makedirs(log_dir, exist_ok=True)
    ts = datetime.datetime.now().strftime('%Y%m%d-%H%M%S')
    logfile = os.path.join(log_dir, f'pairing_e2e_manual_{ts}.log')

    result = {
        'start_ts': now_ts(),
        'port': port,
        'baud': baud,
        'mac': mac,
        'logfile': logfile,
        'status': 'unknown',
        'reason': None,
        'events': []
    }

    try:
        ser = serial.Serial(port, baud, timeout=1)
    except Exception as e:
        result['status'] = 'error'
        result['reason'] = f'failed_open_port: {e}'
        return result, 1

    with open(logfile, 'w') as f:
        def writeln(s):
            # write line with timestamp
            line = f"{now_ts()} {s}"
            print(s)
            f.write(line + '\n')
            f.flush()
            # also record the raw event for the JSON result
            result['events'].append({'ts': now_ts(), 'line': s})

        try:
            writeln(f'Opened {port} @ {baud}')

            # drain initial input
            time.sleep(0.2)
            while ser.in_waiting:
                line = ser.readline().decode(errors='ignore').strip()
                if line:
                    writeln('RX: ' + line)

            # Enable mock mode and add device
            writeln('TX: DEBUG MOCK_ON')
            ser.write(b'DEBUG MOCK_ON\r\n')
            time.sleep(0.1)
            writeln('TX: DEBUG MOCK_ADD ' + mac + ',MockDevice')
            ser.write(f'DEBUG MOCK_ADD {mac},MockDevice\r\n'.encode())
            time.sleep(0.1)

            # Initiate pairing using debug mock
            writeln('TX: DEBUG MOCK_PAIR ' + mac)
            ser.write(f'DEBUG MOCK_PAIR {mac}\r\n'.encode())

            start = time.time()
            while time.time() - start < timeout:
                line = ser.readline().decode(errors='ignore').strip()
                if not line:
                    continue
                writeln('RX: ' + line)

                # Handle pairing event lines
                if line.startswith('EVENT|PAIR|PIN_REQUEST'):
                    # Device asks for PIN; send quick-path CMD|ENTER_PIN|0000
                    writeln('TX: CMD|ENTER_PIN|0000')
                    ser.write(b'CMD|ENTER_PIN|0000\r\n')
                elif line.startswith('EVENT|PAIR|CONFIRM'):
                    # Format: EVENT|PAIR|CONFIRM|<MAC>,<PASSKEY>
                    parts = line.split('|')
                    if len(parts) >= 4:
                        data = parts[3]
                        writeln('TX: CMD|CONFIRM_PIN|1')
                        ser.write(b'CMD|CONFIRM_PIN|1\r\n')
                elif 'PAIR|SUCCESS' in line or line.startswith('OK|PAIR|SUCCESS'):
                    writeln('Pairing succeeded; exiting')
                    result['status'] = 'success'
                    result['end_ts'] = now_ts()
                    return result, 0
                elif 'PAIR|FAILED' in line or line.startswith('OK|PAIR|FAILED'):
                    writeln('Pairing failed; exiting')
                    result['status'] = 'failed'
                    result['reason'] = 'device_reported_failure'
                    result['end_ts'] = now_ts()
                    return result, 2
        except Exception as e:
            writeln('Error: ' + str(e))
            result['status'] = 'error'
            result['reason'] = f'exception: {e}'
            result['end_ts'] = now_ts()
            ser.close()
            return result, 1
        finally:
            try:
                ser.close()
            except Exception:
                pass
            writeln('Closed serial port')

    # If we fell out of the loop, it was a timeout
    result['status'] = 'timeout'
    result['reason'] = 'no_pairing_event'
    result['end_ts'] = now_ts()
    return result, 2


def parse_args():
    p = argparse.ArgumentParser(description='Host pairing test harness')
    p.add_argument('--port', '-p', default='/dev/ttyUSB0')
    p.add_argument('--baud', '-b', type=int, default=115200)
    p.add_argument('--mac', '-m', default='AA:BB:CC:DD:EE:FF')
    p.add_argument('--timeout', '-t', type=int, default=30)
    p.add_argument('--log-dir', '-l', default='build/pairing_e2e_manual_logs')
    return p.parse_args()


if __name__ == '__main__':
    args = parse_args()
    res, code = run_harness(args.port, args.baud, args.mac, args.timeout, args.log_dir)
    # Print single-line JSON result to stdout for CI parsing
    print(json.dumps(res))
    sys.exit(code)
