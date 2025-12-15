#!/usr/bin/env python3
"""
Aggregate Unity test summaries from canonical artifact locations and write a JSON summary.

Usage:
  python3 tools/aggregate_unity.py [--output tmp/canonical_unity_summary.json] [--latest-only]

This script scans only the canonical locations used by the project to avoid false-positives
in README files or other incidental output. See README for canonical paths.
"""
import argparse
import json
import os
import re
import glob
from datetime import datetime

PATTERNS = [
    # device canonical logs (use a small recursive pattern to tolerate
    # slightly different build dir layouts while still limiting scope)
    'esp_bt_audio_source/**/build/one_run_unity.log',
    # host LastTest.log common locations (may vary slightly between builds)
    'esp_bt_audio_source/test/host_test/**/Testing/Temporary/LastTest.log',
    'test/host_test/**/Testing/Temporary/LastTest.log',
]

RE = re.compile(r"(\d+)\s+Tests\s+(\d+)\s+Failures\s+(\d+)\s+Ignored")


def find_files():
    files = []
    for p in PATTERNS:
        matched = glob.glob(p, recursive=True)
        for m in matched:
            if os.path.isfile(m):
                files.append(m)
    # unique and stable order
    files = sorted(set(files))
    return files


def parse_file(path):
    results = []
    try:
        with open(path, 'r', encoding='utf-8', errors='ignore') as f:
            for line in f:
                m = RE.search(line)
                if m:
                    tests = int(m.group(1))
                    failures = int(m.group(2))
                    ignored = int(m.group(3))
                    results.append({'tests': tests, 'failures': failures, 'ignored': ignored, 'line': line.strip()})
            # If we didn't find a numeric Unity summary line, try a fallback:
            # some test runs print per-test PASS/FAIL lines (e.g.
            # "./main/test_main.c:96:test_i2s_driver_init:PASS"). If no
            # numeric summary exists, count those occurrences and synthesize
            # a single derived entry so this file is included in aggregation.
            if not results:
                try:
                    f.seek(0)
                    text = f.read()

                    # Prefer explicit Unity footer when present (e.g., "TEST_RUN_COMPLETE: 29 0 0")
                    # to avoid under-counting when numeric summary lines are absent.
                    complete_m = re.search(r"TEST_RUN_COMPLETE:\s*(\d+)\s+(\d+)\s+(\d+)", text)
                    if complete_m:
                        total = int(complete_m.group(1))
                        failures = int(complete_m.group(2))
                        ignored = int(complete_m.group(3))
                        results.append({'tests': total, 'failures': failures, 'ignored': ignored, 'line': 'derived from TEST_RUN_COMPLETE footer'})
                    else:
                        # First try to detect an alternate multi-line numeric footer that some test
                        # harnesses print, e.g.:
                        #   Tests run : 45
                        #   Tests passed : 45
                        #   Tests failed : 0
                        # Optionally a "Tests ignored" or "Tests skipped" line may be present.
                        run_m = re.search(r"Tests\s+run\s*[:=]\s*(\d+)", text, re.IGNORECASE)
                        passed_m = re.search(r"Tests\s+passed\s*[:=]\s*(\d+)", text, re.IGNORECASE)
                        failed_m = re.search(r"Tests\s+failed\s*[:=]\s*(\d+)", text, re.IGNORECASE)
                        ignored_m = re.search(r"Tests\s+(?:ignored|skipped)\s*[:=]\s*(\d+)", text, re.IGNORECASE)

                        if run_m or passed_m or failed_m:
                            # synthesize counts when at least some components are present
                            total = int(run_m.group(1)) if run_m else None
                            passed = int(passed_m.group(1)) if passed_m else None
                            failed = int(failed_m.group(1)) if failed_m else None
                            ignored = int(ignored_m.group(1)) if ignored_m else 0

                            # If run not provided, try to compute from passed+failed
                            if total is None:
                                if passed is not None and failed is not None:
                                    total = passed + failed + ignored
                                elif passed is not None:
                                    total = passed + ignored
                                elif failed is not None:
                                    total = failed + ignored
                                else:
                                    total = 0

                            # If passed is missing, compute as total - failed - ignored when possible
                            if passed is None:
                                if failed is not None:
                                    passed = max(0, total - failed - ignored)
                                else:
                                    passed = max(0, total - ignored)

                            if failed is None:
                                # compute failures if possible
                                failed = max(0, total - passed - ignored)

                            # Append synthesized numeric entry
                            results.append({'tests': total, 'failures': failed, 'ignored': ignored, 'line': 'derived from multi-line numeric footer'})
                        else:
                            # Fallback: count per-test PASS/FAIL lines and synthesize
                            pass_count = len(re.findall(r":\s*PASS\b", text, re.IGNORECASE))
                            fail_count = len(re.findall(r":\s*FAIL\b", text, re.IGNORECASE))
                            total = pass_count + fail_count
                            if total > 0:
                                results.append({'tests': total, 'failures': fail_count, 'ignored': 0, 'line': 'derived from per-test PASS/FAIL lines'})
                except Exception:
                    # ignore fallback errors; return empty entries below
                    pass
    except Exception as e:
        return {'error': str(e), 'entries': []}
    return {'entries': results}


