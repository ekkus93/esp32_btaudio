#!/usr/bin/env python3
"""
Deprecated helper wrapper.

This script was kept for backward-compatibility but the repository now
consolidates test-running behavior in `esp_bt_audio_source/tools/run_unity.py`.
Please use that script instead. This wrapper will print a short message and
exit with a non-zero status to avoid accidental use.
"""

import sys
print("flash_and_watch.py is deprecated — use esp_bt_audio_source/tools/run_unity.py instead", file=sys.stderr)
print("Example: python3 esp_bt_audio_source/tools/run_unity.py --project-root esp_bt_audio_source/test_app --port /dev/ttyUSB0 --timeout 600", file=sys.stderr)
sys.exit(2)
