#!/usr/bin/env python3
"""
Run flash + monitor and extract Unity summary for esp_bt_audio_source.

Behavior:
- Runs `idf.py -p <port> flash monitor` and captures monitor output to
  `build/one_run_unity.log`.
- Watches the monitor output for Unity summary markers and exits with code:
  0 = all tests passed, 1 = one or more failures, 2 = timeout/no summary found,
  3 = other error.

Usage:
  python tools/run_unity.py --port /dev/ttyUSB0 [--timeout 600]

Notes:
- This script requires Python 3.8+. It shells out to `idf.py` on the project
  directory where it runs. Make sure you run it from `esp_bt_audio_source` or
  pass --project-root.
"""

import argparse
import os
import re
import shlex
import signal
import subprocess
import sys
import threading
import time

# Track the start of Unity output so we only parse the latest block when computing
# PASS/FAIL at the end of the run.
UNITY_START_RE = re.compile(r"^\s*-----\s*UNITY\s*-----", re.IGNORECASE)
# Broad summary detection: Unity prints several different summary markers across
# versions and user code (RUNNING TESTS, TEST SUMMARY, OK, PASS/FAIL, Tests: N)
UNITY_SUMMARY_RE = re.compile(r"(RUNNING TESTS|TESTS? SUMMARY|^\s*OK\b|\bPASS(?:ED)?\b|\bFAIL(?:ED)?\b|Tests:)",
                              re.IGNORECASE | re.MULTILINE)
UNITY_RESULT_RE = re.compile(r"(?P<total>\d+)\s+Tests?\s+(?P<fail>\d+)\s+Failures?\s+(?P<ignored>\d+)\s+Ignored",
                             re.IGNORECASE)
# Final completion markers printed by test_main once *all* suites are finished.
RUN_COMPLETE_RE = re.compile(
    r"(ENTERING IDLE LOOP - TESTS COMPLETE|======== OVERALL TEST SUMMARY ========|----- UNITY TEST COMPLETE|ALL TESTS COMPLETED)",
    re.IGNORECASE)
# Patterns that indicate the ESP32 has rebooted / booted. Monitor should stop after we
# see a boot banner following test completion so CI/runners don't need to manually stop.
# Include generic 'ets' (month varies), 'rst:', hard reset lines and the monitor header.
BOOT_BANNER_RE = re.compile(r"(ESP-IDF|rst:|\bets\b|Chip is ESP32|Hard resetting via RTS|--- esp-idf-monitor)", re.IGNORECASE)
ANSI_ESCAPE_RE = re.compile(r"\x1B\[[0-9;]*[A-Za-z]")


def tail_process_output(proc, logfile_path, stop_event, summary_event, boot_event):
    """Read process stdout line-by-line, write to logfile, and signal when summary seen."""
    with open(logfile_path, "ab", buffering=0) as fh:
        while True:
            line = proc.stdout.readline()
            if not line:
                if proc.poll() is not None:
                    break
                time.sleep(0.05)
                continue
            fh.write(line)
            fh.flush()
            try:
                decoded = line.decode('utf-8', errors='replace')
            except Exception:
                decoded = str(line)

            if RUN_COMPLETE_RE.search(decoded):
                summary_event.set()
            if BOOT_BANNER_RE.search(decoded):
                # if we see a boot banner, signal boot_event — this usually means the
                # device restarted (for example after tests completed and the test_app rebooted)
                boot_event.set()
            if stop_event.is_set():
                break


