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
    # device canonical logs
    'esp_bt_audio_source/test_app/build/one_run_unity.log',
    'esp_bt_audio_source/test_app_audio/build/one_run_unity.log',
    'esp_bt_audio_source/device_test_monitor.log',
    # host LastTest.log common locations (may vary slightly between builds)
    'esp_bt_audio_source/test/host_test/**/Testing/Temporary/LastTest.log',
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
    except Exception as e:
        return {'error': str(e), 'entries': []}
    return {'entries': results}


def aggregate(files, latest_only=False):
    by_file = {}
    total = {'tests': 0, 'failures': 0, 'ignored': 0}
    for p in files:
        parsed = parse_file(p)
        if 'error' in parsed:
            by_file[p] = {'error': parsed['error']}
            continue
        entries = parsed['entries']
        if not entries:
            by_file[p] = {'entries': []}
            continue
        if latest_only:
            e = entries[-1]
            by_file[p] = {'entries': [e], 'count': 1}
            total['tests'] += e['tests']
            total['failures'] += e['failures']
            total['ignored'] += e['ignored']
        else:
            by_file[p] = {'entries': entries, 'count': len(entries)}
            for e in entries:
                total['tests'] += e['tests']
                total['failures'] += e['failures']
                total['ignored'] += e['ignored']
    summary = {
        'generated_at': datetime.utcnow().isoformat() + 'Z',
        'total': total,
        'by_file': by_file,
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
