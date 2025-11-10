#!/usr/bin/env python3
"""Build a SPIFFS image using mkspiffs.

Usage examples:
  python tools/make_spiffs.py -c ../main/assets/spiffs -s 0x40000 -o ../main/assets/spiffs/spiffs.bin

The script looks first for $IDF_PATH/components/spiffs/mkspiffs/mkspiffs, then for `mkspiffs` on PATH.
"""
import os
import sys
import shutil
import argparse
import subprocess


def find_mkspiffs():
    idf = os.environ.get('IDF_PATH')
    if idf:
        candidate = os.path.join(idf, 'components', 'spiffs', 'mkspiffs', 'mkspiffs')
        if os.path.isfile(candidate) and os.access(candidate, os.X_OK):
            return candidate
    exe = shutil.which('mkspiffs')
    if exe:
        return exe
    return None


def run_mkspiffs(mkspiffs, src, size, out, block=4096, page=256):
    cmd = [mkspiffs, '-c', src, '-b', str(block), '-p', str(page), '-s', str(size), out]
    print('Running:', ' '.join(cmd))
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    print(proc.stdout.decode(errors='ignore'))
    return proc.returncode


def main():
    p = argparse.ArgumentParser()
    p.add_argument('-c', '--src', required=True, help='Source directory to pack')
    p.add_argument('-s', '--size', default='0x40000', help='Total image size (e.g. 0x40000)')
    p.add_argument('-o', '--out', required=True, help='Output image path')
    p.add_argument('--block', type=int, default=4096, help='Block size (default 4096)')
    p.add_argument('--page', type=int, default=256, help='Page size (default 256)')
    args = p.parse_args()

    src = os.path.abspath(args.src)
    out = os.path.abspath(args.out)

    if not os.path.isdir(src):
        print('ERROR: source directory not found:', src, file=sys.stderr)
        sys.exit(2)

    mkspiffs = find_mkspiffs()
    if not mkspiffs:
        print('ERROR: mkspiffs not found. Set $IDF_PATH or install mkspiffs and put it on PATH.', file=sys.stderr)
        sys.exit(3)

    # support hex and decimal sizes
    size = args.size
    try:
        if isinstance(size, str) and size.lower().startswith('0x'):
            size_val = int(size, 16)
        else:
            size_val = int(size)
    except Exception as e:
        print('ERROR: invalid size', args.size, file=sys.stderr)
        sys.exit(4)

    rc = run_mkspiffs(mkspiffs, src, hex(size_val), out, block=args.block, page=args.page)
    if rc != 0:
        print('mkspiffs failed with exit code', rc, file=sys.stderr)
        sys.exit(rc)

    # verify output
    if os.path.exists(out):
        st = os.stat(out)
        print('WROTE', out, 'SIZE_BYTES', st.st_size)
    else:
        print('ERROR: expected output not created:', out, file=sys.stderr)
        sys.exit(5)


if __name__ == '__main__':
    main()
