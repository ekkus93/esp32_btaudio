# Host-side Unit Tests (quick start)

This short README explains how to run the host-based unit tests for `esp_bt_audio_source` on your development machine. The host tests compile production code with `ESP_PLATFORM` undefined and use mock implementations (in `test/host_test/mocks/`) so tests run on Linux quickly.

Quick steps
1. Create and enter a build directory for host tests:

```bash
cd /home/phil/work/esp32/esp32_btaudio/esp_bt_audio_source/test/host_test
mkdir -p build_host_tests && cd build_host_tests
cmake ..
```

2. Build the test runner (example target `test_commands`):

```bash
cmake --build . --target test_commands -j$(nproc)
```

3. Run the test binary and capture output:

```bash
./test_commands |& tee test_commands.log
echo "exit code: $?"
```

Where the mocks live
- `test/host_test/mocks/esp_bt.h` and `mock_gap.c` — Bluetooth GAP and stack mocks
- `test/host_test/mocks/nvs_storage_mock.c` — NVS persistence mock
- `test/host_test/mocks/mock_i2s.c`, `mock_uart.c` — peripheral mocks

If a test fails
- Inspect the failing test output in `test_commands.log`.
- Adjust or extend the mocks under `test/host_test/mocks/` to simulate the expected platform behaviour.

Notes
- Host tests are fast and are the preferred development loop for command parsing and business logic.
- For on-device integration and timing-sensitive tests, use the `test_app` Unity tests described in the main README.
# Host test quick start

This directory contains host-run unit tests for the project. The tests compile the production code for a host environment and link small mocks for ESP-IDF APIs so tests run on your Linux machine without flashing a device.

Quick steps
-----------
1. Create and enter a build directory for host tests:

```bash
cd /home/phil/work/esp32/esp32_btaudio/esp_bt_audio_source/test/host_test
mkdir -p build_host_tests && cd build_host_tests
cmake ..
```

2. Build the test runner (example: `test_commands` or `test_osi_allocator`):

```bash
cmake --build . --target test_commands -j$(nproc)
# or build the allocator test runner
cmake --build . --target test_osi_allocator -j$(nproc)
```

3. Run a test binary and capture output:

```bash
./test_commands 2>&1 | tee test_commands.log
echo "exit code: $?"
```

Useful debugging tips
---------------------
- Run a single test binary under gdb to get a backtrace when it crashes:

```bash
# enable core dumps (optional)
ulimit -c unlimited

gdb --args ./test_osi_allocator
# in gdb: run  -> after crash: bt full
```

- Use Valgrind to detect heap misuse (slow):

```bash
valgrind --leak-check=full --track-origins=yes ./test_osi_allocator 2>&1 | tee valgrind_test_osi_allocator.log
```

- If a test needs verbose allocator or logging traces, enable the `UNIT_TEST_VERBOSE` macro in the test target or temporarily enable debug prints in the source. Prefer gating prints behind a macro so they can be toggled without changing logic.

- Host mocks live under `test/host_test/mocks/`. If a test fails due to missing behavior, extend the mocks (for example `mock_gap.c`, `nvs_storage_mock.c`, `esp_log.h`) to simulate IDF behavior expected by the code under test.

- When adding or changing host tests, run the full host test suite (`ctest` or run binaries manually) to ensure there are no regressions.

CI / Automation notes
---------------------
- These host tests are well-suited for CI; add a job to run them on every PR. For test binaries that require specific environment variables or tools (valgrind), gate those steps behind separate CI stages.

- Keep `HEAP_MEMORY_DEBUG` and other debug flags scoped to test targets (CMake's `target_compile_definitions(... PRIVATE ...)`) so production builds are unaffected.

Contact / Troubleshooting
-------------------------
If you run into build errors referencing missing ESP-IDF symbols, ensure you've configured the host-test CMake correctly and that mocks are present. If a runtime crash reproduces only on-device, collect device serial logs and a backtrace (use `idf.py monitor` and enable backtrace decoding with the elf file).

Host tests (fast)

This folder contains host-based unit tests that run on your development machine (Linux). They compile production code with `ESP_PLATFORM` undefined and link mock implementations from `mocks/` so you can run tests without device hardware.

1. Install prerequisites:

   sudo apt-get update -y
   sudo apt-get install -y build-essential cmake pkg-config git

2. Create a build directory and configure:

```bash
cd /home/phil/work/esp32/esp32_btaudio/esp_bt_audio_source/test/host_test
mkdir -p build_host_tests && cd build_host_tests
cmake ..
```

2. Build test executables (examples):

```bash
cmake --build . --target test_commands -j$(nproc)
cmake --build . --target test_bluetooth -j$(nproc)
cmake --build . --target test_nvs_storage -j$(nproc)
```

3. Run all tests with CTest:

```bash
ctest --output-on-failure -j1
```

Or run a single test binary directly:

```bash
./test_commands |& tee test_commands.log
echo "exit code: $?"
```

What the mocks do
-----------------
- `mocks/mock_gap.c` provides fake GAP reply functions used by `components/command_interface/commands.c` when `ESP_PLATFORM` is undefined.
- `mocks/nvs_storage_mock.c` implements a simple in-memory NVS replacement used by tests that exercise persistence logic.
- `mocks/mock_uart.c` and `mocks/mock_i2s.c` provide minimal stubs to satisfy build-time dependencies.

Troubleshooting
---------------
- If CTest says "No tests were found", ensure you configured the build directory with `cmake ..` after pulling recent changes. The CMake file registers tests with CTest using `enable_testing()` and `add_test()`.
- If a test fails, inspect `test/host_test/mocks/` to adjust simulated behavior for the failing case.
- To regenerate CMake files from scratch, delete the `build_host_tests` directory and repeat step 1.

Notes for contributors
----------------------
- Keep host tests fast and deterministic. Use the mocks to simulate error paths and timing where appropriate.
- When adding new tests that require new mocks, put them under `test/host_test/mocks/` and include them in the corresponding `add_executable()` call in `CMakeLists.txt`.

Contact
-------
If you need help running the tests, open an issue or ping the maintainer in the repo.

