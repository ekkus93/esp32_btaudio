#!/usr/bin/env python3
"""Build a SPIFFS image using mkspiffs.

Usage examples:
  python tools/make_spiffs.py -c ../main/assets/spiffs -s 0x40000 -o ../main/assets/spiffs/spiffs.bin

The script looks first for $IDF_PATH/components/spiffs/mkspiffs/mkspiffs, then for `mkspiffs` on PATH.
"""
import argparse
import os
import shutil
import subprocess
import sys


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


def mkspiffs_supports_extended_args(mkspiffs):
    try:
        probe = subprocess.run([mkspiffs, '--help'], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    except (FileNotFoundError, PermissionError, OSError):
        return False
    help_text = probe.stdout.decode(errors='ignore')
    return '--meta-len' in help_text and '--obj-name-len' in help_text


def find_spiffsgen():
    idf = os.environ.get('IDF_PATH')
    candidates = []
    if idf:
        candidates.append(os.path.join(idf, 'components', 'spiffs', 'spiffsgen.py'))

    tools_dir = os.path.dirname(os.path.abspath(__file__))
    repo_root = os.path.abspath(os.path.join(tools_dir, '..'))
    candidates.extend([
        os.path.join(repo_root, 'components', 'spiffs', 'spiffsgen.py'),
        os.path.join(repo_root, 'components', 'components', 'spiffs', 'spiffsgen.py'),
    ])

    for candidate in candidates:
        candidate_path = os.path.abspath(candidate)
        if os.path.isfile(candidate_path):
            return candidate_path
    return None


def run_mkspiffs(mkspiffs, src, size, out, block=4096, page=256, meta=4, obj_name_len=32):
    cmd = [
        mkspiffs,
        '-c',
        src,
        '-b',
        str(block),
        '-p',
        str(page),
        '--meta-len',
        str(meta),
        '--obj-name-len',
        str(obj_name_len),
        '-s',
        str(size),
        out,
    ]
    print('Running:', ' '.join(cmd))
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    print(proc.stdout.decode(errors='ignore'))
    return proc.returncode


def run_spiffsgen(spiffsgen, src, size, out, block=4096, page=256, meta=4, obj_name_len=32):
    cmd = [
        sys.executable,
        spiffsgen,
        str(size),
        src,
        out,
        '--block-size',
        str(block),
        '--page-size',
        str(page),
        '--meta-len',
        str(meta),
        '--obj-name-len',
        str(obj_name_len),
    ]
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
    p.add_argument('--meta', type=int, default=4, help='Metadata length per file (default 4, matches CONFIG_SPIFFS_META_LENGTH)')
    p.add_argument('--name-len', type=int, default=32, help='Object name length (default 32, matches CONFIG_SPIFFS_OBJ_NAME_LEN)')
    args = p.parse_args()

    src = os.path.abspath(args.src)
    out = os.path.abspath(args.out)

    if not os.path.isdir(src):
        print('ERROR: source directory not found:', src, file=sys.stderr)
        sys.exit(2)

    # support hex and decimal sizes
    size = args.size
    try:
        if isinstance(size, str) and size.lower().startswith('0x'):
            size_val = int(size, 16)
        else:
            size_val = int(size)
    except Exception:
        print('ERROR: invalid size', args.size, file=sys.stderr)
        sys.exit(4)

    mkspiffs = find_mkspiffs()
    rc = None
    if mkspiffs and mkspiffs_supports_extended_args(mkspiffs):
        rc = run_mkspiffs(
            mkspiffs,
            src,
            hex(size_val),
            out,
            block=args.block,
            page=args.page,
            meta=args.meta,
            obj_name_len=args.name_len,
        )
    else:
        if mkspiffs:
            print('mkspiffs found but lacks --meta-len/--obj-name-len support; falling back to spiffsgen.py')
        spiffsgen = find_spiffsgen()
        if not spiffsgen:
            if mkspiffs:
                print('ERROR: mkspiffs is missing required options and spiffsgen.py not found.', file=sys.stderr)
            else:
                print('ERROR: mkspiffs not found and spiffsgen.py not located. Set $IDF_PATH or ensure tools/components/spiffs/spiffsgen.py exists.', file=sys.stderr)
            sys.exit(3)
        rc = run_spiffsgen(
            spiffsgen,
            src,
            size_val,
            out,
            block=args.block,
            page=args.page,
            meta=args.meta,
            obj_name_len=args.name_len,
        )
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
