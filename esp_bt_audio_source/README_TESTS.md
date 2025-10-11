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

Steps:

```bash
# from repo root
cd esp_bt_audio_source/test/host_test
cmake -S . -B build_host_tests
cmake --build build_host_tests -- -j$(nproc)
cd build_host_tests
ctest --output-on-failure
```

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

If you want, I can:
- Add a small `scripts/run_tests.sh` wrapper that runs both Python and C tests.
- Move the host build fallbacks into a test-only shim instead of editing the
  public header.

