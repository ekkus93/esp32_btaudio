#!/usr/bin/env bash
# Run all laptop-BT integration tests.
# Usage: tools/run_laptop_bt_tests.sh [pytest extra args]
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
conda run -n python310 python -m pytest \
    "$ROOT/test/laptop_bt_tests/" \
    -m laptop_bt \
    -v \
    "$@"
