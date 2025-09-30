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

