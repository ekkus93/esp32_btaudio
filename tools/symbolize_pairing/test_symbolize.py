import csv
import subprocess
import sys
from pathlib import Path


def run_script(args, cwd=None):
    cmd = [sys.executable, str(Path(__file__).parent / 'symbolize_pairing.py')] + args
    p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, cwd=cwd)
    return p


def test_csv_no_resolve(tmp_path):
    log = tmp_path / 'serial.log'
    log.write_text('line1 0x1000\nline2 0x2000 0x1000\n')
    elf = tmp_path / 'dummy.elf'
    elf.write_text('ELF')

    out = tmp_path / 'out.csv'
    p = run_script(['--log', str(log), '--elf', str(elf), '--csv', str(out), '--no-resolve'])
    assert p.returncode == 0, f'stdout:{p.stdout}\nstderr:{p.stderr}'

    # verify CSV contents
    with out.open() as f:
        reader = csv.DictReader(f)
        rows = list(reader)
    # Expect two unique addresses: 0x1000 (2 occurrences), 0x2000 (1)
    assert any(r['address'] == '0x1000' and r['count'] == '2' and r['symbol'] == '<no-resolve>' for r in rows)
    assert any(r['address'] == '0x2000' and r['count'] == '1' and r['symbol'] == '<no-resolve>' for r in rows)


def test_csv_sort_and_top(tmp_path):
    log = tmp_path / 'serial.log'
    # 0xAAA x3, 0xBBB x2, 0xCCC x1
    log.write_text('a 0xAAA\na 0xAAA\na 0xAAA\nb 0xBBB\nb 0xBBB\nc 0xCCC\n')
    elf = tmp_path / 'dummy.elf'
    elf.write_text('ELF')

    out = tmp_path / 'out_sorted.csv'
    p = run_script(['--log', str(log), '--elf', str(elf), '--csv', str(out), '--no-resolve', '--sort', '--top', '2'])
    assert p.returncode == 0, p.stderr

    with out.open() as f:
        reader = csv.DictReader(f)
        rows = list(reader)
    # Only top 2 should be present and in descending order
    assert len(rows) == 2
    assert rows[0]['address'] == '0xAAA' and rows[0]['count'] == '3'
    assert rows[1]['address'] == '0xBBB' and rows[1]['count'] == '2'


def test_timeline_no_resolve(tmp_path):
    log = tmp_path / 'serial.log'
    log.write_text('first 0x1\nsecond 0x2 0x1\n')
    elf = tmp_path / 'dummy.elf'
    elf.write_text('ELF')

    out = tmp_path / 'serial.symbolized.log'
    p = run_script(['--log', str(log), '--elf', str(elf), '--no-resolve', '-o', str(out)])
    assert p.returncode == 0, p.stderr

    content = out.read_text()
    # should contain arrow lines with <no-resolve>
    assert '-> 0x1' in content and '<no-resolve>' in content
    assert '-> 0x2' in content
