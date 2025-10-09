#!/usr/bin/env python3
"""
Symbolize pairing serial logs produced under build/pairing_e2_logs/serial.log

This small helper extracts addresses (hex) from the serial log and runs addr2line
against the provided ELF to produce a symbolized timeline. It's designed for
ESP-IDF build outputs (xtensa toolchain) but will fall back to `addr2line` in PATH.

Usage:
    ./symbolize_pairing.py --log build/pairing_e2_logs/serial.log --elf build/esp_bt_audio_source.elf -o symbolized.log

If your toolchain's addr2line is not in PATH, set the ADDR2LINE environment variable:
    export ADDR2LINE=/path/to/xtensa-esp32-elf-addr2line

The script will group addresses found per line and append the resolved symbol info
below each original line for easy timeline analysis.
"""

import argparse
import os
import re
import shlex
import shutil
import subprocess
import sys
import csv


# Match hex addresses with 1 to 16 hex digits (tests and some logs use short addresses like 0x1)
ADDR_RE = re.compile(r"0x[0-9a-fA-F]{1,16}")


def find_addr2line():
    env = os.environ.get('ADDR2LINE')
    if env and shutil.which(env):
        return env
    # prefer xtensa esp toolchain if present
    for name in ('xtensa-esp32-elf-addr2line', 'xtensa-esp32s2-elf-addr2line', 'addr2line'):
        path = shutil.which(name)
        if path:
            return path
    return None


def run_addr2line(addr2line, elf, addr):
    # addr2line expects addresses like 0x400d1234; pass -e <elf> -f -p <addr>
    cmd = [addr2line, '-e', elf, '-f', '-p', addr]
    try:
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False, text=True)
        if p.returncode == 0:
            return p.stdout.strip()
        else:
            return p.stderr.strip() or '<no-symbol>'
    except Exception as e:
        return f'<error: {e}>'


def symbolize_log(log_path, elf_path, out_path=None, no_resolve=False):
    if not os.path.exists(log_path):
        print(f'Log file not found: {log_path}', file=sys.stderr)
        return 2
    if not os.path.exists(elf_path):
        print(f'ELF file not found: {elf_path}', file=sys.stderr)
        return 2

    addr2line = find_addr2line()
    # Honor explicit no_resolve request: do not call addr2line even if found
    if no_resolve:
        addr2line = None

    out_lines = []
    with open(log_path, 'r', errors='replace') as f:
        for line in f:
            stripped = line.rstrip('\n')
            out_lines.append(stripped)
            # find all addresses on this line
            addrs = ADDR_RE.findall(stripped)
            if addrs:
                # de-duplicate while preserving order
                seen = set()
                unique = []
                for a in addrs:
                    if a not in seen:
                        seen.add(a)
                        unique.append(a)
                for a in unique:
                    if addr2line:
                        sym = run_addr2line(addr2line, elf_path, a)
                    else:
                        sym = '<no-resolve>'
                    out_lines.append(f'    -> {a} : {sym}')

    if out_path:
        with open(out_path, 'w') as out_f:
            out_f.write('\n'.join(out_lines) + '\n')
        print(f'Wrote symbolized log to: {out_path}')
    else:
        print('\n'.join(out_lines))
    return 0


def write_csv_aggregation(log_path, elf_path, csv_path=None, sort=False, top=None, no_resolve=False):
    """Aggregate addresses in the log, resolve symbols, and write CSV.

    CSV columns: address,count,symbol
    symbol is the addr2line output (function at file:line) or an error marker.
    """
    if not os.path.exists(log_path):
        print(f'Log file not found: {log_path}', file=sys.stderr)
        return 2
    if not os.path.exists(elf_path):
        print(f'ELF file not found: {elf_path}', file=sys.stderr)
        return 2

    addr2line = find_addr2line()
    if not addr2line:
        addr2line = None

    counts = {}
    order = []
    with open(log_path, 'r', errors='replace') as f:
        for line in f:
            addrs = ADDR_RE.findall(line)
            if not addrs:
                continue
            # count unique occurrences per appearance
            for a in addrs:
                counts[a] = counts.get(a, 0) + 1
                if a not in order:
                    order.append(a)

    # Resolve symbols for each unique address in order (or after sorting)
    # Prepare list of (address, count) pairs
    items = [(a, counts.get(a, 0)) for a in order]

    # Optionally sort by count descending (most frequent first)
    if sort:
        items.sort(key=lambda x: x[1], reverse=True)

    # Optionally limit to top N
    if top is not None:
        try:
            top_n = int(top)
            if top_n > 0:
                items = items[:top_n]
        except Exception:
            # ignore invalid top and proceed with full list
            pass

    # Resolve symbols for each address in final items list (unless no_resolve)
    resolved = {}
    for a, _ in items:
        if no_resolve or not addr2line:
            resolved[a] = '<no-resolve>'
        else:
            resolved[a] = run_addr2line(addr2line, elf_path, a)

    # Emit CSV
    out_f = None
    try:
        if csv_path:
            out_f = open(csv_path, 'w', newline='')
            writer = csv.writer(out_f)
        else:
            writer = csv.writer(sys.stdout)

        writer.writerow(['address', 'count', 'symbol'])
        for a, c in items:
            writer.writerow([a, c, resolved.get(a, '')])
        if csv_path:
            print(f'Wrote CSV aggregation to: {csv_path}')
    finally:
        if out_f:
            out_f.close()
    return 0


def main():
    ap = argparse.ArgumentParser(description='Symbolize pairing serial logs using addr2line')
    ap.add_argument('--log', '-l', required=True, help='Path to serial log')
    ap.add_argument('--elf', '-e', required=True, help='Path to built ELF (for addr2line)')
    ap.add_argument('--out', '-o', help='Optional output path for symbolized log')
    ap.add_argument('--csv', help='Optional CSV output path for aggregated address counts')
    ap.add_argument('--sort', action='store_true', help='Sort CSV by count (descending)')
    ap.add_argument('--top', type=int, help='Limit CSV to top N addresses by count')
    ap.add_argument('--no-resolve', action='store_true', help='Do not call addr2line; emit CSV/timeline without symbol resolution')
    args = ap.parse_args()
    if args.csv:
        rc = write_csv_aggregation(args.log, args.elf, args.csv, sort=args.sort, top=args.top, no_resolve=args.no_resolve)
    else:
        # Pass no-resolve via environment; symbolize_log will handle missing addr2line
        rc = symbolize_log(args.log, args.elf, args.out, no_resolve=args.no_resolve)
    sys.exit(rc)


if __name__ == '__main__':
    main()
