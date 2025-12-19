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


def _unlink_artifact(path: Path) -> bool:
    try:
        path.unlink()
        print(f"Removed artifact: {path}")
        return True
    except FileNotFoundError:
        return False
    except IsADirectoryError:
        return False
    except Exception as exc:
        print(f"WARNING: failed to remove {path}: {exc}")
        return False


def cleanup_previous_artifacts(root: Path, remove_host: bool, remove_device: bool) -> None:
    """Remove canonical logs/json outputs from prior runs so fresh artifacts are produced."""
    print("\n== Cleaning previous run artifacts ==")

    # Always clear summary JSON outputs and runner captures in tmp/.
    for candidate in TMP_DIR.glob("run_all_tests_summary*.json"):
        _unlink_artifact(candidate)
    for candidate in TMP_DIR.glob("canonical_unity_summary*.json"):
        _unlink_artifact(candidate)
    for candidate in TMP_DIR.glob("runner_*_stdout.log"):
        _unlink_artifact(candidate)

    if remove_host:
        for candidate in TMP_DIR.glob("host_ctest_output*.log"):
            _unlink_artifact(candidate)

    if remove_device:
        unity_projects = [
            root / "esp_bt_audio_source" / "test" / "test_app",
            root / "esp_bt_audio_source" / "test" / "test_app2",
            root / "esp_bt_audio_source" / "test" / "test_app_audio",
            root / "esp_bt_audio_source" / "test" / "test_app3",
        ]
        for proj in unity_projects:
            _unlink_artifact(proj / "build" / "one_run_unity.log")


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
    
    # After ctest completes, run each host test binary directly to capture Unity case counts
    # (ctest only reports test targets, not per-Unity test cases).
    # Look for executable files named test_* in the build directory.
    def _unity_counts_from_output(stdout: str) -> dict:
        m = re.search(r"(\d+)\s+Tests\s+(\d+)\s+Failures\s+(\d+)\s+Ignored", stdout)
        if not m:
            return {"tests": 0, "failures": 0, "ignored": 0}
        tests = int(m.group(1))
        failures = int(m.group(2))
        ignored = int(m.group(3))
        return {"tests": tests, "failures": failures, "ignored": ignored}

    per_binary = {}
    total_cases = 0
    total_failures = 0
    total_ignored = 0
    try:
        for entry in build_dir.iterdir():
            if not entry.is_file():
                continue
            if not entry.name.startswith("test_"):
                continue
            if not os.access(entry, os.X_OK):
                continue
            rc_bin, out_bin = run_cmd([str(entry)], cwd=str(build_dir))
            counts = _unity_counts_from_output(out_bin)
            per_binary[entry.name] = {
                "rc": rc_bin,
                "stdout": out_bin,
                "tests": counts.get("tests", 0),
                "failures": counts.get("failures", 0),
                "ignored": counts.get("ignored", 0),
            }
            total_cases += counts.get("tests", 0)
            total_failures += counts.get("failures", 0)
            total_ignored += counts.get("ignored", 0)
    except Exception as exc:
        per_binary["_count_error"] = str(exc)

    summary["host"]["case_counts"] = {
        "total": total_cases,
        "failures": total_failures,
        "ignored": total_ignored,
        "per_binary": per_binary,
    }
    return summary


