# ESP-IDF Stubs for Host Testing

## Purpose

This directory contains **minimal ESP-IDF component files** needed for compiling host tests on x86/x64 Linux. These files are extracted from ESP-IDF v5.5.1 to allow host tests to link against Bluetooth stack utilities without requiring the full ESP-IDF toolchain.

## Contents

```
esp_idf_stubs/
└── bt/common/              # Bluetooth stack common utilities
    ├── include/            # BT common headers
    │   ├── bt_common.h
    │   └── bt_user_config.h
    └── osi/                # OS abstraction layer
        ├── allocator.c     # Memory allocator (used by test_list_ownership, test_osi_allocator)
        ├── list.c          # Linked list implementation (used by test_list_ownership)
        └── include/osi/    # OSI headers (14 files)
            ├── allocator.h
            ├── list.h
            └── ... (12 more headers)
```

## Files

- **Total:** 18 files (2 source + 16 headers)
- **Size:** ~156KB
- **Source:** ESP-IDF v5.5.1 `components/bt/common/` directory
- **Used by:** `test_list_ownership`, `test_osi_allocator` host tests

## Why Not Use Full ESP-IDF?

**Previous approach (components/components/):**
- Mirrored entire ESP-IDF component tree (80+ components)
- Size: 300MB, 16,750 files
- Usage: Only 2 source files + headers actually used (99.9% waste)
- Confusion: Nested naming, unclear ownership

**Current approach (esp_idf_stubs/):**
- Extract only what's needed
- Size: 156KB, 18 files
- Clear intent: Test fixtures, not production code
- Easy to understand and maintain

## Maintenance

### If ESP-IDF is upgraded

1. Check if bt/common/osi files changed (unlikely - stable interface)
2. If needed, re-copy from new ESP-IDF version:
   ```bash
   cd $IDF_PATH
   cp components/bt/common/osi/allocator.c <project>/test/host_test/esp_idf_stubs/bt/common/osi/
   cp components/bt/common/osi/list.c <project>/test/host_test/esp_idf_stubs/bt/common/osi/
   cp components/bt/common/osi/include/osi/*.h <project>/test/host_test/esp_idf_stubs/bt/common/osi/include/osi/
   cp components/bt/common/include/*.h <project>/test/host_test/esp_idf_stubs/bt/common/include/
   ```
3. Run host tests: `cd test/host_test && cmake .. && cmake --build . && ctest`

### If host tests need additional ESP-IDF files

1. Copy minimal required files (source + headers) to appropriate subdirectory
2. Update test/host_test/CMakeLists.txt to reference them
3. Keep directory structure matching ESP-IDF (e.g., `esp_common/include/`, `log/include/`)
4. Document additions in this README

## History

- **2026-02-11:** Created by extracting from `components/components/` (CODE_REVIEW 2602101453 P2.1)
- **Previous:** Used wholesale ESP-IDF mirror at `components/components/` (300MB, 2026-02-03)
- **Reduction:** 300MB → 156KB (99.95% size reduction)

## References

- **ESP-IDF v5.5.1:** https://github.com/espressif/esp-idf/tree/v5.5.1
- **BT Common OSI:** `components/bt/common/osi/` in ESP-IDF
- **Host Test Design:** See `../README.md` or `../../README_TESTS.md`
- **Migration:** See `../../code_review/CodeReview2602101453_TODO.md` P2.1 tasks

---

**Last updated:** 2026-02-11  
**ESP-IDF version:** v5.5.1  
**Maintainer:** Phil  
**Related:** CODE_REVIEW 2602101453 P2.1 - Extract Essentials (Option 3)
