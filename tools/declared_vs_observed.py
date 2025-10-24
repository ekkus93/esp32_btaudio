#!/usr/bin/env python3
"""
Generate a declared-vs-observed CSV while ignoring vendor Unity sources.
Outputs: tmp/declared_vs_observed_project.csv

Usage: python3 tools/declared_vs_observed.py
"""
import os
import re
import json
import csv

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
AGG_JSON = os.path.join(ROOT, 'tmp', 'canonical_unity_summary.json')
OUT_CSV = os.path.join(ROOT, 'tmp', 'declared_vs_observed_project.csv')

# Paths to ignore as vendor/dependency sources
VENDOR_SUBSTRINGS = [
    os.path.join('_deps', 'unity-src'),
    os.path.join('build', '_deps', 'unity-src'),
    os.path.join('test', 'host_test', 'build', '_deps'),
    os.path.join('build', '_deps'),
    os.path.join('_deps',),
]

# Directories to include for declared-scan when computing project tests
INCLUDE_DIRS = [
    os.path.join('esp_bt_audio_source', 'test', 'host_test'),
]

RUN_TEST_RE = re.compile(r'RUN_TEST\s*\(\s*([A-Za-z_][A-Za-z0-9_]*)')


def is_vendor_path(path):
    for s in VENDOR_SUBSTRINGS:
        if s in path:
            return True
    return False


def find_declared_tests():
    declared = []  # tuples (name, filepath, lineno)
    for inc in INCLUDE_DIRS:
        top = os.path.join(ROOT, inc)
        if not os.path.isdir(top):
            continue
        for dirpath, dirnames, filenames in os.walk(top):
            # Skip vendor/deps dirs
            if is_vendor_path(dirpath):
                continue
            for fn in filenames:
                if not fn.endswith(('.c', '.h', '.cpp')):
                    continue
                fp = os.path.join(dirpath, fn)
                try:
                    with open(fp, 'r', encoding='utf-8', errors='ignore') as f:
                        for i, line in enumerate(f, start=1):
                            m = RUN_TEST_RE.search(line)
                            if m:
                                name = m.group(1)
                                declared.append((name, os.path.relpath(fp, ROOT), i))
                except Exception:
                    pass
    return declared


def read_observed_names():
    if not os.path.isfile(AGG_JSON):
        return set(), {}
    try:
        with open(AGG_JSON, 'r', encoding='utf-8') as f:
            j = json.load(f)
    except Exception:
        return set(), {}
    per = j.get('per_test', [])
    names = set()
    files_by_name = {}
    for e in per:
        n = e.get('name')
        if not n:
            continue
        names.add(n)
        files_by_name.setdefault(n, []).extend(e.get('files', []))
    # dedupe file lists
    for k in files_by_name:
        files_by_name[k] = sorted(set(files_by_name[k]))
    return names, files_by_name


def main():
    declared = find_declared_tests()
    observed_names, files_by_name = read_observed_names()

    # Write CSV
    os.makedirs(os.path.join(ROOT, 'tmp'), exist_ok=True)
    with open(OUT_CSV, 'w', newline='', encoding='utf-8') as csvf:
        w = csv.writer(csvf)
        w.writerow(['test_name', 'declared_file', 'declared_line', 'observed', 'observed_files'])
        for name, path, line in sorted(declared):
            observed = 'yes' if name in observed_names else 'no'
            obs_files = ';'.join(files_by_name.get(name, []))
            w.writerow([name, path, line, observed, obs_files])

    total_declared = len({n for n,_,_ in declared})
    total_observed = len(observed_names & {n for n,_,_ in declared})
    missing = total_declared - total_observed
    print(f"WROTE {OUT_CSV}")
    print(f"Declared (project host_test): {total_declared}")
    print(f"Observed (intersection): {total_observed}")
    print(f"Missing: {missing}")


if __name__ == '__main__':
    main()
