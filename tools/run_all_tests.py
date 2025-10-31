#!/usr/bin/env python3
"""
run_all_tests.py - run host CTest bundle and on-device Unity suites and collect canonical logs

Usage examples:
  # run host tests only
  python3 tools/run_all_tests.py --no-device

  # run device suites only (flash + monitor via run_unity.py)
  python3 tools/run_all_tests.py --no-host --port /dev/ttyUSB0 --timeout 300

  # run everything, sourcing ESP-IDF env first
  python3 tools/run_all_tests.py --source-idf "$HOME/esp/esp-idf/export.sh"

This script intentionally shells device-related commands through `bash -lc` when
`--source-idf` is provided so callers can pass the path to their `export.sh`.

It writes a summary JSON to `tmp/run_all_tests_summary.json` in the repository root
and leaves per-suite canonical captures in the test project `build/one_run_unity.log`
files (created by the existing `tools/run_unity.py`).
"""

from __future__ import annotations

import argparse
import json
import os
import shlex
import subprocess
import sys
import time
import pty
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
TMP_DIR = ROOT / "tmp"
TMP_DIR.mkdir(exist_ok=True)


def run_cmd(cmd, cwd=None, env=None, shell=False, timeout=None):
    print(f"RUN: {' '.join(cmd) if isinstance(cmd, (list,tuple)) else cmd}")
    try:
        if shell:
            res = subprocess.run(cmd, cwd=cwd, env=env, shell=True, check=False, timeout=timeout, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        else:
            res = subprocess.run(cmd, cwd=cwd, env=env, check=False, timeout=timeout, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        return res.returncode, res.stdout
    except subprocess.TimeoutExpired as e:
        return 124, getattr(e, 'output', '') or f"TIMEOUT after {timeout}s"


def run_with_pty(cmd, shell=False, timeout=None, capture_path: str | None = None):
    """Run a command attached to a pseudo-tty, streaming output to this process' stdout.

    If capture_path is provided, bytes emitted by the child are appended to that
    file (binary mode) while also being streamed to this process' stdout.

    Returns (rc, output) where output is an empty string (we stream to stdout
    and persist to capture_path instead of returning a possibly-large buffer).
    """
    if shell:
        args = ["bash", "-lc", cmd]
    else:
        args = cmd

    master_fd, slave_fd = pty.openpty()
    capture_fh = None
    try:
        if capture_path:
            # open in append-binary mode so multiple invocations don't truncate unexpectedly
            capture_fh = open(capture_path, "ab")
        proc = subprocess.Popen(args, stdin=slave_fd, stdout=slave_fd, stderr=slave_fd, close_fds=True)
        os.close(slave_fd)
        # read and stream until EOF
        try:
            while True:
                try:
                    data = os.read(master_fd, 1024)
                except OSError:
                    break
                if not data:
                    break
                # write raw bytes to stdout
                try:
                    sys.stdout.buffer.write(data)
                    sys.stdout.buffer.flush()
                except Exception:
                    # best-effort: ignore streaming errors
                    pass
                if capture_fh:
                    try:
                        capture_fh.write(data)
                        capture_fh.flush()
                    except Exception:
                        pass
        except OSError:
            pass
        try:
            proc.wait(timeout=timeout)
            rc = proc.returncode
        except subprocess.TimeoutExpired:
            proc.kill()
            rc = 124
    finally:
        try:
            os.close(master_fd)
        except Exception:
            pass
        if capture_fh:
            try:
                capture_fh.close()
            except Exception:
                pass
    return rc, ''


def run_host_tests(root: Path, build_dir_name: str = "build_host_tests", jobs: int = 0) -> dict:
    host_dir = root / "esp_bt_audio_source" / "test" / "host_test"
    build_dir = host_dir / build_dir_name
    build_dir.mkdir(parents=True, exist_ok=True)
    summary = {"host": {"configured": False, "build": False, "ctest_rc": None, "ctest_output": None, "lasttest_path": None}}

    # configure
    rc, out = run_cmd(["cmake", ".."], cwd=str(build_dir))
    summary["host"]["configured"] = (rc == 0)
    summary["host"]["configure_output"] = out

    # build
    build_cmd = ["cmake", "--build", "."]
    if jobs and jobs > 1:
        build_cmd += ["--", f"-j{jobs}"]
    rc, out = run_cmd(build_cmd, cwd=str(build_dir))
    summary["host"]["build"] = (rc == 0)
    summary["host"]["build_output"] = out

    # run ctest
    rc, out = run_cmd(["ctest", "--output-on-failure"], cwd=str(build_dir))
    summary["host"]["ctest_rc"] = rc
    summary["host"]["ctest_output"] = out

    # try to locate LastTest.log
    lasttest = build_dir / "Testing" / "Temporary" / "LastTest.log"
    if lasttest.exists():
        summary["host"]["lasttest_path"] = str(lasttest)
    else:
        # sometimes builds put host tests in other directories; attempt common alternates
        alternates = [root / "esp_bt_audio_source" / "test" / "host_test" / "build-host",
                      root / "test" / "host_test" / "build_host_tests"]
        for alt in alternates:
            p = alt / "Testing" / "Temporary" / "LastTest.log"
            if p.exists():
                summary["host"]["lasttest_path"] = str(p)
                break

    # persist ctest output
    outpath = TMP_DIR / "host_ctest_output.log"
    outpath.write_text(out)
    summary["host"]["ctest_log"] = str(outpath)
    return summary


def run_device_suite(project_root: Path, runner_script: Path, port: str, timeout: int, source_idf: str | None) -> dict:
    # runner_script is a path to tools/run_unity.py inside the repo
    proj = project_root.resolve()
    summary = {"project": str(proj), "rc": None, "output_file": None, "stdout": None}
    # prepare per-suite runner stdout capture path (we'll tee pty output here)
    name = proj.name
    capture_outpath = TMP_DIR / f"runner_{name}_stdout.log"
    # start fresh
    try:
        capture_outpath.parent.mkdir(parents=True, exist_ok=True)
        if capture_outpath.exists():
            capture_outpath.unlink()
    except Exception:
        pass

    # Build the invocation
    cmd = [sys.executable, str(runner_script), "--project-root", str(proj), "--port", port, "--timeout", str(timeout)]

    # If caller requested sourcing the IDF export, run under bash -lc to source first
    # Use an interactive/non-capturing subprocess.run here so the runner can attach
    # to the terminal/pty and block until completion (the runner writes canonical
    # logs itself which we then collect). We still catch timeouts and return codes.
    if source_idf:
        # create a single shell command
        shell_cmd = f". {shlex.quote(source_idf)} >/dev/null 2>&1 && {shlex.join([shlex.quote(p) for p in cmd])}"
        # run under a pty so the monitor/runner can work interactively; tee output to capture_outpath
        rc, out = run_with_pty(shell_cmd, shell=True, timeout=timeout + 60, capture_path=str(capture_outpath))
    else:
        # run under a pty to allow idf.py monitor and other interactive operations; tee output
        rc, out = run_with_pty(cmd, shell=False, timeout=timeout + 60, capture_path=str(capture_outpath))

    summary["rc"] = rc
    summary["stdout"] = out

    # find canonical capture
    one_run = proj / "build" / "one_run_unity.log"
    if one_run.exists():
        summary["output_file"] = str(one_run)
    else:
        # if runner stores elsewhere, try to find idf monitor captures
        alt = proj / "build" / "log"
        if alt.exists():
            # pick most recent file containing "one_run_unity" or any .log
            logs = sorted([p for p in alt.iterdir() if p.suffix in (".log", "")], key=os.path.getmtime)
            if logs:
                summary["output_file"] = str(logs[-1])

    # runner output was teed to capture_outpath by run_with_pty
    summary["runner_stdout_log"] = str(capture_outpath)
    return summary


def aggregate_summary(root: Path) -> dict:
    # If tools/aggregate_unity.py exists, prefer to call it to build canonical summary
    agg = {"generated_at": time.asctime(), "by_file": {}}
    agg_script = root / "tools" / "aggregate_unity.py"
    outpath = TMP_DIR / "canonical_unity_summary.json"
    if agg_script.exists():
        rc, out = run_cmd([sys.executable, str(agg_script), "--output", str(outpath)])
        if rc == 0 and outpath.exists():
            try:
                return json.loads(outpath.read_text())
            except Exception:
                pass
    # fallback: scan canonical locations in repo for numeric summary lines
    import re
    pat = re.compile(r"(\d+)\s+Tests\s+(\d+)\s+Failures\s+(\d+)\s+Ignored")
    files = [root / "esp_bt_audio_source" / "test_app" / "build" / "one_run_unity.log",
             root / "esp_bt_audio_source" / "test_app2" / "build" / "one_run_unity.log",
             root / "esp_bt_audio_source" / "test_app_audio" / "build" / "one_run_unity.log"]
    by_file = {}
    for f in files:
        if f.exists():
            txt = f.read_text()
            m = pat.search(txt)
            if m:
                by_file[str(f)] = {"tests": int(m.group(1)), "failures": int(m.group(2)), "ignored": int(m.group(3))}
    agg = {"generated_at": time.asctime(), "by_file": by_file}
    outpath.write_text(json.dumps(agg, indent=2))
    return agg


def parse_flash_time_from_log(path: Path) -> float:
    """Return the duration of the largest esptool write in the provided log.

    We only care about the flash stage for the main app image; bootloader and
    table writes are much smaller, so we choose the write with the largest byte
    count. Returns 0.0 on parse failure.
    """
    try:
        txt = path.read_text(errors="ignore")
    except Exception:
        return 0.0

    # capture both byte count and duration: "Wrote 841392 bytes ... in 11.6 seconds"
    pat = re.compile(
        r"Wrote\s+([0-9,]+)\s+bytes[\s\S]*?in\s+([0-9]+(?:\.[0-9]+)?)\s+seconds",
        re.IGNORECASE,
    )

    best_duration = 0.0
    best_bytes = -1
    for match in pat.finditer(txt):
        byte_str, duration_str = match.groups()
        try:
            byte_count = int(byte_str.replace(",", ""))
            duration = float(duration_str)
        except Exception:
            continue
        if byte_count > best_bytes:
            best_bytes = byte_count
            best_duration = duration

    return best_duration


def main(argv: list[str] | None = None):
    p = argparse.ArgumentParser(description="Run host + on-device unit tests and collect logs")
    p.add_argument("--port", default="/dev/ttyUSB0", help="Serial port for flashing (device runs)")
    p.add_argument("--timeout", type=int, default=300, help="Timeout per device suite in seconds")
    p.add_argument("--no-host", action="store_true", help="Skip host CTest run")
    p.add_argument("--no-device", action="store_true", help="Skip device Unity runs")
    p.add_argument("--source-idf", help="Path to ESP-IDF export.sh to source before device commands")
    p.add_argument("--jobs", type=int, default=0, help="Parallel jobs for host build (passed to cmake --build)")
    args = p.parse_args(argv)

    report = {"host": None, "devices": {}, "aggregate": None}

    # record overall orchestration start (use float for higher resolution)
    report["start_epoch"] = time.time()

    # Host tests
    if not args.no_host:
        print("\n== Running host tests ==")
        report["host"] = run_host_tests(ROOT, jobs=args.jobs)
    else:
        print("Skipping host tests (--no-host)")

    # Device suites
    if not args.no_device:
        print("\n== Running on-device Unity suites ==")
        runner = ROOT / "tools" / "run_unity.py"
        suites = [ROOT / "esp_bt_audio_source" / "test_app",
                  ROOT / "esp_bt_audio_source" / "test_app2",
                  ROOT / "esp_bt_audio_source" / "test_app_audio"]
        for s in suites:
            print(f"\n-- Suite: {s.name} --")
            # ensure we use the in-tree runner inside esp_bt_audio_source if present
            in_tree_runner = ROOT / "esp_bt_audio_source" / "tools" / "run_unity.py"
            if in_tree_runner.exists():
                runner_to_use = in_tree_runner
            else:
                runner_to_use = runner
            # record per-suite start/end epoch to measure execution time (higher resolution)
            start_epoch = time.time()
            print(f"START_EPOCH: {start_epoch:.3f}")
            report["devices"][s.name] = run_device_suite(s, runner_to_use, args.port, args.timeout, args.source_idf)
            end_epoch = time.time()
            print(f"END_EPOCH: {end_epoch:.3f}")
            # attach timestamps and duration to the suite summary
            report["devices"][s.name]["start_epoch"] = start_epoch
            report["devices"][s.name]["end_epoch"] = end_epoch
            report["devices"][s.name]["duration_seconds"] = end_epoch - start_epoch
            # attempt to extract flash/write time from canonical output and compute test-only time
            try:
                of = report["devices"][s.name].get("output_file")
                flash_seconds = 0.0
                if of:
                    flash_seconds = parse_flash_time_from_log(Path(of))
                # clamp flash duration to total duration and compute test-only time
                total_dur = report["devices"][s.name]["duration_seconds"] or 0.0
                if flash_seconds > total_dur and total_dur > 0:
                    flash_seconds = total_dur
                test_seconds = total_dur - flash_seconds
                if test_seconds < 0:
                    test_seconds = 0.0
                report["devices"][s.name]["flash_time_seconds"] = flash_seconds
                report["devices"][s.name]["test_run_seconds"] = test_seconds
            except Exception:
                # best-effort: don't fail the whole run if parsing fails
                report["devices"][s.name]["flash_time_seconds"] = 0.0
                report["devices"][s.name]["test_run_seconds"] = report["devices"][s.name]["duration_seconds"]
    else:
        print("Skipping device suites (--no-device)")

    # Aggregate
    print("\n== Aggregating results ==")
    report["aggregate"] = aggregate_summary(ROOT)

    outjson = TMP_DIR / "run_all_tests_summary.json"
    outjson.write_text(json.dumps(report, indent=2))
    print(f"Wrote summary to {outjson}")
    # record overall end epoch, duration, and update summary file
    report["end_epoch"] = time.time()
    report["duration_seconds"] = report["end_epoch"] - report["start_epoch"]
    outjson.write_text(json.dumps(report, indent=2))
    print("Done.")


if __name__ == '__main__':
    main()