def run_device_suite(project_root: Path, runner_script: Path, port: str, timeout: int, source_idf: str | None, spiffs_image: str | None = None, spiffs_offset: str | None = None, force_spiffs: bool = False) -> dict:
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
    # If caller provided a spiffs image / offset, forward them to the runner
    if spiffs_image:
        cmd += ["--spiffs-image", str(spiffs_image)]
    if spiffs_offset:
        cmd += ["--spiffs-offset", str(spiffs_offset)]
    if force_spiffs:
        cmd += ["--force-spiffs"]

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
    # Prefer the project aggregator if present, then augment with local parsing to ensure counts.
    agg = {"generated_at": time.asctime(), "by_file": {}}
    agg_script = root / "tools" / "aggregate_unity.py"
    outpath = TMP_DIR / "canonical_unity_summary.json"
    if agg_script.exists():
        rc, _ = run_cmd([sys.executable, str(agg_script), "--output", str(outpath)])
        if rc == 0 and outpath.exists():
            try:
                agg = json.loads(outpath.read_text())
            except Exception:
                pass

    if not isinstance(agg, dict):
        agg = {"generated_at": time.asctime(), "by_file": {}}
    if "by_file" not in agg or not isinstance(agg.get("by_file"), dict):
        agg["by_file"] = {}

    # fallback/augmentation: scan canonical locations for numeric summary lines and merge
    import re

    canonical_pat = re.compile(r"(\d+)\s+Tests\s+(\d+)\s+Failures\s+(\d+)\s+Ignored")
    alt_run_pat = re.compile(r"Tests\s*run\s*:\s*(\d+)", re.IGNORECASE)
    alt_failed_pat = re.compile(r"Tests\s*failed\s*:\s*(\d+)", re.IGNORECASE)
    alt_passed_pat = re.compile(r"Tests\s*passed\s*:\s*(\d+)", re.IGNORECASE)

    def parse_log(path: Path) -> dict | None:
        try:
            txt = path.read_text(errors="ignore")
        except Exception:
            return None

        m = canonical_pat.search(txt)
        if m:
            return {"tests": int(m.group(1)), "failures": int(m.group(2)), "ignored": int(m.group(3))}

        m_run = alt_run_pat.search(txt)
        m_failed = alt_failed_pat.search(txt)
        m_passed = alt_passed_pat.search(txt)
        if m_run:
            total = int(m_run.group(1))
            failed = int(m_failed.group(1)) if m_failed else None
            passed = int(m_passed.group(1)) if m_passed else None
            if failed is None and passed is not None:
                failed = total - passed
            if passed is None and failed is not None:
                passed = total - failed
            ignored = total - (passed if passed is not None else 0) - (failed if failed is not None else 0)
            if ignored < 0:
                ignored = 0
            return {"tests": total, "failures": (failed if failed is not None else 0), "ignored": ignored}

        try:
            pass_count = len(re.findall(r":PASS\b", txt))
            fail_count = len(re.findall(r":FAIL\b", txt))
            ignore_count = len(re.findall(r":IGNORE\b", txt))
            total = pass_count + fail_count + ignore_count
            if total > 0:
                return {"tests": total, "failures": fail_count, "ignored": ignore_count}
        except Exception:
            return None
        return None

    files = [root / "esp_bt_audio_source" / "test" / "test_app" / "build" / "one_run_unity.log",
             root / "esp_bt_audio_source" / "test" / "test_app2" / "build" / "one_run_unity.log",
             root / "esp_bt_audio_source" / "test" / "test_app_audio" / "build" / "one_run_unity.log",
             root / "esp_bt_audio_source" / "test" / "test_app3" / "build" / "one_run_unity.log"]

    for f in files:
        if not f.exists():
            continue
        parsed = parse_log(f)
        key = str(f)
        existing = agg["by_file"].get(key, {}) if isinstance(agg.get("by_file"), dict) else {}
        if parsed:
            if not existing or int(existing.get("tests", 0) or 0) == 0:
                agg["by_file"][key] = parsed

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


