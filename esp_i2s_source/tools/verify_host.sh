#!/usr/bin/env bash
# One-command host-side verification: everything that does not need the
# physical S3/WROOM32 hardware. Run before every commit in the repair loop.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

./tools/run_host_tests.sh --strict
./tools/run_host_tests.sh --strict --asan
./tools/run_host_tests.sh --strict --ubsan
python3 -m pytest -q tools/test_s3_gate_assert.py

if command -v npm >/dev/null 2>&1; then
    # --if-present: frontend unit tests land in a later phase (TODO Phase
    # 10). Until then there is no "test" script and this is a no-op, not
    # a failure.
    (cd web && npm ci && npm run build && npm test --if-present)
else
    echo "verify_host.sh: npm not found — skipping web build/test (install Node to run them)" >&2
fi
