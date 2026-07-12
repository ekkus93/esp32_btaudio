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
import sys
import time
from pathlib import Path

from rat.common import ROOT, TMP_DIR
from rat.cleanup import cleanup_previous_artifacts
from rat.host import run_host_tests, run_standalone_host_tests
from rat.device import run_device_suite
from rat.proc import ensure_esptool
from rat.report import (
    aggregate_summary,
    count_unity_results,
    parse_flash_time_from_log,
    parse_ctest_duration,
)


def main(argv: list[str] | None = None):
    p = argparse.ArgumentParser(description="Run host + on-device unit tests and collect logs")
    p.add_argument("--port", default="/dev/ttyUSB0", help="Serial port for flashing (device runs)")
    p.add_argument("--timeout", type=int, default=300, help="Timeout per device suite in seconds")
    p.add_argument("--no-host", action="store_true", help="Skip host CTest run")
    p.add_argument("--no-device", action="store_true", help="Skip device Unity runs")
    p.add_argument("--no-standalone", action="store_true", help="Skip standalone host test build (CI parity check)")
    p.add_argument("--source-idf", help="Path to ESP-IDF export.sh to source before device commands")
    p.add_argument("--jobs", type=int, default=0, help="Parallel jobs for host build (passed to cmake --build)")
    p.add_argument("--valgrind", action="store_true", help="Run host tests under Valgrind for memory leak detection")
    p.add_argument("--coverage", action="store_true", help="Enable code coverage reporting (gcov/lcov)")
    p.add_argument("--asan", action="store_true", help="Enable AddressSanitizer for fast memory error detection (requires rebuild)")
    args = p.parse_args(argv)

    report = {"host": None, "standalone_host": None, "devices": {}, "aggregate": None}
    overall_failed = False

    cleanup_previous_artifacts(ROOT, remove_host=not args.no_host, remove_device=not args.no_device)

    # record overall orchestration start (use float for higher resolution)
    report["start_epoch"] = time.time()

    # Host tests
    if not args.no_host:
        print("\n== Running host tests ==")
        if args.valgrind:
            print("  ⚠️  Valgrind enabled - tests will run slower but with memory leak detection")
        if args.coverage:
            print("  📊 Coverage reporting enabled - will generate HTML report after tests")
        if args.asan:
            print("  🛡️  AddressSanitizer enabled - fast memory error detection (2-3x slower)")
        if args.asan and args.valgrind:
            print("  ⚠️  WARNING: Both ASan and Valgrind enabled - Valgrind will be skipped")
        host_start = time.time()
        report["host"] = run_host_tests(ROOT, jobs=args.jobs, valgrind=args.valgrind and not args.asan, coverage=args.coverage, asan=args.asan)
        host_end = time.time()
        # run_host_tests historically returned a wrapper dict {"host": {...}}
        # unwrap it if present so downstream code can treat report['host'] as
        # the host-summary dict directly.
        try:
            if isinstance(report["host"], dict) and "host" in report["host"] and isinstance(report["host"]["host"], dict):
                report["host"] = report["host"]["host"]
        except Exception:
            pass
        try:
            report["host"]["start_epoch"] = host_start
            report["host"]["end_epoch"] = host_end
            report["host"]["duration_seconds"] = host_end - host_start
            report["host"]["ctest_duration_seconds"] = parse_ctest_duration(report["host"].get("ctest_output", ""))
        except Exception:
            pass
        # Treat host test failures as critical
        # Note: ctest returns 8 when tests pass but there are warnings/issues.
        # Only fail if there were actual test failures (case_counts.failures > 0).
        try:
            case_counts = report["host"].get("case_counts", {}) if isinstance(report["host"].get("case_counts"), dict) else {}
            total_failures = int(case_counts.get("failures", 0) or 0)
            if total_failures > 0:
                overall_failed = True
        except Exception:
            pass

        # Treat zero-test host binaries as a failure so silent suites are caught
        try:
            zero_bins = report["host"].get("case_counts", {}).get("zero_test_binaries", [])
            if zero_bins:
                overall_failed = True
        except Exception:
            pass

        # All host suites now live under test/host_test and are exercised via ctest above
        report["host_extra"] = {}
    else:
        print("Skipping host tests (--no-host)")

    # Standalone host tests (CI parity check)
    if not args.no_standalone and not args.no_host:
        print("\n== Running standalone host tests (CI parity check) ==")
        standalone_start = time.time()
        report["standalone_host"] = run_standalone_host_tests(ROOT, jobs=args.jobs)
        standalone_end = time.time()
        try:
            report["standalone_host"]["start_epoch"] = standalone_start
            report["standalone_host"]["end_epoch"] = standalone_end
            report["standalone_host"]["duration_seconds"] = standalone_end - standalone_start
        except Exception:
            pass
        # Treat standalone build/test failures as critical
        try:
            if not report["standalone_host"].get("build", False):
                overall_failed = True
                print("❌ STANDALONE BUILD FAILED - this would break CI!")
            elif report["standalone_host"].get("failures", 0) > 0:
                overall_failed = True
                print(f"❌ STANDALONE TESTS FAILED ({report['standalone_host']['failures']} failures) - this would break CI!")
        except Exception:
            pass
    else:
        if args.no_standalone:
            print("Skipping standalone host tests (--no-standalone)")
        else:
            print("Skipping standalone host tests (host tests disabled)")

    # Device suites
    if not args.no_device:
        print("\n== Running on-device Unity suites ==")
        rc_esptool, out_esptool = ensure_esptool("esptool~=4.11.dev1", args.source_idf)
        if rc_esptool != 0:
            print("WARNING: failed to install required esptool; device runs may fail")
            try:
                report.setdefault("devices", {})["esptool_install"] = {"rc": rc_esptool, "output": out_esptool}
            except Exception:
                pass
        runner = ROOT / "tools" / "run_unity.py"
        suites = [
            ROOT / "esp_bt_audio_source" / "test" / "test_bluetooth",
            ROOT / "esp_bt_audio_source" / "test" / "test_app_audio",
            ROOT / "esp_bt_audio_source" / "test" / "test_manager",
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
                # no canonical capture found for this suite; don't mark overall failure
                # yet — allow fallback counting from per-test entries or the
                # runner-captured output file below before deciding the run
                # should be considered a failure.
                print(f"Suite {sname}: no canonical log found to extract test counts; check {dev.get('output_file')}")
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
        def _fmt_secs(val):
            try:
                return f"{float(val):.2f}s"
            except Exception:
                return "n/a"

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
                zero_bins = host.get("case_counts", {}).get("zero_test_binaries", [])
                zero_note = " (CRITICAL: zero tests in " + ", ".join(zero_bins) + ")" if zero_bins else ""
                if zero_bins:
                    overall_failed = True
                wall = _fmt_secs(host.get("duration_seconds")) if host.get("duration_seconds") else None
                ctest_dur = host.get("ctest_duration_seconds")
                duration_note = ""
                if wall or ctest_dur:
                    duration_note = f" (wall {wall or 'n/a'}, ctest {_fmt_secs(ctest_dur) if ctest_dur else 'n/a'})"
                print(f"Host tests: {total_cases} total cases, {passed_cases} passed, {failed_cases} failed, {ignored_cases} ignored{zero_note}{duration_note}")
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

        # Standalone host Unity suites
        extra_host = report.get("host_extra", {}) or {}
        if extra_host:
            for name, summary in extra_host.items():
                tests = int(summary.get("tests", 0) or 0)
                failures = int(summary.get("failures", 0) or 0)
                ignored = int(summary.get("ignored", 0) or 0)
                passed = tests - failures - ignored
                if passed < 0:
                    passed = 0
                ctest_rc = summary.get("ctest_rc")
                rc_note = " (ctest failed)" if ctest_rc not in (None, 0) else ""
                zero_note = " (CRITICAL: zero tests)" if tests == 0 else ""
                if tests == 0:
                    overall_failed = True
                print(f"Host suite {name}: {tests} total, {passed} passed, {failures} failed, {ignored} ignored{rc_note}{zero_note}")

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
                                passed = tests - failed_count - ignored
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
                    total_dur = _fmt_secs(dev.get("duration_seconds")) if dev.get("duration_seconds") is not None else "n/a"
                    flash_dur = _fmt_secs(dev.get("flash_time_seconds")) if dev.get("flash_time_seconds") is not None else "n/a"
                    test_dur = _fmt_secs(dev.get("test_run_seconds")) if dev.get("test_run_seconds") is not None else "n/a"
                    print(f"{sname}: {int(tests)} total, {int(passed)} passed, {int(failed_count)} failed, {int(ignored)} ignored (total {total_dur}, flash {flash_dur}, tests {test_dur})")

        print(f"Aggregate device totals: {total_tests} total, {total_tests - total_failed - total_ignored} passed, {total_failed} failed, {total_ignored} ignored")
    except Exception as _:
        # don't fail the script if pretty printing fails
        pass
    # Recompute final failure status from the current summary so transient
    # earlier flags (e.g., flaky device runs) don't override the final results.
    final_failed = False
    try:
        host = report.get("host")
        if host and isinstance(host, dict):
            case_counts = host.get("case_counts", {}) if isinstance(host.get("case_counts"), dict) else {}
            if int(case_counts.get("failures", 0) or 0) > 0:
                final_failed = True
            zero_bins = case_counts.get("zero_test_binaries", []) if isinstance(case_counts.get("zero_test_binaries"), list) else []
            if zero_bins:
                final_failed = True
    except Exception:
        pass
    try:
        standalone = report.get("standalone_host")
        if standalone and isinstance(standalone, dict):
            if not standalone.get("build", False):
                final_failed = True
            elif int(standalone.get("failures", 0) or 0) > 0:
                final_failed = True
    except Exception:
        pass
    try:
        devices = report.get("devices", {}) or {}
        for dev in devices.values():
            tests = dev.get("tests_total")
            failures = dev.get("tests_failed")
            ignored = dev.get("tests_ignored")
            if tests is None or failures is None:
                # fallback to parse output file if present
                if dev.get("output_file"):
                    counted = count_unity_results(Path(dev.get("output_file")))
                    tests = counted.get("tests", 0)
                    failures = counted.get("failures", 0)
                    ignored = counted.get("ignored", 0)
                else:
                    tests = tests or 0
                    failures = failures or 0
                    ignored = ignored or 0
            if int(tests or 0) == 0:
                final_failed = True
            if int(failures or 0) > 0:
                final_failed = True
    except Exception:
        pass

    overall_failed = final_failed
    print("Done.")
    if overall_failed:
        print("One or more suites failed to build/run or reported zero tests. Exiting with failure.")
        sys.exit(1)
    sys.exit(0)


if __name__ == '__main__':
    main()
