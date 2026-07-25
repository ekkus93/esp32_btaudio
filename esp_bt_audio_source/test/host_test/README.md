# Host-side Unit Tests (quick start)

This directory contains host-run unit tests for `esp_bt_audio_source`. They compile production code with `ESP_PLATFORM` undefined and link mock implementations of ESP-IDF APIs (in `test/host_test/mocks/`) so tests run on your Linux machine quickly, without flashing a device.

## Prerequisites

```bash
sudo apt-get update -y
sudo apt-get install -y build-essential cmake pkg-config git
```

## Quick steps

1. Create and enter a build directory for host tests:

```bash
cd /home/phil/work/esp32/esp32_btaudio/esp_bt_audio_source/test/host_test
mkdir -p build_host_tests && cd build_host_tests
cmake ..
```

2. Build every test binary so they register with CTest (or build one target, e.g. `test_commands`, `test_bluetooth`, `test_nvs_storage`):

```bash
cmake --build . -j"$(nproc)"
# or a single target:
cmake --build . --target test_commands -j"$(nproc)"
```

3. Run the full suite with CTest, or a single binary directly:

```bash
ctest --output-on-failure

# or run one binary directly and capture output
./test_commands 2>&1 | tee test_commands.log
echo "exit code: $?"
```

## Where the mocks live

- `mocks/mock_gap.c` — Bluetooth GAP reply mocks used by `components/command_interface/commands.c` when `ESP_PLATFORM` is undefined
- `mocks/nvs_storage_mock.c` — simple in-memory NVS replacement for tests that exercise persistence logic
- `mocks/mock_uart.c`, `mocks/mock_i2s.c`, `mocks/mock_i2s_std.c` — peripheral stubs to satisfy build-time dependencies

## Debugging tips

- Run a single test binary under gdb to get a backtrace when it crashes:

```bash
ulimit -c unlimited   # enable core dumps (optional)
gdb --args ./test_osi_allocator
# in gdb: run  -> after crash: bt full
```

- Use Valgrind to detect heap misuse (slow):

```bash
valgrind --leak-check=full --track-origins=yes ./test_osi_allocator 2>&1 | tee valgrind_test_osi_allocator.log
```

- If a test needs verbose allocator or logging traces, enable the `UNIT_TEST_VERBOSE` macro in the test target or temporarily enable debug prints in the source. Prefer gating prints behind a macro so they can be toggled without changing logic.

## CI / Automation notes

- These host tests are well-suited for CI; add a job to run them on every PR. For test binaries that require specific environment variables or tools (valgrind), gate those steps behind separate CI stages.
- Keep `HEAP_MEMORY_DEBUG` and other debug flags scoped to test targets (CMake's `target_compile_definitions(... PRIVATE ...)`) so production builds are unaffected.

## Troubleshooting

- If CTest says "No tests were found", ensure you configured the build directory with `cmake ..` after pulling recent changes. The CMake file registers tests with CTest using `enable_testing()` and `add_test()`.
- If a test fails, inspect the failing test's output/log and adjust or extend the mocks under `test/host_test/mocks/` to simulate the expected platform behaviour.
- To regenerate CMake files from scratch, delete the `build_host_tests` directory and repeat the quick steps above.
- If you run into build errors referencing missing ESP-IDF symbols, ensure the mocks needed by the code under test are present and linked into that test's `add_executable()` call in `CMakeLists.txt`.
- If a runtime crash reproduces only on-device, collect device serial logs and a backtrace (use `idf.py monitor` and enable backtrace decoding with the ELF file).

## Notes for contributors

- Host tests are fast and are the preferred development loop for command parsing and business logic. For on-device integration and timing-sensitive tests, use the `test_bluetooth`/`test_app_audio`/`test_manager` Unity suites described in the project's main README.
- Keep host tests fast and deterministic. Use the mocks to simulate error paths and timing where appropriate.
- When adding new tests that require new mocks, put them under `test/host_test/mocks/` and include them in the corresponding `add_executable()` call in `CMakeLists.txt`.