def aggregate(files, latest_only=False):
    by_file = {}
    # First, attempt to collect per-test PASS/FAIL/IGNORE lines from files and dedupe by test name.
    per_test_re = re.compile(r":\d+:([^:\s]+):(PASS|FAIL|IGNORE)", re.IGNORECASE)
    # fallback generic pattern (less strict) to catch other formats if needed
    per_test_re2 = re.compile(r"\b([^\s:]+)\s*:\s*(PASS|FAIL|IGNORE)\b", re.IGNORECASE)

    # aggregate per-file raw entries and also record per-test outcomes across files
    per_test_map = {}  # test_name -> {'result': 'PASS'|'FAIL'|'IGNORE', 'files': set()}
    def worse(a, b):
        # return the worse of two results: FAIL > IGNORE > PASS
        order = {'PASS': 0, 'IGNORE': 1, 'FAIL': 2}
        return a if order[a] >= order[b] else b

    for p in files:
        parsed = parse_file(p)
        if 'error' in parsed:
            by_file[p] = {'error': parsed['error']}
            continue
        entries = parsed['entries']
        by_file[p] = {'entries': entries, 'count': len(entries)}

        # scan file for per-test lines (preferred) and record occurrences
        try:
            with open(p, 'r', encoding='utf-8', errors='ignore') as fh:
                text = fh.read()
        except Exception:
            text = ''

        found_any = False
        for m in per_test_re.finditer(text):
            found_any = True
            test = m.group(1).strip()
            result = m.group(2).upper()
            if test in per_test_map:
                per_test_map[test]['result'] = worse(per_test_map[test]['result'], result)
                per_test_map[test]['files'].add(p)
            else:
                per_test_map[test] = {'result': result, 'files': {p}}

        if not found_any:
            # try the looser pattern
            for m in per_test_re2.finditer(text):
                test = m.group(1).strip()
                result = m.group(2).upper()
                if test in per_test_map:
                    per_test_map[test]['result'] = worse(per_test_map[test]['result'], result)
                    per_test_map[test]['files'].add(p)
                else:
                    per_test_map[test] = {'result': result, 'files': {p}}

    # If we found per-test entries, compute deduped totals from them. Otherwise, fall back to numeric summing.
    total = {'tests': 0, 'failures': 0, 'ignored': 0}
    per_test_list = []
    if per_test_map:
        for t, info in sorted(per_test_map.items()):
            total['tests'] += 1
            if info['result'] == 'FAIL':
                total['failures'] += 1
            elif info['result'] == 'IGNORE':
                total['ignored'] += 1
            per_test_list.append({'name': t, 'result': info['result'], 'files': sorted(list(info['files']))})
    else:
        # fallback to previous numeric behavior (summing or latest_only)
        total = {'tests': 0, 'failures': 0, 'ignored': 0}
        for p in files:
            parsed = parse_file(p)
            if 'error' in parsed:
                continue
            entries = parsed['entries']
            if not entries:
                continue
            if latest_only:
                e = entries[-1]
                total['tests'] += e['tests']
                total['failures'] += e['failures']
                total['ignored'] += e['ignored']
            else:
                for e in entries:
                    total['tests'] += e['tests']
                    total['failures'] += e['failures']
                    total['ignored'] += e['ignored']
    summary = {
        'generated_at': datetime.utcnow().isoformat() + 'Z',
        'total': total,
        'by_file': by_file,
        'per_test': per_test_list,
    }
    return summary


def main():
    parser = argparse.ArgumentParser(description='Aggregate Unity test summaries from canonical locations')
    parser.add_argument('--output', '-o', default='tmp/canonical_unity_summary.json', help='Output JSON path')
    parser.add_argument('--latest-only', action='store_true', help='Use only the last summary per file instead of summing repeated runs')
    args = parser.parse_args()

    files = find_files()
    if not files:
        print('No canonical artifact files found. Checked patterns:')
        for p in PATTERNS:
            print('  -', p)
        # still write empty summary
    else:
        print('Found files:')
        for f in files:
            print('  -', f)

    summary = aggregate(files, latest_only=args.latest_only)

    outdir = os.path.dirname(args.output)
    if outdir:
        os.makedirs(outdir, exist_ok=True)
    with open(args.output, 'w', encoding='utf-8') as outf:
        json.dump(summary, outf, indent=2)

    print('WROTE', args.output)
    print('Totals: {tests} Tests, {failures} Failures, {ignored} Ignored'.format(**summary['total']))

    # exit non-zero if there are failures in the aggregated totals
    if summary['total']['failures'] > 0:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
