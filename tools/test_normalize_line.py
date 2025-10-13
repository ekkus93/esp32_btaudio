import os
import sys

# Ensure tools package can be imported from repo root
repo_root = os.path.dirname(os.path.dirname(__file__))
sys.path.insert(0, repo_root + '/tools')


def test_strip_idf_prefix():
    from flash_and_watch import normalize_line

    raw = 'I (123) ----- UNITY TEST START -----\n'
    assert normalize_line(raw) == '----- UNITY TEST START -----'


def test_strip_ansi_codes():
    from flash_and_watch import normalize_line

    raw = '\x1b[32mOK\x1b[0m\n'
    assert normalize_line(raw) == 'OK'


def test_strip_leading_dashes():
    from flash_and_watch import normalize_line

    raw = '--- esp-idf-monitor: some header\n11 Tests 0 Failures 0\n'
    # leading '---...' line removed, remaining line preserved and stripped
    assert normalize_line(raw) == '11 Tests 0 Failures 0'


def test_no_change_for_clean_line():
    from flash_and_watch import normalize_line

    raw = 'All tests completed.'
    assert normalize_line(raw) == 'All tests completed.'
