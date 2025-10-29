#!/usr/bin/env python3
"""
flash_and_watch.py

Flash the built firmware using idf.py and run the serial monitor.
Watch the monitor output for Unity test summary lines and stop the monitor
automatically when the summary is observed. Saves full monitor output to
build/one_run_unity.log inside the project root.

Usage:
  ./tools/flash_and_watch.py --port /dev/ttyUSB0 [--elf build/esp_bt_audio_source.elf]

Notes:
- This script expects `idf.py` to be available in PATH. If not, it will attempt
  to source $HOME/esp/esp-idf/export.sh automatically before running idf.py.
- The script does not modify any source files. It simply invokes idf.py and
  monitors output for Unity summary lines such as "--- SUMMARY ---" or
  "Tests X Failures Y".
"""
import argparse
import os
import re
import shlex
import signal
import subprocess
import sys
import time


def normalize_line(raw):
    """Normalize an idf_monitor line.

    Strips ANSI color sequences and common idf_monitor prefixes such as
    "I (123) " or other single-letter level prefixes. Returns the cleaned
    line for regex matching.
    """
    # remove ANSI escape sequences
    cleaned = re.sub(r'\x1B\[[0-9;]*[A-Za-z]', '', raw)
    # remove common idf_monitor prefixes like 'I (123) ' or 'W (45) ' or 'E (67) '
    cleaned = re.sub(r'^[A-Z] \(\d+\)\s*', '', cleaned)
    # also remove occasional leading tag e.g. '--- esp-idf-monitor ...' or '--- 0x40080400:'
    # Only strip when the '---' is followed by whitespace (to avoid stripping
    # Unity banners which start with multiple dashes like '----- UNITY ...').
    cleaned = re.sub(r'^---\s+[^\n]*\n?', '', cleaned)
    # strip surrounding whitespace
    return cleaned.strip()


def find_export_sh():
    candidate = os.path.expanduser('~/esp/esp-idf/export.sh')
    return candidate if os.path.isfile(candidate) else None


