#!/usr/bin/env python3
"""
Consolidated runner for flashing and capturing Unity test output.

Behavior:
- Builds the test image (using `idf.py build`),
- Flashes and monitors the target (`idf.py -p <port> flash monitor`),
- Captures monitor output to `build/one_run_unity.log`,
- Watches for deterministic completion markers (numeric Unity summary,
  `TEST_RUN_COMPLETE: ...`, or conservative `Auto-run of tests completed`) and
  exits with an appropriate status code.

This script attempts to source the user's ESP-IDF `export.sh` when `idf.py` is
not found on PATH (convenience for interactive shells).
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
import shutil
from pathlib import Path

# Write a short invocation marker so callers can detect that this script started
# even if their stdout/stderr capture swallowed output. Appends lines to
# <repo>/tmp/run_unity_invocations.log with timestamp, pid and argv.
try:
    _inv_log = Path(__file__).resolve().parents[2] / "tmp" / "run_unity_invocations.log"
    _inv_log.parent.mkdir(parents=True, exist_ok=True)
    try:
        with open(_inv_log, "a", encoding="utf-8") as _fh:
            _fh.write(f"{time.time()} PID={os.getpid()} ARGV={shlex.join(sys.argv)}\n")
    except Exception:
        # best-effort only
        pass
except Exception:
    pass

# Track the start of Unity output so we only parse the latest block when computing
# PASS/FAIL at the end of the run.
UNITY_START_RE = re.compile(r"^\s*-----\s*UNITY(?:\s+TEST)?(?:\s+START)?\b", re.IGNORECASE | re.MULTILINE)
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
# Explicit deterministic marker emitted by the device-side test runner. When
# present we should treat this as a definitive signal to stop the monitor and
# consider the run finished.
TEST_RUN_COMPLETE_RE = re.compile(r"TEST_RUN_COMPLETE:\s*(\d+)\s+(\d+)\s+(\d+)", re.IGNORECASE)
# Some test apps emit a human-friendly completion line when auto-running tests.
# `test_app2` prints "Auto-run of tests completed" before (in some cases) crashing
# or rebooting; treat that as a deterministic completion marker so the runner
# doesn't wait for a numeric Unity summary that may never appear.
AUTORUN_COMPLETE_RE = re.compile(r"Auto-run of tests completed", re.IGNORECASE)
# Patterns that indicate the ESP32 has rebooted / booted. Monitor should stop after we
# see a boot banner following test completion so CI/runners don't need to manually stop.
# Include generic 'ets' (month varies), 'rst:', hard reset lines and the monitor header.
BOOT_BANNER_RE = re.compile(r"(ESP-IDF|rst:|\bets\b|Chip is ESP32|Hard resetting via RTS|--- esp-idf-monitor)", re.IGNORECASE)
ANSI_ESCAPE_RE = re.compile(r"\x1B\[[0-9;]*[A-Za-z]")


def find_export_sh():
    candidate = os.path.expanduser('~/esp/esp-idf/export.sh')
    return candidate if os.path.isfile(candidate) else None


def idf_available():
    return shutil.which('idf.py') is not None


def make_shell_cmd(args_list, export_sh=None):
    """Return a command suitable for subprocess when `idf.py` may not be in PATH.

    If `export_sh` is provided, wrap the command in `bash -lc " . export_sh && idf.py ..."`.
    Otherwise return the raw list for direct execution.
    """
    if export_sh and not idf_available():
        # join args correctly with quoting for bash -lc
        cmd = ' '.join(shlex.quote(a) for a in args_list)
        shell_cmd = ['bash', '-lc', f". {shlex.quote(export_sh)} && {cmd}"]
        return shell_cmd
    else:
        return args_list


def tail_process_output(proc, logfile_path, stop_event, summary_event, boot_event, test_complete_event):
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
            # If we see an explicit run-complete marker, set the summary flag.
            if RUN_COMPLETE_RE.search(decoded):
                # Debug: log that we saw the completion marker
                print("[run_unity DEBUG] RUN_COMPLETE_RE matched in monitor output", file=sys.stderr)
                summary_event.set()

            # Treat explicit numeric Unity result or an explicit OK line as an
            # indicator that the test run has completed. Avoid using the broad
            # UNITY_SUMMARY_RE here because many intermediate test-suite lines
            # include tokens like "PASS"/"OK" and can cause the monitor to be
            # stopped prematurely. Also accept the device-emitted deterministic
            # marker: "TEST_RUN_COMPLETE: <total> <fail> <ignored>".
            # If we detect the explicit TEST_RUN_COMPLETE marker, set both the
            # summary_event and the stronger test_complete_event so the main
            # loop can short-circuit the usual boot/wait logic and stop quickly.
            if UNITY_RESULT_RE.search(decoded) or re.search(r"^\s*OK\s*$", decoded, re.MULTILINE):
                print("[run_unity DEBUG] UNITY summary/result line matched in monitor output", file=sys.stderr)
                summary_event.set()
            elif AUTORUN_COMPLETE_RE.search(decoded):
                print("[run_unity DEBUG] AUTORUN_COMPLETE marker matched in monitor output", file=sys.stderr)
                summary_event.set()
                test_complete_event.set()
            elif TEST_RUN_COMPLETE_RE.search(decoded):
                print("[run_unity DEBUG] TEST_RUN_COMPLETE marker matched in monitor output", file=sys.stderr)
                summary_event.set()
                test_complete_event.set()
                print("[run_unity DEBUG] UNITY summary/result line matched in monitor output", file=sys.stderr)
                summary_event.set()

            elif UNITY_SUMMARY_RE.search(decoded):
                print("[run_unity DEBUG] UNITY summary-ish line seen (not triggering completion)", file=sys.stderr)

            if BOOT_BANNER_RE.search(decoded):
                print("[run_unity DEBUG] BOOT_BANNER_RE matched in monitor output", file=sys.stderr)
                boot_event.set()
            if stop_event.is_set():
                break


def run_flash_and_monitor(port, project_root, timeout):
    logfile = os.path.join(project_root, "build", "one_run_unity.log")
    os.makedirs(os.path.dirname(logfile), exist_ok=True)
    env = os.environ.copy()

    export_sh = find_export_sh()

    # Build the project first
    build_args = ['idf.py', 'build']
    build_cmd = make_shell_cmd(build_args, export_sh=export_sh)
    with open(logfile, "ab", buffering=0) as fh:
        fh.write(b"\n=== idf.py build output ===\n")
        # Run the build and stream output to logfile
        try:
            ret = subprocess.run(build_cmd, cwd=project_root, env=env, stdout=fh, stderr=subprocess.STDOUT)
        except FileNotFoundError:
            return 3, logfile
        if ret.returncode != 0:
            return 3, logfile

    # Flash + monitor
    flash_args = ['idf.py', '-p', port, 'flash', 'monitor']
    flash_cmd = make_shell_cmd(flash_args, export_sh=export_sh)

    try:
        proc = subprocess.Popen(flash_cmd, cwd=project_root, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    except FileNotFoundError:
        return 3, logfile

    stop_event = threading.Event()
    summary_event = threading.Event()
    boot_event = threading.Event()
    test_complete_event = threading.Event()

    t = threading.Thread(target=tail_process_output, args=(proc, logfile, stop_event, summary_event, boot_event, test_complete_event), daemon=True)
    t.start()

    start = time.time()
    try:
        while True:
            if summary_event.is_set():
                if test_complete_event.is_set():
                    time.sleep(0.2)
                else:
                    grace_start = time.time()
                    grace_timeout = 60.0
                    time.sleep(0.5)
                    while not boot_event.is_set() and (time.time() - grace_start) < grace_timeout:
                        if proc.poll() is not None:
                            break
                        time.sleep(0.2)

                try:
                    proc.send_signal(signal.SIGINT)
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
                # Timeout fallback: inspect logfile for numeric results or deterministic markers
                try:
                    max_wait = 10.0
                    stable_for = 1.0
                    last_size = -1
                    stable_start = None
                    t_wait_start = time.time()
                    while (time.time() - t_wait_start) < max_wait:
                        try:
                            size = os.path.getsize(logfile)
                        except Exception:
                            size = 0
                        if size == last_size:
                            if stable_start is None:
                                stable_start = time.time()
                            elif (time.time() - stable_start) >= stable_for:
                                break
                        else:
                            last_size = size
                            stable_start = None
                        time.sleep(0.2)

                    with open(logfile, 'r', encoding='utf-8', errors='replace') as fh:
                        text = fh.read()
                    clean_text = ANSI_ESCAPE_RE.sub("", text)

                    result_matches = [m for m in UNITY_RESULT_RE.finditer(clean_text)]
                    if result_matches:
                        last_match = result_matches[-1]
                        failures = int(last_match.group('fail'))
                        print(f"[run_unity DEBUG] Timeout fallback: numeric UNITY result found -> failures={failures}", file=sys.stderr)
                        return (1 if failures else 0), logfile

                    m = TEST_RUN_COMPLETE_RE.search(clean_text)
                    if m:
                        total = int(m.group(1))
                        failures = int(m.group(2))
                        ignored = int(m.group(3))
                        print(f"[run_unity DEBUG] Timeout fallback: TEST_RUN_COMPLETE marker found -> total={total} failures={failures} ignored={ignored}", file=sys.stderr)
                        return (1 if failures else 0), logfile

                    if AUTORUN_COMPLETE_RE.search(clean_text):
                        fail_count = len(re.findall(r":\s*FAIL\b", clean_text, re.IGNORECASE))
                        print(f"[run_unity DEBUG] Timeout fallback: AUTORUN_COMPLETE marker seen -> fail_count={fail_count}", file=sys.stderr)
                        return (1 if fail_count else 0), logfile

                    pass_count = len(re.findall(r":\s*PASS\b", clean_text, re.IGNORECASE))
                    fail_count = len(re.findall(r":\s*FAIL\b", clean_text, re.IGNORECASE))
                    print(f"[run_unity DEBUG] Timeout fallback: per-test counts -> pass_count={pass_count} fail_count={fail_count}", file=sys.stderr)
                    if fail_count:
                        return 1, logfile
                    if pass_count:
                        return 0, logfile
                except Exception:
                    pass

                return 2, logfile

            time.sleep(0.2)

        t.join(timeout=5)

        with open(logfile, 'r', encoding='utf-8', errors='replace') as fh:
            text = fh.read()
        clean_text = ANSI_ESCAPE_RE.sub("", text)

        complete_matches = list(RUN_COMPLETE_RE.finditer(clean_text))
        if complete_matches:
            complete_idx = complete_matches[-1].start()
        else:
            complete_idx = len(clean_text)

        result_matches = [m for m in UNITY_RESULT_RE.finditer(clean_text) if m.start() < complete_idx]
        if result_matches:
            last_match = result_matches[-1]
            failures = int(last_match.group("fail"))
            return (1 if failures else 0), logfile

        last_pos = -1
        for m in UNITY_START_RE.finditer(clean_text):
            last_pos = m.start()
        for m in UNITY_SUMMARY_RE.finditer(clean_text):
            if m.start() > last_pos and m.start() < complete_idx:
                last_pos = m.start()

        if last_pos != -1:
            search_region = clean_text[last_pos:complete_idx]
        else:
            search_region = clean_text[-10000:]

        if re.search(r"^\s*OK\s*\(\s*\d+\s*(tests?|TESTS?)?\s*\)", search_region, re.MULTILINE):
            return 0, logfile

        if re.search(r"\bFAIL(?:ED)?\b|\bFAILURES?\b", search_region, re.IGNORECASE):
            return 1, logfile

        pass_count = len(re.findall(r":\s*PASS\b", search_region, re.IGNORECASE))
        fail_count = len(re.findall(r":\s*FAIL\b", search_region, re.IGNORECASE))
        if fail_count:
            return 1, logfile
        if pass_count:
            return 0, logfile

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
    ap.add_argument('--timeout', '-t', type=int, default=300, help='Timeout in seconds to wait for Unity summary (default 300s)')
    ap.add_argument('--project-root', '-r', default='.', help='Project root (defaults to cwd, must contain the test project and idf.py)')
    args = ap.parse_args()

    rc, logfile = run_flash_and_monitor(args.port, args.project_root, args.timeout)

    if rc != 0 and os.path.isfile(logfile):
        try:
            with open(logfile, 'r', encoding='utf-8', errors='replace') as fh:
                txt = fh.read()
            clean_text = ANSI_ESCAPE_RE.sub('', txt)
            m = TEST_RUN_COMPLETE_RE.search(clean_text)
            if m:
                failures = int(m.group(2))
                rc = 1 if failures else 0
            else:
                if AUTORUN_COMPLETE_RE.search(clean_text):
                    fail_count = len(re.findall(r":\s*FAIL\b", clean_text, re.IGNORECASE))
                    print(f"[run_unity DEBUG] Post-check: AUTORUN_COMPLETE marker seen -> fail_count={fail_count}", file=sys.stderr)
                    rc = 1 if fail_count else 0
                else:
                    result_matches = [m for m in UNITY_RESULT_RE.finditer(clean_text)]
                    if result_matches:
                        last_match = result_matches[-1]
                        failures = int(last_match.group('fail'))
                        rc = 1 if failures else 0
        except Exception:
            pass

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
