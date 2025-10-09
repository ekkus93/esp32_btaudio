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


ADDR_RE = re.compile(r"0x[0-9a-fA-F]{6,16}")


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


def symbolize_log(log_path, elf_path, out_path=None):
    if not os.path.exists(log_path):
        print(f'Log file not found: {log_path}', file=sys.stderr)
        return 2
    if not os.path.exists(elf_path):
        print(f'ELF file not found: {elf_path}', file=sys.stderr)
        return 2

    addr2line = find_addr2line()
    if not addr2line:
        print('addr2line not found. Set ADDR2LINE env var to your toolchain addr2line.', file=sys.stderr)
        return 3

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
                    sym = run_addr2line(addr2line, elf_path, a)
                    out_lines.append(f'    -> {a} : {sym}')

    if out_path:
        with open(out_path, 'w') as out_f:
            out_f.write('\n'.join(out_lines) + '\n')
        print(f'Wrote symbolized log to: {out_path}')
    else:
        print('\n'.join(out_lines))
    return 0


def main():
    ap = argparse.ArgumentParser(description='Symbolize pairing serial logs using addr2line')
    ap.add_argument('--log', '-l', required=True, help='Path to serial log')
    ap.add_argument('--elf', '-e', required=True, help='Path to built ELF (for addr2line)')
    ap.add_argument('--out', '-o', help='Optional output path for symbolized log')
    args = ap.parse_args()

    rc = symbolize_log(args.log, args.elf, args.out)
    sys.exit(rc)


if __name__ == '__main__':
    main()
