import os
import pty
import sys
import threading
import subprocess
import time


def _mock_device(master_fd, mac, passkey='1234', initial_delay=0.0):
    """Lightweight mock device for tests. It reads commands from master_fd and writes responses."""
    w = os.fdopen(master_fd, 'wb', buffering=0)

    def write_line(s):
        try:
            w.write((s + '\r\n').encode())
        except Exception:
            return

    # optional initial delay to force watchdog expiry
    time.sleep(initial_delay)

    try:
        while True:
            data = os.read(master_fd, 1024)
            if not data:
                time.sleep(0.01)
                continue
            txt = data.decode(errors='ignore').strip()
            # echo raw receive
            write_line(f"INFO|RAW|RECV|{txt}")
            if txt.startswith('DEBUG MOCK_ON'):
                write_line('OK|DEBUG|MOCK_ON')
            elif txt.startswith('DEBUG MOCK_ADD'):
                parts = txt.split(' ', 2)
                payload = parts[2] if len(parts) >= 3 else ''
                write_line(f'OK|DEBUG|MOCK_ADD|{payload}')
            elif txt.startswith('DEBUG MOCK_PAIR'):
                parts = txt.split(' ', 2)
                payload = parts[2] if len(parts) >= 3 else mac
                write_line(f'EVENT|PAIR|CONFIRM|{payload},{passkey[-4:]}')
                write_line(f'OK|DEBUG|MOCK_PAIR_STARTED|{payload}')
            elif 'CMD|CONFIRM_PIN|1' in txt or txt.startswith('CMD|CONFIRM_PIN|1'):
                write_line(f'OK|CONFIRM_PIN|MOCK_ACCEPTED|{mac}')
                write_line(f'EVENT|PAIR|SUCCESS|{mac}')
                # give harness a moment to read the success event and reply
                time.sleep(0.05)
                break
            elif 'CMD|ENTER_PIN|' in txt:
                write_line(f'OK|ENTER_PIN|MOCK_SENT|{mac}')
                write_line(f'EVENT|PAIR|SUCCESS|{mac}')
                time.sleep(0.05)
                break
            else:
                # ignore unknown
                pass
            time.sleep(0.01)
    finally:
        try:
            w.close()
        except Exception:
            pass


def _run_harness_on_slave(slave_name, watchdog=None, watchdog_source=None, timeout=10, mac='AA:BB:CC:DD:EE:FF', log_dir=None):
    cmd = [sys.executable, 'esp_bt_audio_source/tools/host_pairing_driver.py', '--port', slave_name, '--mac', mac, '--timeout', str(timeout)]
    if log_dir:
        cmd += ['--log-dir', log_dir]
    if watchdog is not None:
        cmd += ['--watchdog', str(watchdog)]
    if watchdog_source is not None:
        cmd += ['--watchdog-source', watchdog_source]

    p = subprocess.run(cmd, capture_output=True, text=True)
    return p.returncode, p.stdout, p.stderr


def test_watchdog_expiry(tmp_path):
    # Create PTY and start mock with a delay so the watchdog will expire (watchdog=1s)
    master, slave = pty.openpty()
    slave_name = os.ttyname(slave)

    t = threading.Thread(target=_mock_device, args=(master, 'AA:BB:CC:DD:EE:FF', '1234', 2.0), daemon=True)
    t.start()

    rc, out, err = _run_harness_on_slave(slave_name, watchdog=1, watchdog_source='rx', timeout=5, log_dir=str(tmp_path))
    # watchdog should have expired -> exit code 2 (timeout)
    assert rc == 2, f"expected watchdog timeout (2), got {rc}\nstdout:{out}\nstderr:{err}"


def test_watchdog_no_expiry(tmp_path):
    # Create PTY and start mock that responds promptly; watchdog should not fire
    master, slave = pty.openpty()
    slave_name = os.ttyname(slave)

    t = threading.Thread(target=_mock_device, args=(master, 'AA:BB:CC:DD:EE:FF', '1234', 0.0), daemon=True)
    t.start()

    rc, out, err = _run_harness_on_slave(slave_name, watchdog=5, watchdog_source='rx', timeout=10, log_dir=str(tmp_path))
    # harness should complete successfully
    assert rc == 0, f"expected success (0), got {rc}\nstdout:{out}\nstderr:{err}"

