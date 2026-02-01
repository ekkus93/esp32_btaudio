# Running tests for esp_bt_audio_source

This document explains how to run the Python and C host/unit tests for the
`esp_bt_audio_source` component. It assumes you have a working Python 3.10
environment (the repository was developed with Python 3.10) and a host C build
toolchain (gcc, cmake, make).

## Python tests (host tools)

1. Create/activate a virtualenv or use your conda env for Python 3.10.

2. Install pinned dependencies for the tools/tests:

```bash
python -m pip install -r esp_bt_audio_source/tools/requirements.txt
```

3. Run the pytest suite for the tools only (fast):

```bash
# from repo root
pytest -q esp_bt_audio_source/tools
```

Notes:
- The repository contains many upstream component tests; running `pytest` with
  a broad path (for example `pytest esp_bt_audio_source`) may collect tests
  outside this component and require additional environment or native tools.
- Use the repo-level `pytest.ini` which defaults discovery to
  `esp_bt_audio_source/tools`.

## C host / Unity unit tests

These tests are in `esp_bt_audio_source/test/host_test` and are built with
CMake. They are host-executable unit tests (Unity) that don't require flashing
or an ESP device.

### Quick start (using Makefile - RECOMMENDED)

```bash
# from repo root
cd esp_bt_audio_source/test/host_test
make test
```

This performs a **clean build** from scratch and runs all tests, exactly matching
the GitHub Actions CI workflow. This is the recommended way to test before pushing.

### Manual build steps

```bash
# from repo root
cd esp_bt_audio_source/test/host_test
cmake -S . -B build_host_tests
cmake --build build_host_tests -- -j$(nproc)
cd build_host_tests
ctest --output-on-failure
```

### Complete test suite (using run_all_tests.py)

The main test runner at `tools/run_all_tests.py` now includes standalone host tests
by default. This provides CI parity checking:

```bash
# from repo root
python3 tools/run_all_tests.py --no-device
```

This will run:
1. Regular host tests (incremental build)
2. **Standalone host tests** (clean build, matches CI exactly)

To skip standalone tests:
```bash
python3 tools/run_all_tests.py --no-device --no-standalone
```

## Pre-commit testing checklist

**IMPORTANT:** Before committing changes that touch production code, follow this
checklist to avoid CI failures:

### 1. Run standalone host tests (CI parity check)

```bash
cd esp_bt_audio_source/test/host_test
make test
```

**Why?** This catches missing mock implementations and linker errors that incremental
builds might miss. It runs exactly as GitHub Actions CI does.

### 2. Check for new functions requiring mocks

If you added new functions to components (e.g., `nvs_storage`, `bt_manager`),
check if they're called by code that gets linked into host tests:

```bash
# Find functions in production code
grep -r "nvs_storage_.*(" esp_bt_audio_source/components/nvs_storage/nvs_storage.h

# Check if corresponding mocks exist
ls -la esp_bt_audio_source/test/host_test/mocks/*mock*.c
```

Common mocks that need updates:
- `test/host_test/mocks/nvs_storage_mock.c` - NVS storage functions
- `test/host_test/mocks/mock_*.c` - Various ESP-IDF peripherals

### 3. Install pre-push hook (RECOMMENDED)

Automatically run standalone tests before every push:

```bash
# from repo root
cp tools/hooks/pre-push .git/hooks/pre-push
chmod +x .git/hooks/pre-push
```

This will block pushes that would break CI. To bypass temporarily (use sparingly):
```bash
git push --no-verify
```

## CI parity testing

The standalone host test build exactly matches GitHub Actions CI:
- **Clean build** from scratch (no incremental state)
- Strict linker checking (catches missing symbols immediately)  
- Tests all mock implementations are complete

**Key lesson from v0.2.0 release:** The audio autostart feature added two new
NVS functions (`nvs_storage_get_audio_autostart`, `nvs_storage_set_audio_autostart`)
that weren't mocked. Incremental builds didn't catch this, but CI did. The
standalone build now prevents this class of error.

What I changed to make host builds work
- `main/include/audio_processor.h` now guards ESP-IDF-only includes with
  `#ifdef ESP_PLATFORM` and provides small fallbacks for host builds. This
  allows building the host tests without the full ESP-IDF headers present.

If you prefer the fallback types to live in a test-only shim instead of the
public header, I can move the definitions into `test/host_test/include` and
adjust the CMake for test builds.

## Troubleshooting
- If the C build fails due to missing ESP-IDF headers, ensure you're building
  the host tests (not target firmware) and that `ESP_PLATFORM` is not defined
  when building for host.
- If pytest discovers unexpected upstream tests, run pytest against the exact
  directory you want (for example `pytest esp_bt_audio_source/tools`).

---