def run_flash_and_monitor(port, project_root, timeout):
    logfile = os.path.join(project_root, "build", "one_run_unity.log")
    os.makedirs(os.path.dirname(logfile), exist_ok=True)
    # Ensure the test app is built before flashing. Capture build output
    # into the same logfile so CI/triage has a complete trace.
    build_cmd = ["idf.py", "build"]
    env = os.environ.copy()
    with open(logfile, "ab", buffering=0) as fh:
        fh.write(b"\n=== idf.py build output ===\n")
        # Run the build and stream output to logfile
        ret = subprocess.run(build_cmd, cwd=project_root, env=env, stdout=fh, stderr=subprocess.STDOUT)
        if ret.returncode != 0:
            # Build failed; return an error code and the logfile path so caller can inspect
            return 3, logfile

    # Command will run idf.py in the project root. Using -y to stop monitor
    # from prompting to exit on Ctrl-C isn't available; we'll manage the process.
    cmd = ["idf.py", "-p", port, "flash", "monitor"]

    with subprocess.Popen(cmd, cwd=project_root, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT) as proc:
        stop_event = threading.Event()
        summary_event = threading.Event()
        boot_event = threading.Event()

        t = threading.Thread(target=tail_process_output, args=(proc, logfile, stop_event, summary_event, boot_event), daemon=True)
        t.start()

        start = time.time()
        try:
            while True:
                if summary_event.is_set():
                    # We saw the Unity summary — now wait for an expected reboot/boot banner
                    # which commonly follows test-runner completion. If we see the boot banner
                    # within a short grace window, treat that as the run finishing and stop.
                    grace_start = time.time()
                    # Allow a longer grace window for trailing output and for slower devices
                    grace_timeout = 60.0
                    # small sleep to allow trailing output to be flushed
                    time.sleep(0.5)
                    # Wait for boot_event or until grace_timeout expires
                    while not boot_event.is_set() and (time.time() - grace_start) < grace_timeout:
                        if proc.poll() is not None:
                            break
                        time.sleep(0.2)

                    # Attempt a polite shutdown of the monitor process
                    try:
                        proc.send_signal(signal.SIGINT)
                        # give it a moment to exit
                        for _ in range(10):
                            if proc.poll() is not None:
                                break
                            time.sleep(0.1)
                        else:
                            proc.terminate()
                    except Exception:
                        try:
                            proc.terminate()
                        except Exception:
                            pass

                    stop_event.set()
                    break

                if proc.poll() is not None:
                    break

                if timeout and (time.time() - start) > timeout:
                    stop_event.set()
                    try:
                        proc.terminate()
                    except Exception:
                        pass
                    return 2, logfile

                time.sleep(0.2)

            # Wait up to 5s for thread to finish writing
            t.join(timeout=5)

            # Read back logfile and search for final Unity PASS/FAIL
            with open(logfile, 'r', encoding='utf-8', errors='replace') as fh:
                text = fh.read()
            clean_text = ANSI_ESCAPE_RE.sub("", text)

            complete_matches = list(RUN_COMPLETE_RE.finditer(clean_text))
            if complete_matches:
                complete_idx = complete_matches[-1].start()
            else:
                complete_idx = len(clean_text)

            # Direct Unity summary line such as "22 Tests 7 Failures 0 Ignored"
            result_matches = [m for m in UNITY_RESULT_RE.finditer(clean_text) if m.start() < complete_idx]
            if result_matches:
                last_match = result_matches[-1]
                failures = int(last_match.group("fail"))
                return (1 if failures else 0), logfile

            # Look for Unity summary only inside the most-recent Unity-related block
            # (avoid matching unrelated 'FAILED' tokens from other subsystems).
            last_pos = -1
            for m in UNITY_START_RE.finditer(clean_text):
                last_pos = m.start()
            for m in UNITY_SUMMARY_RE.finditer(clean_text):
                if m.start() > last_pos and m.start() < complete_idx:
                    last_pos = m.start()

            # If we saw a Unity block, only inspect a slice following that marker.
            if last_pos != -1:
                search_region = clean_text[last_pos:complete_idx]
            else:
                # fallback: inspect the tail of the logfile (last 10k chars)
                search_region = clean_text[-10000:]

            # Prefer explicit Unity OK pattern like: "OK (3 tests)"
            if re.search(r"^\s*OK\s*\(\s*\d+\s*(tests?|TESTS?)?\s*\)", search_region, re.MULTILINE):
                return 0, logfile

            # Unity may print explicit failure indicators in the same block
            if re.search(r"\bFAIL(?:ED)?\b|\bFAILURES?\b", search_region, re.IGNORECASE):
                return 1, logfile

            # If we see a Tests: N line and no failures in the region, treat as pass.
            if re.search(r"Tests:\s*\d+", search_region) and not re.search(r"\bFAIL(?:ED)?\b", search_region, re.IGNORECASE):
                return 0, logfile

            return 2, logfile

        except KeyboardInterrupt:
            stop_event.set()
            try:
                proc.terminate()
            except Exception:
                pass
            return 3, logfile


def main():
    ap = argparse.ArgumentParser(description="Flash device and capture Unity results to a log file.")
    ap.add_argument('--port', '-p', required=True, help='Serial port, e.g. /dev/ttyUSB0')
    ap.add_argument('--timeout', '-t', type=int, default=600, help='Timeout in seconds to wait for Unity summary (default 600s)')
    ap.add_argument('--project-root', '-r', default='.', help='Project root (defaults to cwd, must contain idf.py)')
    args = ap.parse_args()

    rc, logfile = run_flash_and_monitor(args.port, args.project_root, args.timeout)
    if rc == 0:
        print(f"Unity tests passed — logfile: {logfile}")
    elif rc == 1:
        print(f"Unity tests had failures — logfile: {logfile}")
    elif rc == 2:
        print(f"No Unity summary found within timeout; logfile: {logfile}")
    else:
        print(f"Error running monitor; logfile: {logfile}")
    sys.exit(rc)


if __name__ == '__main__':
    main()
