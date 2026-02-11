# Unit Test Exclusions Analysis

**Date:** 2026-02-11  
**Task:** CodeReview2602101453_TODO.md A2

## Overview

This document analyzes all `#ifndef UNIT_TEST` usage in the codebase, categorizes each by necessity, and provides recommendations for improving testability while maintaining pragmatic boundaries.

**Total Occurrences:** 19 instances across 6 files

---

## Categories of Exclusions

### Category 1: Platform-Specific Headers (NECESSARY)

**Rationale:** These headers define FreeRTOS/ESP-IDF types and APIs that don't exist in host builds. No alternative without complete platform simulation.

| File | Line | Content | Why Excluded |
|------|------|---------|--------------|
| audio_processor_internal.h | 9 | `#include "freertos/event_groups.h"` | EventGroupHandle_t type unavailable in host |
| audio_processor_internal.h | 13 | `#include "esp_timer.h"` | esp_timer_handle_t type unavailable in host |
| audio_ringbuffer.c | 45 | `#include "freertos/portmacro.h"` | portMUX_TYPE unavailable (has `#else` fallback) |

**Assessment:** ✅ **Keep as-is** - These are genuinely platform-specific with no reasonable host alternative.

**Note:** audio_ringbuffer.c has the best pattern - includes `freertos/semphr.h` in the `#else` clause to provide portENTER_CRITICAL/portEXIT_CRITICAL macros for host tests. This is a good example of minimal exclusion.

---

### Category 2: Platform-Specific Variables (NECESSARY)

**Rationale:** Variables declared with FreeRTOS/ESP-IDF types that don't exist in host builds.

| File | Line Range | Content | Why Excluded |
|------|------------|---------|--------------|
| audio_processor_internal.h | 118-130 | Task handle, event groups, cooperative shutdown | TaskHandle_t, EventGroupHandle_t unavailable |
| audio_processor_internal.h | 138 | `esp_timer_handle_t s_volume_commit_timer` | ESP-IDF timer type unavailable |
| audio_processor_state.c | 20-52 | TaskHandle, EventGroupHandle definitions | FreeRTOS types unavailable |

**Assessment:** ✅ **Keep as-is** - Type dependencies make these genuinely platform-specific.

**Opportunity:** Could expose getters/setters for these variables that return mock-friendly primitive types (bool, int) to allow state inspection in tests without exposing FreeRTOS types.

---

### Category 3: Task Lifecycle Code (PARTIALLY IMPROVABLE)

**Rationale:** Code that manages FreeRTOS tasks, events, timers - depends on platform but logic could be tested with mocks.

| File | Line | Content | Why Excluded | Testability |
|------|------|---------|--------------|-------------|
| audio_processor.c | 56-68 | `volume_commit_timer_callback()` | Uses esp_timer callback | Could mock timer |
| audio_processor.c | 223-228 | Signal ENGINE_STOPPED_BIT on error | Uses EventGroup | Could mock event group |
| audio_processor.c | 236-241 | Signal ENGINE_RUNNING_BIT on startup | Uses EventGroup | Could mock event group |
| audio_processor.c | 355-360 | Signal ENGINE_STOPPED_BIT on shutdown | Uses EventGroup | Could mock event group |
| audio_processor.c | 536-554 | Create engine event group | Uses xEventGroupCreate | Could mock creation |
| audio_processor.c | 598-634 | Create audio_engine_task | Uses xTaskCreate | Could mock creation |
| audio_processor.c | 661-709 | Cooperative shutdown logic | Uses task notification, event wait | Could mock handshake |
| audio_processor.c | 784-789 | Delete engine event group | Uses vEventGroupDelete | Could mock cleanup |

**Assessment:** ⚠️ **Improvable with effort** - Logic is testable but requires FreeRTOS simulator or comprehensive mocking.

**Current State:** These exclusions prevent testing critical paths:
- Task creation/deletion lifecycle (P0.1 cooperative shutdown)
- Event group handshake logic
- Timer debounce behavior

**Options:**
1. **FreeRTOS Simulator:** Use FreeRTOS POSIX port or FakeOS to run tasks in host tests
2. **Comprehensive Mocking:** Mock xTaskCreate, xEventGroupCreate, etc. (heavy maintenance burden)
3. **Keep Excluded:** Accept that task lifecycle is device-test-only (current approach)

**Recommendation:** **Option 3 (keep)** for MVP, consider Option 1 (FreeRTOS simulator) for Phase 2 if task lifecycle bugs become frequent.