def count_unity_results(path: Path) -> dict:
    """Count Unity per-test PASS/FAIL/IGNORE tokens as a last-resort fallback.

    Returns a dict: {"tests": N, "failures": F, "ignored": I}
    If the file cannot be read or no tokens found, returns {"tests": 0, "failures": 0, "ignored": 0}
    """
    try:
        txt = path.read_text(errors="ignore")
    except Exception:
        return {"tests": 0, "failures": 0, "ignored": 0}

    import re
    try:
        pass_count = len(re.findall(r":PASS\\b", txt))
        fail_count = len(re.findall(r":FAIL\\b", txt))
        ignore_count = len(re.findall(r":IGNORE\\b", txt))
        total = pass_count + fail_count + ignore_count
        if total == 0:
            return {"tests": 0, "failures": 0, "ignored": 0}
        return {"tests": total, "failures": fail_count, "ignored": ignore_count}
    except Exception:
        return {"tests": 0, "failures": 0, "ignored": 0}


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
    overall_failed = False

    cleanup_previous_artifacts(ROOT, remove_host=not args.no_host, remove_device=not args.no_device)

    # record overall orchestration start (use float for higher resolution)
    report["start_epoch"] = time.time()

    # Host tests
    if not args.no_host:
        print("\n== Running host tests ==")
        report["host"] = run_host_tests(ROOT, jobs=args.jobs)
        # run_host_tests historically returned a wrapper dict {"host": {...}}
        # unwrap it if present so downstream code can treat report['host'] as
        # the host-summary dict directly.
        try:
            if isinstance(report["host"], dict) and "host" in report["host"] and isinstance(report["host"]["host"], dict):
                report["host"] = report["host"]["host"]
        except Exception:
            pass
        # Treat host test failures as critical
        try:
            if report["host"].get("ctest_rc") not in (None, 0):
                overall_failed = True
        except Exception:
            pass
    else:
        print("Skipping host tests (--no-host)")

    # Device suites
    if not args.no_device:
        print("\n== Running on-device Unity suites ==")
        runner = ROOT / "tools" / "run_unity.py"
        suites = [
            ROOT / "esp_bt_audio_source" / "test" / "test_app",
            ROOT / "esp_bt_audio_source" / "test" / "test_app2",
            ROOT / "esp_bt_audio_source" / "test" / "test_app_audio",
            ROOT / "esp_bt_audio_source" / "test" / "test_app3",
        ]
        # attempt to detect an in-tree SPIFFS image and partition offset so the
        # runner can flash it before the monitor step. Prefer the canonical
        # location inside esp_bt_audio_source/main/assets/spiffs/spiffs.bin and
        # parse esp_bt_audio_source/partitions.csv for the offset.
        spiffs_image = None
        spiffs_offset = None
        try:
            esp_root = ROOT / "esp_bt_audio_source"
            cand = esp_root / "main" / "assets" / "spiffs" / "spiffs.bin"
            if cand.exists():
                spiffs_image = str(cand)
            # parse partitions.csv for spiffs offset
            part_file = esp_root / "partitions.csv"
            if part_file.exists():
                try:
                    with open(part_file, 'r', encoding='utf-8', errors='ignore') as pf:
                        for line in pf:
                            if 'spiffs' in line.lower():
                                cols = [c.strip() for c in line.split(',')]
                                for c in cols:
                                    if c.lower().startswith('0x'):
                                        spiffs_offset = c
                                        break
                                if spiffs_offset:
                                    break
                except Exception:
                    spiffs_offset = None
        except Exception:
            spiffs_image = None
            spiffs_offset = None
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
            report["devices"][s.name] = run_device_suite(
                s,
                runner_to_use,
                args.port,
                args.timeout,
                args.source_idf,
                spiffs_image=spiffs_image,
                spiffs_offset=spiffs_offset,
                force_spiffs=False,
            )
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
            # Flag any non-zero runner return as critical
            try:
                if report["devices"][s.name].get("rc") not in (None, 0):
                    overall_failed = True
            except Exception:
                pass
    else:
        print("Skipping device suites (--no-device)")

    # Aggregate
    print("\n== Aggregating results ==")
    report["aggregate"] = aggregate_summary(ROOT)

    # Ensure aggregate contains device_totals computed from per-device entries
    try:
        dev_totals = {"tests": 0, "failures": 0, "ignored": 0}
        for dev in report.get("devices", {}).values():
            dev_totals["tests"] += int(dev.get("tests_total", 0) or 0)
            dev_totals["failures"] += int(dev.get("tests_failed", 0) or 0)
            dev_totals["ignored"] += int(dev.get("tests_ignored", 0) or 0)
        if isinstance(report.get("aggregate"), dict):
            report["aggregate"]["device_totals"] = dev_totals
    except Exception:
        pass

    # Extract per-suite numeric summaries (tests / failures / ignored) from
    # the aggregate summary and attach them to each device entry. Also print
    # a short human-readable summary to stdout for quick inspection.
    try:
        agg = report.get("aggregate", {})
        by_file = agg.get("by_file", {}) if isinstance(agg, dict) else {}
        per_test = agg.get("per_test", []) if isinstance(agg, dict) else []
        # Map aggregate file keys to device projects robustly by resolving
        # relative paths against the repository root and comparing to the
        # recorded project path for each device. This avoids brittle
        # substring-only matching.
        for sname, dev in report.get("devices", {}).items():
            matched = None
            proj_path = None
            try:
                proj_path = Path(dev.get("project", "")).resolve()
            except Exception:
                proj_path = None

            # Prefer a direct match using the device's recorded canonical output
            # file (absolute path) normalized relative to the repo root. This
            # is the most reliable mapping when available.
            output_file = dev.get("output_file")
            if output_file:
                try:
                    rel = str(Path(output_file).resolve().relative_to(ROOT.resolve()))
                except Exception:
                    # fallback to os.path.relpath for older Python or inexact paths
                    try:
                        import os as _os

                        rel = _os.path.relpath(output_file, str(ROOT))
                    except Exception:
                        rel = None
                if rel and rel in by_file:
                    matched = rel
                else:
                    pass
            if not matched:
                for fpath in by_file.keys():
                    # normalize the aggregated key into an absolute path we can reason about
                    try:
                        p = Path(fpath)
                    except Exception:
                        p = None

                    if p is None:
                        # fallback to simple substring check if Path() fails
                        if f"/{sname}/" in fpath or f"{sname}/build/one_run_unity.log" in fpath or sname in fpath:
                            matched = fpath
                            break
                        continue

                    # If the aggregated path is relative, resolve it against the repo root
                    if not p.is_absolute():
                        p_abs = (ROOT / p).resolve()
                    else:
                        try:
                            p_abs = p.resolve()
                        except Exception:
                            p_abs = p

                    # If we have a recorded project path, check whether the canonical
                    # file appears under that project directory (most robust).
                    if proj_path:
                        try:
                            if proj_path == p_abs or proj_path in p_abs.parents:
                                matched = fpath
                                break
                        except Exception:
                            # fall through to name-based checks
                            pass

                    # as a final check, see if the suite name appears as a path component
                    if sname in p.parts:
                        matched = fpath
                        break

            if matched:
                vals = by_file.get(matched, {})
                # Support both flattened and nested aggregator shapes. Prefer
                # the last entry in an "entries" list when present.
                if isinstance(vals, dict) and "entries" in vals and isinstance(vals.get("entries"), list) and vals["entries"]:
                    entry = vals["entries"][-1]
                    tests = int(entry.get("tests", 0))
                    failures = int(entry.get("failures", 0))
                    ignored = int(entry.get("ignored", 0))
                else:
                    tests = int(vals.get("tests", 0))
                    failures = int(vals.get("failures", 0))
                    ignored = int(vals.get("ignored", 0))
                passed = tests - failures - ignored
                if passed < 0:
                    passed = 0
                # attach numeric fields to the device report for downstream
                # consumers and persistence to the summary JSON
                dev["tests_total"] = tests
                dev["tests_passed"] = passed
                dev["tests_failed"] = failures
                dev["tests_ignored"] = ignored
                print(f"Suite {sname}: {passed} passed, {failures} failed, {ignored} ignored (total {tests})")
            else:
                # no canonical capture found for this suite
                print(f"Suite {sname}: no canonical log found to extract test counts; check {dev.get('output_file')}")
                overall_failed = True
        # Fallback: if any device still has zero counts, try deriving counts from
        # the per_test list by counting tests that reference that suite's file.
        try:
            from collections import defaultdict

            per_suite_counts = defaultdict(int)
            for entry in per_test:
                files = entry.get("files", [])
                for f in files:
                    # normalize and check suite name as a path component
                    try:
                        if any(s in Path(f).parts for s in report.get("devices", {}).keys()):
                            # increment for each suite that appears in the file path
                            for sname in report.get("devices", {}).keys():
                                if sname in Path(f).parts:
                                    per_suite_counts[sname] += 1
                    except Exception:
                        # fallback substring check
                        for sname in report.get("devices", {}).keys():
                            if sname in f:
                                per_suite_counts[sname] += 1

            # attach fallback counts where needed
            for sname, dev in report.get("devices", {}).items():
                if dev.get("tests_total", 0) == 0 and per_suite_counts.get(sname, 0) > 0:
                    tests = per_suite_counts[sname]
                    dev["tests_total"] = tests
                    dev["tests_passed"] = tests
                    dev["tests_failed"] = 0
                    dev["tests_ignored"] = 0
                    print(f"Suite {sname}: (fallback) {tests} passed, 0 failed, 0 ignored (total {tests})")
        except Exception:
            pass
    except Exception as exc:
        # best-effort: do not fail orchestration if parsing fails
        print(f"Warning: failed to extract per-suite counts: {exc}")

    outjson = TMP_DIR / "run_all_tests_summary.json"
    outjson.write_text(json.dumps(report, indent=2))
    print(f"Wrote summary to {outjson}")
    # record overall end epoch, duration, and update summary file
    report["end_epoch"] = time.time()
    report["duration_seconds"] = report["end_epoch"] - report["start_epoch"]
    outjson.write_text(json.dumps(report, indent=2))
    # Print a concise human-readable summary to stdout so callers get instant counts
    try:
        print("\n=== Quick test summary ===")
        # Host summary (if present)
        host = report.get("host")
        if host and isinstance(host, dict) and host.get("ctest_rc") is not None:
            # Prefer per-Unity case counts if available, otherwise fall back to ctest target counts
            case_counts = host.get("case_counts", {}) if isinstance(host.get("case_counts"), dict) else {}
            total_cases = int(case_counts.get("total", 0) or 0)
            failed_cases = int(case_counts.get("failures", 0) or 0)
            ignored_cases = int(case_counts.get("ignored", 0) or 0)
            passed_cases = total_cases - failed_cases - ignored_cases
            if total_cases > 0:
                print(f"Host tests: {total_cases} total cases, {passed_cases} passed, {failed_cases} failed, {ignored_cases} ignored")
            else:
                # try to extract target counts from the ctest output
                ctest_out = host.get("ctest_output", "")
                import re
                m = re.search(r"(\d+)% tests passed, (\d+) tests failed out of (\d+)", ctest_out)
                if m:
                    print(f"Host tests: {m.group(3)} total targets, {int(m.group(3))-int(m.group(2))} passed, {m.group(2)} failed")
                else:
                    # best-effort: print ctest summary header
                    print("Host tests: ctest run (see host_ctest_output.log)")

        # Device suites
        devices = report.get("devices", {}) or {}
        total_tests = 0
        total_failed = 0
        total_ignored = 0
        if devices:
            for sname, dev in devices.items():
                tests = dev.get("tests_total") or dev.get("tests") or 0
                passed = dev.get("tests_passed")
                failed_count = dev.get("tests_failed")
                ignored = dev.get("tests_ignored")
                # if numeric fields missing, try to derive from aggregated 'aggregate.by_file'
                if passed is None or failed_count is None:
                    # attempt to find entry in report['aggregate']['by_file']
                    try:
                        by_file = report.get("aggregate", {}).get("by_file", {})
                        # look for a key that endswith the suite's one_run_unity.log
                        match_key = None
                        for k in by_file.keys():
                            if sname in k and k.endswith("one_run_unity.log"):
                                match_key = k
                                break
                        if match_key:
                            entries = by_file.get(match_key, {}).get("entries", [])
                            if entries:
                                vals = entries[-1]
                                tests = vals.get("tests", tests)
                                failed_count = vals.get("failures", failed_count if failed_count is not None else 0)
                                ignored = vals.get("ignored", ignored if ignored is not None else 0)
                                passed = tests - failed - ignored
                    except Exception:
                        pass

                # final fallback: count tokens in the output_file
                if (passed is None or failed_count is None) and dev.get("output_file"):
                    try:
                        p = Path(dev.get("output_file"))
                        counted = count_unity_results(p)
                        tests = counted.get("tests", tests)
                        failed_count = counted.get("failures", failed_count if failed_count is not None else 0)
                        ignored = counted.get("ignored", ignored if ignored is not None else 0)
                        passed = tests - failed - ignored
                    except Exception:
                        pass

                if passed is None:
                    passed = tests - (failed_count or 0) - (ignored or 0)
                if failed_count is None:
                    failed_count = tests - passed - (ignored or 0)
                if ignored is None:
                    ignored = 0

                total_tests += int(tests or 0)
                total_failed += int(failed_count or 0)
                total_ignored += int(ignored or 0)
                if int(tests) == 0:
                    overall_failed = True
                    print(f"{sname}: {int(tests)} total (CRITICAL: zero tests), {int(passed)} passed, {int(failed_count)} failed, {int(ignored)} ignored")
                else:
                    print(f"{sname}: {int(tests)} total, {int(passed)} passed, {int(failed_count)} failed, {int(ignored)} ignored")

        print(f"Aggregate device totals: {total_tests} total, {total_tests - total_failed - total_ignored} passed, {total_failed} failed, {total_ignored} ignored")
    except Exception as _:
        # don't fail the script if pretty printing fails
        pass
    print("Done.")
    if overall_failed:
        print("One or more suites failed to build/run or reported zero tests. Exiting with failure.")
        sys.exit(1)
    sys.exit(0)


if __name__ == '__main__':
    main()