def run_command(cmd, env=None):
    return subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env, text=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        '--port', required=True,
        help='Serial port to use (e.g., /dev/ttyUSB0)'
    )
    parser.add_argument(
        '--project-dir',
        default='esp_bt_audio_source/test_app',
        help='Project directory containing the build (defaults to test_app which '
             'runs Unity on boot)'
    )
    parser.add_argument(
        '--out', default='build/one_run_unity.log',
        help='Path to save captured monitor output'
    )
    parser.add_argument(
        '--timeout', type=int, default=300,
        help='Seconds to wait for Unity summary before giving up '
             '(default: 300)'
    )
    parser.add_argument(
        '--no-stop', action='store_true',
        help='Do not stop the monitor automatically when a Unity '
             'summary-like line is seen; run until timeout'
    )
    args = parser.parse_args()

    project_dir = os.path.abspath(args.project_dir)
    if not os.path.isdir(project_dir):
        print(f'Error: project dir not found: {project_dir}', file=sys.stderr)
        sys.exit(2)

    out_path = os.path.join(project_dir, args.out) if not os.path.isabs(args.out) else args.out
    os.makedirs(os.path.dirname(out_path), exist_ok=True)

    # Try to ensure idf.py is in PATH by sourcing export.sh if needed
    export_sh = find_export_sh()
    idf_cmd = 'idf.py'
    if not shutil_in_path(idf_cmd) and export_sh:
        # Build a shell command that sources export.sh then runs idf.py
        parts = [
            '.', shlex.quote(export_sh), '&&',
            'cd', shlex.quote(project_dir), '&&',
            'idf.py', '-p', shlex.quote(args.port), 'flash', 'monitor'
        ]
        flash_cmd = ' '.join(parts)
    else:
        parts = [
            'cd', shlex.quote(project_dir), '&&',
            'idf.py', '-p', shlex.quote(args.port), 'flash', 'monitor'
        ]
        flash_cmd = ' '.join(parts)

    print('Running:', flash_cmd)

    proc = run_command(flash_cmd, env=os.environ.copy())

    # Match only the exact on-device Unity banners emitted by test_main.c to
    # avoid false-positives from build output (for example symbols like
    # UNITY_FREERTOS_STACK_SIZE) or unrelated logs.
    # idf_monitor sometimes prefixes lines with timestamps/tags (for example
    # "I (123) ..."), so anchoring to the start of line can miss banners.
    # We match known Unity/banner markers anywhere on the line.
    unity_summary_re = re.compile(
        (
            r'('
            r'--- SUMMARY ---|'
            r'----- UNITY TEST COMPLETE:|'
            r'Tests?\s+\d+\s+Failures?\s+\d+|'
            r'All tests completed'
            r')'
        ),
        re.IGNORECASE,
    )
    summary_found = False
    timed_out = False
    start_time = time.time()

    # use module-level normalize_line() defined above

    with open(out_path, 'w', encoding='utf-8') as fout:
        try:
            for line in proc.stdout:
                fout.write(line)
                fout.flush()
                print(line, end='')
                # Normalize before applying regex so idf_monitor prefixes don't hide markers
                nline = normalize_line(line)
                # Check for test summary markers
                if unity_summary_re.search(nline):
                    # If user requested no-stop, do not terminate on first match;
                    # keep recording until timeout.
                    if args.no_stop:
                        print(
                            (
                                'Unity summary-like line detected but --no-stop '
                                'set; continuing to record until timeout...'
                            ),
                            file=sys.stderr,
                        )
                    else:
                        summary_found = True
                        # give the monitor a short grace period for remaining output
                        time.sleep(0.5)
                        break

                # Check for timeout
                if (time.time() - start_time) > args.timeout:
                    timed_out = True
                    print(
                        (
                            f"Timeout ({args.timeout}s) reached while waiting "
                            "for Unity summary"
                        ),
                        file=sys.stderr,
                    )
                    break
        except KeyboardInterrupt:
            print('\nUser cancelled monitor', file=sys.stderr)
            proc.terminate()

    # After detecting summary (and if not --no-stop), terminate the monitor process gracefully
    if summary_found and not args.no_stop:
        print('\nUnity summary detected; terminating monitor...')
        try:
            proc.send_signal(signal.SIGINT)
            # wait a bit for process to exit
            proc.wait(timeout=5)
        except Exception:
            proc.terminate()
    else:
        if timed_out:
            print(
                '\nNo Unity summary detected before timeout; terminating monitor to '
                'avoid leaving it running.'
            )
        else:
            if summary_found and args.no_stop:
                print(
                    '\nUnity summary detected but --no-stop specified; monitor was '
                    'left running until timeout or exit.'
                )
            else:
                print(
                    '\nNo Unity summary detected; leaving monitor running. To stop it, '
                    'press Ctrl-] or Ctrl-C in the terminal.'
                )

    rc = proc.poll()
    if rc is None:
        # still running; try to terminate
        try:
            proc.terminate()
        except Exception:
            pass

    # Ensure process is stopped before we inspect output
    rc = proc.poll()
    if rc is None:
        try:
            proc.terminate()
        except Exception:
            pass

    # If user requested no-stop, inspect whatever was captured and decide exit code
    if args.no_stop:
        print(f'Captured monitor output to: {out_path}')
        with open(out_path, 'r', encoding='utf-8') as f:
            content = f.read()
            # Prefer parsing the Unity summary. Unity prints the summary in a few
            # formats (e.g. "11 Tests 0 Failures 0" or "Tests 11 Failures 0").
            # Try to match both variants and extract the failures count.
            m = re.search(r'^[ \t-]*(\d+)\s+Tests?\s+(\d+)\s+Failures?\s+(\d+)', content, re.MULTILINE | re.IGNORECASE)
            failures = None
            if m:
                # format: '<tests> Tests <failures> Failures <ignored?>'
                failures = int(m.group(2))
            else:
                pattern_tests_alt = (
                    r'^[ \t-]*Tests?\s+'
                    r'(\d+)\s+Failures?\s+(\d+)'
                )
                m2 = re.search(pattern_tests_alt, content, re.MULTILINE | re.IGNORECASE)
                if m2:
                    failures = int(m2.group(2))
            if failures is not None:
                if failures > 0:
                    print(f'Detected {failures} test failures in monitor output.')
                    sys.exit(1)
                else:
                    print('No failures detected in Unity summary.')
                    sys.exit(0)
            # Fallback: look for explicit per-test FAIL markers like ':FAIL'
            if re.search(r':FAIL\b', content):
                print('Detected per-test FAIL markers in monitor output.')
                sys.exit(1)
            # If we see a clear OK marker or summary-like text, treat as success
            pattern_ok = (
                r'(^--- SUMMARY ---|\bOK\b|All tests completed|All tests passed)'
            )
            if re.search(pattern_ok, content, re.IGNORECASE | re.MULTILINE):
                print('No failures detected in monitor output.')
                sys.exit(0)
            # No summary found at all
            print('No Unity summary detected before timeout.')
            sys.exit(3)

    # Default behavior (old): we only inspect when we stopped on a detected summary
    if summary_found:
        print(f'Captured monitor output to: {out_path}')
        # Inspect the captured file for failures
        with open(out_path, 'r', encoding='utf-8') as f:
            content = f.read()
            m = re.search(r'^[ \t-]*(\d+)\s+Tests?\s+(\d+)\s+Failures?\s+(\d+)', content, re.MULTILINE | re.IGNORECASE)
            failures = None
            if m:
                failures = int(m.group(2))
            else:
                m2 = re.search(r'^[ \t-]*Tests?\s+(\d+)\s+Failures?\s+(\d+)', content, re.MULTILINE | re.IGNORECASE)
                if m2:
                    failures = int(m2.group(2))
            if failures is not None:
                if failures > 0:
                    print(f'Detected {failures} test failures in monitor output.')
                    sys.exit(1)
                else:
                    print('No failures detected in Unity summary.')
                    sys.exit(0)
            if re.search(r':FAIL\b', content):
                print('Detected per-test FAIL markers in monitor output.')
                sys.exit(1)
            # If no explicit failures, assume success (we detected the summary marker earlier)
            print('No failures detected in monitor output.')
            sys.exit(0)
    else:
        sys.exit(3)


def shutil_in_path(cmd):
    # simple which() replacement
    from shutil import which
    return which(cmd) is not None


if __name__ == '__main__':
    main()
