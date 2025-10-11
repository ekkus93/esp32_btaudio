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
import threading


def now_ts():
    return datetime.datetime.utcnow().isoformat() + 'Z'


def run_harness(port, baud, mac, timeout, log_dir, watchdog=0, watchdog_source='both'):
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
        # last_activity is stored in a mutable container so inner functions can update it
        last_activity = [time.time()]
        # set local copy of configured watchdog source from function parameter
        watch_src = watchdog_source
        stop_event = threading.Event()

        def writeln(s, source='rx'):
            # write line with timestamp
            line = f"{now_ts()} {s}"
            print(s)
            f.write(line + '\n')
            f.flush()
            # also record the raw event for the JSON result
            result['events'].append({'ts': now_ts(), 'line': s})
            # update last activity time depending on configured watchdog_source
            if watch_src == 'both' or watch_src == source:
                last_activity[0] = time.time()

        # watchdog thread: abort if no activity for `watchdog` seconds
        def watchdog_func():
            if watchdog <= 0:
                return
            while not stop_event.is_set():
                now = time.time()
                if now - last_activity[0] > float(watchdog):
                    try:
                        writeln(f'Watchdog expired after {watchdog}s; aborting')
                    except Exception:
                        # best-effort logging
                        pass
                    # mark result as timeout due to watchdog
                    result['status'] = 'timeout'
                    result['reason'] = 'watchdog_expired'
                    result['end_ts'] = now_ts()
                    # signal the main loop to stop
                    stop_event.set()
                    break
                # check twice per second
                time.sleep(0.5)

        wd_thread = None
        if watchdog and float(watchdog) > 0:
            wd_thread = threading.Thread(target=watchdog_func, daemon=True)
            wd_thread.start()

        try:
            writeln(f'Opened {port} @ {baud}')

            # drain initial input
            time.sleep(0.2)
            while ser.in_waiting:
                line = ser.readline().decode(errors='ignore').strip()
                if line:
                    writeln('RX: ' + line)

            # Enable mock mode and add device
            writeln('TX: DEBUG MOCK_ON', source='tx')
            ser.write(b'DEBUG MOCK_ON\r\n')
            time.sleep(0.1)
            writeln('TX: DEBUG MOCK_ADD ' + mac + ',MockDevice', source='tx')
            ser.write(f'DEBUG MOCK_ADD {mac},MockDevice\r\n'.encode())
            time.sleep(0.1)

            # Initiate pairing using debug mock
            writeln('TX: DEBUG MOCK_PAIR ' + mac, source='tx')
            ser.write(f'DEBUG MOCK_PAIR {mac}\r\n'.encode())

            start = time.time()
            while time.time() - start < timeout:
                if stop_event.is_set():
                    # watchdog triggered
                    return result, 2

                line = ser.readline().decode(errors='ignore').strip()
                if not line:
                    continue
                writeln('RX: ' + line)

                # Handle pairing event lines
                if line.startswith('EVENT|PAIR|PIN_REQUEST'):
                    # Device asks for PIN; send quick-path CMD|ENTER_PIN|0000
                    writeln('TX: CMD|ENTER_PIN|0000', source='tx')
                    ser.write(b'CMD|ENTER_PIN|0000\r\n')
                elif line.startswith('EVENT|PAIR|CONFIRM'):
                    # Format: EVENT|PAIR|CONFIRM|<MAC>,<PASSKEY>
                    parts = line.split('|')
                    if len(parts) >= 4:
                        data = parts[3]
                        writeln('TX: CMD|CONFIRM_PIN|1', source='tx')
                        ser.write(b'CMD|CONFIRM_PIN|1\r\n')
                elif 'PAIR|SUCCESS' in line or line.startswith('OK|PAIR|SUCCESS'):
                        writeln('Pairing succeeded; exiting')
                        result['status'] = 'success'
                        result['end_ts'] = now_ts()
                        stop_event.set()
                        return result, 0
                elif 'PAIR|FAILED' in line or line.startswith('OK|PAIR|FAILED'):
                        writeln('Pairing failed; exiting')
                        result['status'] = 'failed'
                        result['reason'] = 'device_reported_failure'
                        result['end_ts'] = now_ts()
                        stop_event.set()
                        return result, 2
        except Exception as e:
            writeln('Error: ' + str(e))
            # Some pyserial/read errors indicate the pseudo-tty closed (EOF); treat as timeout
            msg = str(e)
            if 'device reports readiness to read but returned no data' in msg or 'EOF' in msg:
                result['status'] = 'timeout'
                result['reason'] = 'device_disconnected'
                result['end_ts'] = now_ts()
                try:
                    ser.close()
                except Exception:
                    pass
                return result, 2
            result['status'] = 'error'
            result['reason'] = f'exception: {e}'
            result['end_ts'] = now_ts()
            try:
                ser.close()
            except Exception:
                pass
            return result, 1
        finally:
            try:
                ser.close()
            except Exception:
                pass
            # signal watchdog thread to stop (if running) and join
            try:
                stop_event.set()
                if wd_thread:
                    wd_thread.join(timeout=1)
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
    p.add_argument('--watchdog', '-w', type=int, default=0, help='Watchdog inactivity timeout in seconds; 0 disables watchdog')
    p.add_argument('--watchdog-source', choices=['both', 'rx', 'tx'], default='both', help='What counts as activity for the watchdog: rx (incoming), tx (outgoing), or both')
    return p.parse_args()


if __name__ == '__main__':
    args = parse_args()
    # pass watchdog_source through by setting closure var after creating result
    res, code = run_harness(args.port, args.baud, args.mac, args.timeout, args.log_dir, watchdog=args.watchdog)
    # Print single-line JSON result to stdout for CI parsing
    print(json.dumps(res))
    sys.exit(code)
