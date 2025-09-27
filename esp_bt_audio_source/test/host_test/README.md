Host unit tests for esp_bt_audio_source

Quick start (Linux)

1. Install prerequisites:

   sudo apt-get update -y
   sudo apt-get install -y build-essential cmake pkg-config git

2. Build and run tests:

   mkdir -p build_host_tests
   cd build_host_tests
   cmake ..
   cmake --build . -- -j$(nproc)
   ./test_commands || true
   ./test_bluetooth || true
   ./test_nvs_storage || true

Notes
- The host test harness compiles production code with `ESP_PLATFORM` undefined and links in mocks found in `mocks/`.
- If you see errors about missing types (for example `size_t`) or missing esp macros (for example `ESP_OK`), ensure the mock headers under `mocks/` include the basic standard headers or define required esp error codes.
- Tests are intentionally quiet on failure (they return non-zero on assertion), use the test binary output for debugging.

If you want, add this workflow file to CI so tests run automatically on PRs: `.github/workflows/ci-host-tests.yml` (already added in this repo).