---

### Category 4: Entire Task Function (PLATFORM-SPECIFIC)

**Rationale:** The `audio_engine_task()` function (lines 70-365 in audio_processor.c) is wrapped entirely in `#ifndef UNIT_TEST`.

**Why:** Function body uses:
- `vTaskDelay()` for timing
- `ulTaskNotifyTake()` for shutdown signal
- `xEventGroupSetBits()` for handshake
- `vTaskDelete(NULL)` for self-deletion

**Assessment:** ✅ **Keep as-is** - Task function is inherently platform-specific.

**Mitigation:** Extract testable logic from task:
- `get_active_source()` - ✅ Already testable (not excluded)
- `produce_audio_chunk()` - ✅ Already testable (not excluded)
- `beep_overlay_fill()` - ✅ Already testable (not excluded)

**Current Coverage:** Task *orchestration* is device-only, but *business logic* is host-testable. This is a good separation.

---

### Category 5: Device-Only Tests (TEST ORGANIZATION)

**Rationale:** Some tests require real FreeRTOS timing (`vTaskDelay`) and task lifecycle to validate behavior.

| File | Line | Content | Why Excluded |
|------|------|---------|--------------|
| test_audio_processor.c | 287 | `test_audio_processor_beep_disables_synth_keepalive` | Requires `vTaskDelay` for beep timing |
| test_audio_processor.c | 400 | `test_f1_beep_preempts_and_restores_i2s` | Requires task delays and timing |
| test_audio_processor.c | 648-654 | F1.7 integration tests (6 tests) | Require real task timing |
| i2s_audio_test.c | 12 | `#define UNIT_TEST` | **BUG:** Device test shouldn't define UNIT_TEST |

**Assessment:** ⚠️ **Mixed - needs cleanup**

**Issues:**
1. **i2s_audio_test.c line 12:** This is a *device test* but it `#define UNIT_TEST`. This is backwards - UNIT_TEST should only be defined for host tests.
2. **Test organization:** Device-only tests are sprinkled in test_audio_processor.c. Ideally, device-specific tests would be in separate files or clearly marked sections.

**Recommendation:** 
1. **Remove** `#define UNIT_TEST` from i2s_audio_test.c (file is already in test_app_audio, which is device-only)
2. **Keep** `#ifndef UNIT_TEST` guards in test_audio_processor.c for timing-dependent tests
3. **Document** in test file headers which tests require device (FreeRTOS) vs host (no FreeRTOS)

---

## Cognitive Load Assessment

**Current State:**
- 19 `#ifndef UNIT_TEST` instances across 6 files
- Mix of necessary (headers, types) and defensible (task code) exclusions
- Generally well-documented (WHY comments present in most cases)

**Cognitive Load Sources:**
1. ✅ **Well-handled:** Headers and types clearly platform-specific
2. ✅ **Well-handled:** Task function exclusion is reasonable
3. ⚠️ **Moderate load:** Task lifecycle code scattered across audio_processor.c (8 instances)
4. ❌ **Confusing:** i2s_audio_test.c defines UNIT_TEST in a device test (contradiction)

**Improvement Impact:**
- **High impact, low effort:** Fix i2s_audio_test.c UNIT_TEST definition bug
- **Medium impact, high effort:** Consolidate task lifecycle exclusions into wrapper functions
- **Low impact, very high effort:** Add FreeRTOS simulator for full task testing

---

## Testability Assessment

### Currently Testable in Host Tests ✅

**Business Logic (no FreeRTOS dependency):**
- `get_active_source()` - source priority/arbitration
- `produce_audio_chunk()` - chunk generation logic
- `beep_overlay_fill()` - beep mixing
- `audio_processor_set_synth_mode()` - mode transitions
- `audio_processor_beep_tone()` - beep API (minus task interaction)
- `audio_rb_*()` - ring buffer operations

**Coverage:** ~80% of audio processor business logic is host-testable.

### Device-Only (FreeRTOS required) ⚠️

**Task Lifecycle:**
- Task creation/destruction
- Cooperative shutdown handshake (P0.1)
- Event group synchronization
- Timer callbacks (volume commit debounce)

**Real-Time Behavior:**
- 2ms tick rate adherence
- I2S timeout vs tick interplay
- Beep timing and restoration

**Coverage:** ~20% of code requires device tests (primarily orchestration, not logic).

### Not Testable Currently ❌

**Nothing significant.** All critical logic paths have either host or device test coverage.

---

## Recommendations

### Immediate (Low Effort, High Value)

1. **Fix i2s_audio_test.c bug (5 min)**
   - Remove `#define UNIT_TEST` from line 12
   - Verify test still runs in test_app_audio device suite
   - Commit: "fix: remove incorrect UNIT_TEST define from device test"

2. **Add WHY comments to remaining exclusions (15 min)**
   - audio_ringbuffer.c line 45: Explain portMUX alternative in else clause
   - Standardize format: `/* WHY excluded: [reason] */`

3. **Document test types in test headers (10 min)**
   - test_audio_processor.c: Add header comment explaining host vs device tests
   - test_app_audio tests: Clarify these are device-only

### Short-Term (Medium Effort, Medium Value)

4. **Consolidate task lifecycle exclusions (2-3 hours)**
   - Extract `task_lifecycle_init()`, `task_lifecycle_start()`, `task_lifecycle_stop()` wrappers
   - Single `#ifndef UNIT_TEST` per wrapper function (reduce 8 instances to 3)
   - Benefits: Clearer separation, easier to mock in future

5. **Add state inspection helpers (1 hour)**
   - `audio_processor_is_task_running()` → returns bool (testable primitive)
   - `audio_processor_get_event_group_state()` → returns uint32_t bits (testable primitive)
   - Allows host tests to verify state without accessing FreeRTOS types

### Long-Term (High Effort, Uncertain Value)

6. **Investigate FreeRTOS POSIX port (1-2 days)**
   - Research: Can FreeRTOS POSIX simulator run in CTest?
   - POC: Run audio_engine_task() in host test with pthread-based FreeRTOS
   - Decision: Adopt if <20% effort increase for >50% coverage gain

7. **Split test suites more formally (1 day)**
   - `test/host_test/` - pure logic, no FreeRTOS (current: 250 tests)
   - `test/device_test/` - timing, tasks, hardware (current: 99 tests via test_*/main/)
   - `test/integration_test/` - full stack (BT + audio + I2S)
   - Benefits: Clearer expectations, faster CI (host tests run first)

---

## Summary

**Current State: GOOD** ✅
- Necessary exclusions are well-justified (headers, types, task code)
- Business logic is 80% host-testable (excellent separation)
- Only 1 bug found (i2s_audio_test.c incorrect UNIT_TEST define)

**Cognitive Load: LOW-MODERATE**
- 19 instances is reasonable for a 15K+ LOC embedded project
- Most have good WHY documentation
- Task lifecycle exclusions are the main "cognitive load" but defensible

**Improvement Priority:**
1. **Must-do:** Fix i2s_audio_test.c bug
2. **Should-do:** Add WHY comments to all exclusions
3. **Nice-to-have:** Consolidate task lifecycle wrappers
4. **Future:** Consider FreeRTOS simulator if task lifecycle bugs become frequent

**Defer indefinitely:**
- Comprehensive mocking of FreeRTOS (high maintenance burden)
- Forcing all code to be host-testable (violates pragmatic boundaries)

---

## Appendix: Complete Location Index

### By File

**audio_processor_internal.h** (4 instances)
- Line 9: `#include "freertos/event_groups.h"` - Header
- Line 13: `#include "esp_timer.h"` - Header  
- Line 118-130: TaskHandle, EventGroup externs - Variables
- Line 138: Timer handle extern - Variable

**audio_processor_state.c** (1 instance)
- Line 20-52: TaskHandle, EventGroup definitions - Variables

**audio_ringbuffer.c** (1 instance)
- Line 45: `#include "freertos/portmacro.h"` - Header (has else clause) ✅ Good pattern

**audio_processor.c** (8 instances)
- Line 56: `volume_commit_timer_callback()` - Function
- Line 223: Error path event signal - Logic
- Line 236: Startup event signal - Logic
- Line 355: Shutdown event signal - Logic
- Line 536: Event group creation - Lifecycle
- Line 598: Task creation - Lifecycle
- Line 661: Cooperative shutdown - Lifecycle
- Line 784: Event group cleanup - Lifecycle

**test_audio_processor.c** (4 instances)
- Line 287: Beep timing test - Device test
- Line 400: F1.7.1 test - Device test
- Line 648-654: F1.7 test suite - Device tests

**i2s_audio_test.c** (1 instance)
- Line 12: `#define UNIT_TEST` - **BUG** (device test shouldn't define this)

---

**Last Updated:** 2026-02-11  
**Related Tasks:** CodeReview2602101453_TODO.md A2
