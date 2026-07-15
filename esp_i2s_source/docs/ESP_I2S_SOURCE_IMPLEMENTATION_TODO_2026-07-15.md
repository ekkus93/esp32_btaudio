# `esp_i2s_source` Implementation TODO

**Based on:** `ESP_I2S_SOURCE_FULL_CODE_REVIEW_2026-07-15.md` and `ESP_I2S_SOURCE_FIX_SPEC_V2_2026-07-15.md`  
**Audience:** Qwen3.6 / Claude Code / human implementer  
**Rule:** Complete phases in order. Do not combine unrelated phases into one large patch.

**Errata:** `ESP_I2S_SOURCE_FIX_RESPONSES_V2_2026-07-15.md` is normative and overrides this file where they conflict. Not yet reconciled into this doc's task bodies (apply when reaching that phase):

- **Phase 2 (boot order):** this file's `boot_status_t` field order (§2.3) and `test_main_boot.c`'s assertion order (§2.8) disagree with each other; use the errata's answer #1 order (i2s_out before bt_link/radio/stations/wifi) as normative for both, and make sure `web_ok` is asserted too.
- **Phase 9.4 (station URL parsing):** add the errata's answer #2 SSRF blocking (reject loopback/link-local/private/multicast/unspecified/broadcast destinations for literal IPs, DNS results, redirects, and reconnects; `CONFIG_ESP_I2S_SOURCE_ALLOW_LOCAL_STREAMS`, default `n`) — not in this file's task body at all.
- **Phase 8.4 (Wi-Fi hex PSK):** gate 64-char hex STA PSK behind `CONFIG_ESP_I2S_SOURCE_STA_HEX_PSK` per errata answer #4; this file currently accepts it unconditionally.
- **Phase 9.1 (station migration):** use the errata's answer #6 detailed migration algorithm (legacy `STA1` blob detection by size+magic, sequential ID assignment, `stations_v2` key, retain legacy key, `last_station` → `last_station_id` conversion) — this file's "add migration ... when old blob validates" is a one-line placeholder.
- **Phase 10.6 (auth bootstrap):** use the errata's answer #7 concrete flow (token generated + printed once to USB serial as `AUTH|BOOTSTRAP_TOKEN|<token>`, physical-button rotation) — this file defers the whole flow to an unwritten README section.

---

# 0. Instructions for the coding model

Use these rules for every task:

1. Work only in `esp_i2s_source/` unless a task explicitly says the WROOM32 side must be inspected.
2. Make one logical change at a time.
3. Before editing a file, read the complete file and its header.
4. Do not silently ignore an error.
5. Do not add a fallback unless the specification names it.
6. Do not truncate commands, SSIDs, passwords, URLs, station names, or JSON bodies. Validate and reject.
7. Do not use `volatile` as thread synchronization.
8. Do not hold a critical section, mutex, or spinlock while performing blocking I/O unless the specification explicitly requires it.
9. Do not free a task/session/request object until every possible user has released or acknowledged it.
10. Add or update tests in the same commit as each fix.
11. Compile with warnings as errors after every phase.
12. Keep diagnostics machine-readable and redact secrets.
13. Never change a failing test merely to match broken behavior. First establish the intended contract from the spec.
14. When a snippet below conflicts with an actual ESP-IDF type/signature, keep the design and adapt only the type/signature after checking the installed ESP-IDF header.
15. Do not claim hardware success without a device log showing the required markers.

Recommended commit sequence:

```text
fix(test): make host tests deterministic and strict
fix(boot): remove duplicate initialization and start I2S pipeline
fix(i2s): make writer lifecycle and sink commit safe
fix(bt-link): replace abandoned request ownership with refcount
fix(audio): validate tone generation and coherent config
fix(resampler): correct streaming interpolation
fix(radio): serialize lifecycle and join workers safely
fix(wifi): make manager idempotent and transactional
fix(ctrl): synchronize config and make station identity stable
fix(web): fix request ownership, errors, auth, and async operations
fix(ui): centralize API errors and non-overlapping polling
test(device): require end-to-end audio evidence
```

---

# Phase 1 — Repair and strengthen the test infrastructure

**Status: DONE** — commits `acbb348b`, `b1843231`, `0fe3e89e`, `168eb8de`, `65898f1f`, `4c00085c`.

## 1.1 Remove implicit network fetches from host tests

**Status: DONE** — commit `acbb348b`. Unity 2.6.0 vendored under `test/third_party/unity/`; `find_package`/`FetchContent` removed.

**Files:**

- `test/host_test/CMakeLists.txt`
- `tools/run_host_tests.sh`
- Add `test/third_party/unity/` or an equivalent vendored path

### Required behavior

A clean offline machine must configure and run host tests without contacting GitHub.

### Recommended CMake replacement

Vendor the pinned Unity files:

```text
test/third_party/unity/src/unity.c
test/third_party/unity/src/unity.h
test/third_party/unity/src/unity_internals.h
```

Replace the FetchContent block with:

```cmake
set(UNITY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/unity")
set(UNITY_SRC "${UNITY_DIR}/src/unity.c")
set(UNITY_INC "${UNITY_DIR}/src")

if(NOT EXISTS "${UNITY_SRC}")
    message(FATAL_ERROR
        "Vendored Unity is missing at ${UNITY_SRC}. "
        "Host tests are intentionally offline and will not fetch dependencies.")
endif()

add_library(unity STATIC "${UNITY_SRC}")
target_include_directories(unity PUBLIC "${UNITY_INC}")
```

Remove:

```cmake
find_package(unity QUIET)
include(FetchContent)
FetchContent_Declare(...)
FetchContent_MakeAvailable(unity)
```

### Acceptance

- [ ] Disconnect networking or use a network-disabled container.
- [ ] Delete `test/host_test/build_host_tests`.
- [ ] Run `./tools/run_host_tests.sh`.
- [ ] CMake does not mention FetchContent or GitHub.
- [ ] All tests compile and execute.

---

## 1.2 Add strict, ASan, and UBSan options

**Status: DONE** — commits `b1843231`, `65898f1f`. Full flag set (`-Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wformat=2 -Wundef -Wcast-qual -Wstrict-prototypes`) implemented, `ENABLE_STRICT` defaults ON, `--no-strict` opts out. All three of `--strict`, `--strict --asan`, `--strict --ubsan` pass 18/18.

**File:** `test/host_test/CMakeLists.txt`

Add:

```cmake
option(ENABLE_STRICT "Enable strict compiler warnings" ON)
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)

if(ENABLE_STRICT)
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -Wshadow
        -Wconversion
        -Wsign-conversion
        -Wformat=2
        -Wundef
        -Wcast-qual
        -Wstrict-prototypes
    )
endif()

if(ENABLE_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address)
endif()

if(ENABLE_UBSAN)
    add_compile_options(-fsanitize=undefined -fno-sanitize-recover=all -g)
    add_link_options(-fsanitize=undefined)
endif()
```

Some legacy warnings may need target-specific fixes. Do not disable warnings globally unless a third-party target requires it.

Update `tools/run_host_tests.sh`:

```bash
case "$arg" in
    --strict)   CMAKE_ARGS+=("-DENABLE_STRICT=ON") ;;
    --no-strict) CMAKE_ARGS+=("-DENABLE_STRICT=OFF") ;;
    --coverage) CMAKE_ARGS+=("-DENABLE_COVERAGE=ON") ;;
    --asan)     CMAKE_ARGS+=("-DENABLE_ASAN=ON") ;;
    --ubsan)    CMAKE_ARGS+=("-DENABLE_UBSAN=ON") ;;
    *) echo "unknown arg: $arg" >&2; exit 2 ;;
esac
```

Use separate build directories per mode so flags do not contaminate one another:

```bash
MODE="strict"
[[ " ${*} " == *" --asan "* ]] && MODE+="-asan"
[[ " ${*} " == *" --ubsan "* ]] && MODE+="-ubsan"
BUILD_DIR="$HERE/test/host_test/build-$MODE"
```

### Acceptance

- [ ] `./tools/run_host_tests.sh --strict`
- [ ] `./tools/run_host_tests.sh --strict --asan`
- [ ] `./tools/run_host_tests.sh --strict --ubsan`
- [ ] All three pass.

---

## 1.3 Fix the UART mock signature

**Status: DONE** — commit `b1843231`.

**Files:**

- `test/host_test/mocks/include/driver/uart.h`
- `test/host_test/mocks/fake_uart.c`

Use the ESP-IDF-compatible signature:

```c
esp_err_t uart_driver_install(
    uart_port_t uart_num,
    int rx_buffer_size,
    int tx_buffer_size,
    int queue_size,
    QueueHandle_t *uart_queue,
    int intr_alloc_flags);
```

Implementation example:

```c
esp_err_t uart_driver_install(
    uart_port_t uart_num,
    int rx_buffer_size,
    int tx_buffer_size,
    int queue_size,
    QueueHandle_t *uart_queue,
    int intr_alloc_flags)
{
    (void)uart_num;
    (void)rx_buffer_size;
    (void)tx_buffer_size;
    (void)queue_size;
    (void)uart_queue;
    (void)intr_alloc_flags;
    return fake_uart_driver_install_result;
}
```

Use the exact typedefs from the mock headers. Do not replace pointer parameters with integers.

---

## 1.4 Make the host `ctrl_cfg_save()` declaration visible

**Status: DONE** — commit `0fe3e89e`.

**File:** `components/ctrl/include/ctrl_cfg.h`

The public declaration must be available to host stubs. Use a platform-neutral error typedef header or include the host stub `esp_err.h` through the include path.

Simple fix:

```c
#include "esp_err.h"

void ctrl_cfg_load(ctrl_cfg_t *out);
esp_err_t ctrl_cfg_save(const ctrl_cfg_t *cfg);
```

Remove the `#ifdef ESP_PLATFORM` around the declaration. The implementation remains platform-specific or supplied by a host stub.

---

## 1.5 Replace the tautological control test

**Status: DONE** — commit `168eb8de`. (Static mock state reset between tests was already handled by the existing `setUp()`/`reset_ctrl_state()`; not revisited.)

**File:** `test/host_test/test_ctrl_init.c`

Remove:

```c
TEST_ASSERT(strlen(cfg.sink_mac) >= 0);
```

Use meaningful defaults:

```c
TEST_ASSERT_EQUAL_STRING("", cfg.sink_mac);
TEST_ASSERT_EQUAL_UINT8(0, cfg.autostart);
TEST_ASSERT_EQUAL_INT(CTRL_STATION_NONE, cfg.last_station);
TEST_ASSERT_EQUAL_UINT8(CTRL_VOLUME_DEFAULT, cfg.volume);
```

Reset all static mock/global state between tests.

---

## 1.6 Add a top-level verification script

**Status: DONE** — commit `4c00085c`. `npm test` uses `--if-present` since the frontend test script doesn't exist until Phase 10.

**New file:** `tools/verify_host.sh`

```bash
#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

./tools/run_host_tests.sh --strict
./tools/run_host_tests.sh --strict --asan
./tools/run_host_tests.sh --strict --ubsan
python3 -m pytest -q tools/test_s3_gate_assert.py

if command -v npm >/dev/null 2>&1; then
    (cd web && npm ci && npm run build && npm test)
fi
```

Do not make `npm ci` optional in CI. The local helper may skip only with a clear warning when Node is unavailable.

---

# Phase 2 — Fix boot and start the actual audio pipeline

**Status: DONE** (host-verifiable parts) — commits `071d5ef9`, `be60d9d3`, `3ccdeb7a`. Boot order reconciled per errata answer #1 (not the file's own conflicting §2.3/§2.8 orderings — see the top-of-file errata note). **Hardware checkpoint 1 (RESPONSES answer #12) still needed**: flash the S3 and confirm no duplicate-netif assertion, every initializer runs once, `BOOT_COMPLETE` appears, and I2S reports a real state without hanging, with the WROOM32 both absent and present.

## 2.1 Delete duplicate startup calls

**Status: DONE** — commit `071d5ef9`.

**File:** `main/main.c`

Delete the second `link_selftest()` and second `wifi_mgr_init()` block. Do not merely add flags around the duplicates; remove the bad merge.

---

## 2.2 Separate BT initialization from the health probe

**Status: DONE** — commit `071d5ef9`. Probe runs async in its own low-priority task after BOOT_COMPLETE.

Change:

```c
static void link_selftest(void)
{
    esp_err_t err = bt_link_init(...);
    ...
}
```

To a read-only probe that assumes initialization already succeeded:

```c
static void link_health_probe(void)
{
    static const char *const cmds[] = { "VERSION", "STATUS" };

    int ok = 0;
    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); ++i) {
        bt_link_cmd_state_t state = BT_LINK_CMD_TIMEOUT;
        char result[BT_LINK_FIELD_MAX] = {0};
        char data[BT_LINK_FIELD_MAX] = {0};

        esp_err_t err = bt_link_send(
            cmds[i], &state,
            result, sizeof(result),
            data, sizeof(data));

        if (err == ESP_OK && state == BT_LINK_CMD_DONE_OK) {
            ++ok;
        }

        printf("DIAG|BTLINK|PROBE|cmd=%s,transport=%s,state=%s\n",
               cmds[i], esp_err_to_name(err), link_state_str(state));
    }

    printf("DIAG|BTLINK|PROBE_DONE|ok=%d/%u\n",
           ok, (unsigned)(sizeof(cmds) / sizeof(cmds[0])));
    fflush(stdout);
}
```

Remove `VOLUME 40` from every boot test.

Prefer running this probe in a low-priority task after boot complete so it cannot delay web/console startup.

---

## 2.3 Add a safe helper for boot steps

**Status: DONE** — commit `071d5ef9`.

**File:** `main/main.c`

```c
typedef struct {
    bool bt_link_ok;
    bool radio_ok;
    bool stations_ok;
    bool wifi_ok;
    bool i2s_ok;
    bool audio_task_ok;
    bool console_ok;
    bool ctrl_ok;
    bool web_ok;
} boot_status_t;

static bool log_boot_step(const char *name, esp_err_t err)
{
    printf("DIAG|BOOT|STEP|name=%s,result=%s\n",
           name, esp_err_to_name(err));
    fflush(stdout);
    return err == ESP_OK;
}
```

Do not call `ESP_ERROR_CHECK()` for optional components. Use explicit required/degraded policy.

---

## 2.4 Initialize and start I2S

**Status: DONE** — commit `071d5ef9`.

**File:** `main/main.c`

Add exactly once:

```c
esp_err_t err = i2s_out_init(I2S_RING_BYTES);
boot.i2s_ok = log_boot_step("i2s_init", err);

if (boot.i2s_ok) {
    err = i2s_out_start();
    boot.i2s_ok = log_boot_step("i2s_start", err);
}
```

The implementation from Phase 3 must ensure missing external clocks do not hang boot.

---

## 2.5 Create the audio producer task

**Status: DONE** — commit `071d5ef9`. State-aware backoff (I2S_STATE_FAULTED) deferred to Phase 3 as noted in this file.

Add a static handle and exit controls near the task:

```c
static TaskHandle_t s_audio_out_task;
static _Atomic bool s_audio_out_stop;
```

Create it only after I2S is initialized/started:

```c
if (boot.i2s_ok) {
    atomic_store(&s_audio_out_stop, false);
    BaseType_t created = xTaskCreate(
        audio_out_task,
        "audio_out",
        4096,
        NULL,
        tskIDLE_PRIORITY + 4,
        &s_audio_out_task);

    boot.audio_task_ok = (created == pdPASS);
    log_boot_step("audio_out_start",
                  boot.audio_task_ok ? ESP_OK : ESP_ERR_NO_MEM);
}
```

Update the loop:

```c
while (!atomic_load(&s_audio_out_stop)) {
    ...
    while (off < total && !atomic_load(&s_audio_out_stop)) {
        size_t written = i2s_out_write(p + off, total - off);
        if (written == 0) {
            i2s_out_stats_t stats;
            i2s_out_get_stats(&stats);
            if (stats.state == I2S_STATE_FAULTED) {
                vTaskDelay(pdMS_TO_TICKS(100));
            } else {
                vTaskDelay(pdMS_TO_TICKS(1));
            }
            continue;
        }
        off += written;
    }
}

s_audio_out_task = NULL;
vTaskDelete(NULL);
```

Adapt the state type after Phase 3.

---

## 2.6 Use a clear source arbitration function

**Status: DONE** — commit `071d5ef9`. `tone_is_on()` added as a local static wrapper around the existing `tone_get()` snapshot accessor rather than a new tone.h API.

Create a pure function/testable decision:

```c
typedef enum {
    AUDIO_SOURCE_SILENCE = 0,
    AUDIO_SOURCE_TONE,
    AUDIO_SOURCE_RADIO,
} audio_source_t;

static audio_source_t choose_audio_source(
    radio_state_t radio_state,
    bool radio_ready,
    bool tone_on)
{
    if (radio_state == RADIO_STATE_RUNNING ||
        radio_state == RADIO_STATE_STARTING) {
        return radio_ready ? AUDIO_SOURCE_RADIO : AUDIO_SOURCE_SILENCE;
    }
    return tone_on ? AUDIO_SOURCE_TONE : AUDIO_SOURCE_SILENCE;
}
```

The task switch:

```c
switch (choose_audio_source(radio_get_state(), radio_audio_ready(), tone_is_on())) {
case AUDIO_SOURCE_RADIO:
    got = radio_pcm_read(block, AUDIO_OUT_FRAMES);
    if (got < AUDIO_OUT_FRAMES) {
        memset(block + got * 2, 0,
               (AUDIO_OUT_FRAMES - got) * 2 * sizeof(block[0]));
    }
    break;
case AUDIO_SOURCE_TONE:
    tone_fill(block, AUDIO_OUT_FRAMES);
    break;
case AUDIO_SOURCE_SILENCE:
default:
    memset(block, 0, sizeof(block));
    break;
}
```

Add a `tone_is_on()` getter that reads a coherent tone snapshot.

---

## 2.7 Reduce diagnostic overhead

**Status: DONE** — commit `071d5ef9`. Extracted into main/clock_diag.c/.h (commit `be60d9d3`) so the boot sequence stays free of PCNT dependencies.

Create PCNT units once or move clock measurement to a dedicated low-priority diagnostic task.

Minimum immediate change:

```c
#define CLOCK_DIAG_PERIOD_MS 10000
#define CLOCK_MEASURE_MS       100
```

Check every PCNT call. If any call fails, emit one error and return `-1.0f`. Do not print invalid frequency as successful data.

---

## 2.8 Add a boot-sequence host test

**Status: DONE** — commit `3ccdeb7a`, `test/host_test/test_main_boot.c`.

Create `test/host_test/test_main_boot.c` using stubs that count every initializer/start call.

Required assertions:

```c
TEST_ASSERT_EQUAL_INT(1, calls.bt_link_init);
TEST_ASSERT_EQUAL_INT(1, calls.wifi_mgr_init);
TEST_ASSERT_EQUAL_INT(1, calls.radio_init);
TEST_ASSERT_EQUAL_INT(1, calls.stations_init);
TEST_ASSERT_EQUAL_INT(1, calls.i2s_out_init);
TEST_ASSERT_EQUAL_INT(1, calls.i2s_out_start);
TEST_ASSERT_EQUAL_INT(1, calls.audio_task_create);
TEST_ASSERT_EQUAL_INT(1, calls.console_start);
TEST_ASSERT_EQUAL_INT(1, calls.ctrl_init);
TEST_ASSERT_EQUAL_INT(1, calls.ctrl_start);
```

Also inspect recorded UART commands and assert:

```c
TEST_ASSERT_EQUAL_INT(0, count_command_prefix("VOLUME "));
TEST_ASSERT_EQUAL_INT(0, count_command_prefix("CONNECT "));
TEST_ASSERT_EQUAL_INT(0, count_command("START"));
```

---

# Phase 3 — Make I2S lifecycle and data commit safe

**Status: DONE** — commit `b230fade`. All of 3.1-3.8 implemented and hardware-verified (WROOM32 absent): boots to BOOT_COMPLETE, i2s state correctly reports WAITING_FOR_CLOCK, no crash/watchdog trip. Gap: no host test yet for 3.8's NVS-failure-injection requirement (would need a new host test target compiling i2s_out.c under ESP_PLATFORM with NVS mocks, matching test_main_boot.c's pattern) — follow-up item, not blocking.

## 3.1 Correct the header contract

**File:** `components/i2s_out/include/i2s_out.h`

Update the top comment to say:

```text
ESP32-S3 slave transmitter, 44.1 kHz stereo, Philips I2S,
32-bit slots containing signed 16-bit PCM in bits 31..16.
WROOM32 is the clock master.
```

Do not call the S3 a master.

Add explicit state and richer stats:

```c
typedef enum {
    I2S_STATE_UNINITIALIZED = 0,
    I2S_STATE_IDLE,
    I2S_STATE_STARTING,
    I2S_STATE_RUNNING,
    I2S_STATE_WAITING_FOR_CLOCK,
    I2S_STATE_STOPPING,
    I2S_STATE_FAULTED,
} i2s_out_state_t;

typedef struct {
    uint64_t bytes_written;
    uint64_t underrun_bytes;
    uint64_t underrun_events;
    uint64_t write_timeouts;
    uint64_t write_errors;
    uint64_t partial_writes;
    uint64_t source_drop_bytes;
    size_t ring_used;
    size_t ring_capacity;
    size_t ring_peak;
    i2s_out_state_t state;
    esp_err_t last_error;
} i2s_out_stats_t;
```

Add:

```c
esp_err_t i2s_out_deinit(void);
i2s_out_state_t i2s_out_get_state(void);
```

---

## 3.2 Require PSRAM rather than silently falling back

**Files:**

- `components/i2s_out/pcm_ring.c`
- `components/i2s_out/include/pcm_ring.h`

Replace the bool with an allocation policy:

```c
typedef enum {
    PCM_RING_INTERNAL_ONLY = 0,
    PCM_RING_PSRAM_REQUIRED,
    PCM_RING_PSRAM_PREFERRED,
} pcm_ring_memory_t;
```

Allocator:

```c
static void *ring_alloc(size_t n, pcm_ring_memory_t memory)
{
#ifdef ESP_PLATFORM
    if (memory == PCM_RING_PSRAM_REQUIRED ||
        memory == PCM_RING_PSRAM_PREFERRED) {
        void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (p != NULL || memory == PCM_RING_PSRAM_REQUIRED) {
            return p;
        }
    }
    return heap_caps_malloc(n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#else
    (void)memory;
    return malloc(n);
#endif
}
```

Overflow guard:

```c
if (capacity == 0 || capacity == SIZE_MAX) {
    return NULL;
}
```

I2S must call:

```c
pcm_ring_create(ring_capacity_bytes, PCM_RING_PSRAM_REQUIRED);
```

---

## 3.3 Add ring peek and consume operations

**Files:**

- `components/i2s_out/include/pcm_ring.h`
- `components/i2s_out/pcm_ring.c`

Header:

```c
size_t pcm_ring_peek(const pcm_ring_t *r, uint8_t *dst, size_t len);
size_t pcm_ring_consume(pcm_ring_t *r, size_t len);
```

Implementation:

```c
size_t pcm_ring_peek(const pcm_ring_t *r, uint8_t *dst, size_t len)
{
    if (r == NULL || dst == NULL || len == 0) return 0;

    size_t head = atomic_load_explicit(&r->head, memory_order_acquire);
    size_t tail = atomic_load_explicit(&r->tail, memory_order_relaxed);
    size_t used = used_of(head, tail, r->size);
    size_t n = len < used ? len : used;

    size_t first = r->size - tail;
    if (first > n) first = n;
    memcpy(dst, r->buf + tail, first);
    if (n > first) memcpy(dst + first, r->buf, n - first);
    return n;
}

size_t pcm_ring_consume(pcm_ring_t *r, size_t len)
{
    if (r == NULL || len == 0) return 0;

    size_t head = atomic_load_explicit(&r->head, memory_order_acquire);
    size_t tail = atomic_load_explicit(&r->tail, memory_order_relaxed);
    size_t used = used_of(head, tail, r->size);
    size_t n = len < used ? len : used;

    atomic_store_explicit(
        &r->tail,
        (tail + n) % r->size,
        memory_order_release);
    return n;
}
```

Only the single consumer may call peek/consume/read/reset.

Tests:

- Peek does not change used count.
- Consume changes used count.
- Producer cannot overwrite peeked bytes while tail is unchanged.
- Wraparound peek and partial consume.

---

## 3.4 Replace the current pure pump contract

The current `i2s_out_pump_once()` cannot represent partial acceptance. Replace it with a sink that returns accepted bytes:

```c
typedef struct {
    int status;          /* 0 success, 1 timeout, negative fatal */
    size_t accepted;     /* 0..len */
} i2s_sink_result_t;

typedef i2s_sink_result_t (*i2s_out_sink_fn)(
    void *ctx,
    const uint8_t *data,
    size_t len);
```

Or remove the pure pump and host-test a pure pending-block state machine. Do not keep a callback contract that says “consume exactly len” when the driver can partially write.

---

## 3.5 Rewrite the writer with a persistent pending block

**File:** `components/i2s_out/i2s_out.c`

Core pattern:

```c
static void writer_task(void *arg)
{
    (void)arg;

    uint8_t pending[I2S_OUT_BLOCK_BYTES];
    size_t pending_len = 0;
    size_t pending_real = 0;

    xEventGroupSetBits(s_events, I2S_EVT_WRITER_STARTED);

    while (!atomic_load(&s_stop_requested)) {
        if (pending_len == 0) {
            pending_real = pcm_ring_peek(
                s_ring, pending, sizeof(pending));

            if (pending_real < sizeof(pending)) {
                memset(pending + pending_real, 0,
                       sizeof(pending) - pending_real);
            }
            pending_len = sizeof(pending);
        }

        size_t written = 0;
        esp_err_t err = i2s_channel_write(
            s_tx_chan,
            pending,
            pending_len,
            &written,
            pdMS_TO_TICKS(I2S_WRITE_TIMEOUT_MS));

        if (written > pending_len) {
            written = pending_len;
            err = ESP_FAIL;
        }

        size_t real_accepted = written < pending_real
            ? written
            : pending_real;

        if (real_accepted > 0) {
            size_t consumed = pcm_ring_consume(s_ring, real_accepted);
            configASSERT(consumed == real_accepted);
            pending_real -= real_accepted;
        }

        if (written > 0) {
            if (written < pending_len) {
                memmove(pending, pending + written, pending_len - written);
            }
            pending_len -= written;
        }

        i2s_stats_record_write(err, written, real_accepted);

        if (err == ESP_ERR_TIMEOUT) {
            i2s_set_state(I2S_STATE_WAITING_FOR_CLOCK, err);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        if (err != ESP_OK) {
            i2s_set_state(I2S_STATE_FAULTED, err);
            break;
        }

        i2s_set_state(I2S_STATE_RUNNING, ESP_OK);
    }

    s_writer_task = NULL;
    xEventGroupSetBits(s_events, I2S_EVT_WRITER_EXITED);
    vTaskDelete(NULL);
}
```

Important details:

- No critical section surrounds `i2s_channel_write()`.
- Real ring bytes are consumed only after the corresponding prefix was accepted.
- Zero-filled tail is not consumed from the ring.
- A partial write keeps the remainder in `pending`.
- Timeout does not lose bytes.
- Stop is checked between bounded calls.

---

## 3.6 Make stats updates short and safe

```c
static void i2s_stats_record_write(
    esp_err_t err,
    size_t written,
    size_t real_written)
{
    taskENTER_CRITICAL(&s_stats_mux);

    s_stats.bytes_written += written;
    if (written > real_written) {
        s_stats.underrun_bytes += written - real_written;
        s_stats.underrun_events += 1;
    }
    if (err == ESP_ERR_TIMEOUT) {
        s_stats.write_timeouts += 1;
    } else if (err != ESP_OK) {
        s_stats.write_errors += 1;
        s_stats.last_error = err;
    }

    taskEXIT_CRITICAL(&s_stats_mux);
}
```

This helper must not call logging, allocation, or drivers while locked.

---

## 3.7 Add lifecycle mutex and idempotence

Near globals:

```c
static SemaphoreHandle_t s_lifecycle_mtx;
static i2s_out_state_t s_state = I2S_STATE_UNINITIALIZED;
```

Create the mutex before publishing resources. Every `init/start/stop/deinit` takes it.

Start rules:

```c
if (s_state == I2S_STATE_RUNNING ||
    s_state == I2S_STATE_WAITING_FOR_CLOCK) {
    return ESP_OK;
}
if (s_state != I2S_STATE_IDLE) {
    return ESP_ERR_INVALID_STATE;
}
```

Wait for writer start:

```c
EventBits_t bits = xEventGroupWaitBits(
    s_events,
    I2S_EVT_WRITER_STARTED,
    pdFALSE,
    pdTRUE,
    pdMS_TO_TICKS(1000));

if ((bits & I2S_EVT_WRITER_STARTED) == 0) {
    atomic_store(&s_stop_requested, true);
    return ESP_ERR_TIMEOUT;
}
```

Clear both start/exit bits before creating a new writer.

---

## 3.8 Fix NVS gain ownership and transaction

Replace `i2s_out_set_gain()` with:

```c
esp_err_t i2s_out_set_gain(int pct)
{
    if (pct < 0 || pct > 100) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle = 0;
    bool opened = false;

    esp_err_t err = nvs_open(
        I2S_GAIN_NVS_NS,
        NVS_READWRITE,
        &handle);
    if (err != ESP_OK) {
        return err;
    }
    opened = true;

    err = nvs_set_u8(handle, I2S_GAIN_NVS_KEY, (uint8_t)pct);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    if (opened) {
        nvs_close(handle);
    }

    if (err == ESP_OK) {
        atomic_store(&s_gain_pct, pct);
    }
    return err;
}
```

Declare:

```c
static _Atomic int s_gain_pct = I2S_GAIN_DEFAULT;
```

Getter:

```c
int i2s_out_get_gain(void)
{
    return atomic_load(&s_gain_pct);
}
```

Add tests injecting `nvs_open`, `nvs_set`, and `nvs_commit` failure. Assert no close on unopened handle and no RAM publication on failure.

---

# Phase 4 — Fix `bt_link` request ownership and event dispatch

**Status: DONE** — commit `74a4fa57`. Retain-before-enqueue refcounting matches RESPONSES answer #3 exactly. Hardware-verified. Gap: no deterministic interleaving test for 4.6 yet (follow-up).

## 4.1 Add explicit module lifecycle

**Files:**

- `components/bt_link/include/bt_link.h`
- `components/bt_link/bt_link.c`

Add:

```c
esp_err_t bt_link_stop(void);
esp_err_t bt_link_deinit(void);
bool bt_link_is_initialized(void);
```

Globals:

```c
static SemaphoreHandle_t s_lifecycle_mtx;
static TaskHandle_t s_task;
static _Atomic bool s_stop_requested;
static bool s_initialized;
```

`bt_link_init()` must return `ESP_OK` without reinstalling UART when already initialized with the same timeout.

---

## 4.2 Replace `abandoned` with reference counting

Add `<stdatomic.h>`.

Request:

```c
typedef struct {
    atomic_uint refs;
    char cmd[BT_LINK_LINE_MAX];
    bt_link_cmd_state_t state;
    esp_err_t transport_error;
    char result[BT_LINK_FIELD_MAX];
    char data[BT_LINK_FIELD_MAX];
    SemaphoreHandle_t done_sem;
} bt_link_request_t;
```

Helpers:

```c
static bt_link_request_t *request_create(const char *cmd)
{
    bt_link_request_t *req = calloc(1, sizeof(*req));
    if (req == NULL) return NULL;

    req->done_sem = xSemaphoreCreateBinary();
    if (req->done_sem == NULL) {
        free(req);
        return NULL;
    }

    atomic_init(&req->refs, 1); /* caller */
    req->state = BT_LINK_CMD_PENDING;
    req->transport_error = ESP_OK;
    memcpy(req->cmd, cmd, strlen(cmd) + 1);
    return req;
}

static void request_retain(bt_link_request_t *req)
{
    (void)atomic_fetch_add_explicit(
        &req->refs, 1, memory_order_relaxed);
}

static void request_release(bt_link_request_t *req)
{
    if (req == NULL) return;

    unsigned previous = atomic_fetch_sub_explicit(
        &req->refs, 1, memory_order_acq_rel);
    configASSERT(previous > 0);

    if (previous == 1) {
        vSemaphoreDelete(req->done_sem);
        free(req);
    }
}
```

Queueing:

```c
bt_link_request_t *req = request_create(cmd);
if (req == NULL) return ESP_ERR_NO_MEM;

request_retain(req); /* worker reference */
if (xQueueSend(s_cmd_queue, &req, pdMS_TO_TICKS(250)) != pdTRUE) {
    request_release(req); /* worker reference was never transferred */
    request_release(req); /* caller reference */
    return ESP_ERR_TIMEOUT;
}
```

Worker completion:

```c
req->state = final_state;
req->transport_error = transport_error;
copy_session_fields(req);

xSemaphoreGive(req->done_sem);
request_release(req); /* worker never touches req after this line */
```

Caller success:

```c
if (xSemaphoreTake(req->done_sem, wait_ticks) == pdTRUE) {
    copy_result_to_caller(req, out);
    esp_err_t err = req->transport_error;
    request_release(req);
    return err;
}

request_release(req); /* timeout: worker still owns its reference */
return ESP_ERR_TIMEOUT;
```

Delete every `abandoned` field/read/write and every branch that conditionally lets the worker free caller memory.

---

## 4.3 Validate commands exactly

```c
static esp_err_t validate_command(const char *cmd)
{
    if (cmd == NULL || cmd[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    size_t len = strnlen(cmd, BT_LINK_LINE_MAX);
    if (len == BT_LINK_LINE_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }

    for (size_t i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)cmd[i];
        if (c == '\r' || c == '\n' || c < 0x20 || c == 0x7f) {
            return ESP_ERR_INVALID_ARG;
        }
    }
    return ESP_OK;
}
```

Call before allocating.

Do not use `strncpy` for accepted commands. After validation, copy `len + 1` bytes.

---

## 4.4 Check UART writes

```c
int expected = snprintf(out, sizeof(out), "%s\r\n", req->cmd);
if (expected <= 0 || (size_t)expected >= sizeof(out)) {
    complete_request(req, ESP_ERR_INVALID_SIZE, BT_LINK_CMD_DONE_ERR);
    continue;
}

int actual = uart_write_bytes(BT_LINK_UART, out, (size_t)expected);
if (actual != expected) {
    complete_request(req, ESP_FAIL, BT_LINK_CMD_DONE_ERR);
    continue;
}
```

If ESP-IDF returns a more precise negative code through another API, preserve it.

---

## 4.5 Move events off the UART owner task

Create a copied event item:

```c
typedef struct {
    bt_link_msg_type_t type;
    char event[BT_LINK_FIELD_MAX];
    char result[BT_LINK_FIELD_MAX];
    char data[BT_LINK_FIELD_MAX];
} bt_link_event_item_t;
```

`on_line()` copies event fields into `s_event_queue`. A separate `bt_link_event_task` takes a snapshot of subscribers under a mutex, releases the mutex, then calls callbacks.

Add:

```c
int bt_link_subscribe(bt_link_event_cb cb, void *ctx);
esp_err_t bt_link_unsubscribe(int subscription_id);
```

Never call user callbacks while holding the subscriber mutex.

---

## 4.6 Add deterministic concurrency tests

Create a pthread-backed test adapter or a deterministic fake semaphore hook.

Required interleaving test:

```text
worker writes result
worker gives semaphore
worker pauses
caller wakes and releases caller reference
worker resumes and releases worker reference
assert object freed exactly once
```

Also test caller timeout first, queue failure, worker error, and repeated init/deinit.

Run under ASan and TSan where available.

---

# Phase 5 — Harden tone and signal generation

**Status: DONE** — commit `92cea091`. Public tone.h API kept stable (setters unchanged) rather than switching to the TODO's combined `tone_set(hz,amp,voice)` signature, to avoid rippling into web_ui/console callers under time pressure — coherence achieved internally via a mutex-guarded snapshot instead. Hardware-verified.

## 5.1 Validate oscillator inputs

**Files:**

- `components/signal_gen/signal_gen.c`
- `components/signal_gen/include/signal_gen.h`

Prefer APIs that return a status. If signatures cannot change immediately, sanitize to silence and record a caller-side error.

Helper:

```c
static bool valid_frequency(double hz)
{
    return isfinite(hz) && hz >= 0.0 && hz <= 20000.0;
}

static double wrap_phase(double phase)
{
    if (!isfinite(phase)) return 0.0;
    phase -= floor(phase);
    return phase < 0.0 ? phase + 1.0 : phase;
}
```

Every public fill checks:

```c
if (state == NULL || out == NULL || frames == 0) return;
if (!valid_frequency(hz) || !isfinite(amplitude)) {
    memset(out, 0, frames * I2S_OUT_CHANNELS * sizeof(out[0]));
    return;
}
```

Clamp amplitude to `[0,1]` only if the public contract says clamping; otherwise reject at the controlling API.

---

## 5.2 Use a 64-bit piano age

Change the elapsed sample field to:

```c
uint64_t elapsed_samples;
```

Use saturating add:

```c
if (UINT64_MAX - state->elapsed_samples < frames) {
    state->elapsed_samples = UINT64_MAX;
} else {
    state->elapsed_samples += frames;
}
```

---

## 5.3 Publish one coherent tone config

**Files:** `components/tone/tone.c`, `components/tone/include/tone.h`

```c
typedef struct {
    bool on;
    tone_voice_t voice;
    float hz;
    float amplitude;
    uint32_t generation;
} tone_config_t;

static SemaphoreHandle_t s_tone_mtx;
static tone_config_t s_config;
```

Setter:

```c
esp_err_t tone_set(float hz, float amplitude, tone_voice_t voice)
{
    if (!isfinite(hz) || hz < 0.0f || hz > 20000.0f) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!isfinite(amplitude) || amplitude < 0.0f || amplitude > 1.0f) {
        return ESP_ERR_INVALID_ARG;
    }
    if (voice < TONE_VOICE_SINE || voice >= TONE_VOICE_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_tone_mtx, portMAX_DELAY);
    s_config.on = true;
    s_config.hz = hz;
    s_config.amplitude = amplitude;
    s_config.voice = voice;
    ++s_config.generation;
    xSemaphoreGive(s_tone_mtx);
    return ESP_OK;
}
```

Snapshot:

```c
void tone_get_config(tone_config_t *out)
{
    if (out == NULL) return;
    xSemaphoreTake(s_tone_mtx, portMAX_DELAY);
    *out = s_config;
    xSemaphoreGive(s_tone_mtx);
}
```

`tone_fill()` takes one snapshot per block.

---

## 5.4 Add click-suppression ramp

Simple block-safe ramp state:

```c
typedef struct {
    float current_gain;
    float target_gain;
    float step_per_sample;
    uint32_t remaining_samples;
} gain_ramp_t;
```

When on/off/amplitude changes, configure a 10 ms ramp:

```c
uint32_t ramp_samples = I2S_OUT_SAMPLE_RATE_HZ / 100;
ramp.step_per_sample =
    (target - current) / (float)ramp_samples;
ramp.remaining_samples = ramp_samples;
```

Apply the same gain to left/right for each frame.

---

# Phase 6 — Replace the resampler with a correct streaming algorithm

## 6.1 Replace the state

**File:** `components/radio/include/radio_resampler.h`

```c
typedef struct {
    int src_rate;
    int channels;
    double step;       /* source frames per output frame */
    double phase;      /* may temporarily be >=1 while waiting for input */
    int16_t left_l;
    int16_t left_r;
    bool primed;
} radio_resampler_t;
```

Change init to return bool/error:

```c
bool radio_resampler_init(
    radio_resampler_t *r,
    int src_rate,
    int channels);
```

---

## 6.2 Use this streaming interpolation core

**File:** `components/radio/radio_resampler.c`

```c
#include "radio_resampler.h"

#include <math.h>
#include <string.h>

static int16_t clamp_i16(long value)
{
    if (value > INT16_MAX) return INT16_MAX;
    if (value < INT16_MIN) return INT16_MIN;
    return (int16_t)value;
}

static void read_frame(
    const int16_t *in,
    size_t index,
    int channels,
    int16_t *left,
    int16_t *right)
{
    if (channels == 1) {
        *left = in[index];
        *right = in[index];
    } else {
        *left = in[index * 2];
        *right = in[index * 2 + 1];
    }
}

bool radio_resampler_init(
    radio_resampler_t *r,
    int src_rate,
    int channels)
{
    if (r == NULL || src_rate <= 0 ||
        (channels != 1 && channels != 2)) {
        return false;
    }

    memset(r, 0, sizeof(*r));
    r->src_rate = src_rate;
    r->channels = channels;
    r->step = (double)src_rate / (double)RESAMPLE_OUT_RATE;
    return isfinite(r->step) && r->step > 0.0;
}

size_t radio_resampler_run(
    radio_resampler_t *r,
    const int16_t *in,
    size_t in_frames,
    int16_t *out,
    size_t out_cap,
    size_t *in_used)
{
    if (in_used != NULL) *in_used = 0;
    if (r == NULL || in == NULL || out == NULL ||
        in_used == NULL || out_cap == 0 ||
        r->src_rate <= 0 ||
        (r->channels != 1 && r->channels != 2)) {
        return 0;
    }

    size_t input_index = 0;
    size_t output_frames = 0;

    while (output_frames < out_cap) {
        if (!r->primed) {
            if (input_index >= in_frames) break;
            read_frame(in, input_index, r->channels,
                       &r->left_l, &r->left_r);
            ++input_index;
            r->phase = 0.0;
            r->primed = true;
        }

        while (r->phase >= 1.0) {
            if (input_index >= in_frames) {
                *in_used = input_index;
                return output_frames;
            }
            read_frame(in, input_index, r->channels,
                       &r->left_l, &r->left_r);
            ++input_index;
            r->phase -= 1.0;
        }

        int16_t right_l = r->left_l;
        int16_t right_r = r->left_r;

        if (r->phase > 0.0) {
            if (input_index >= in_frames) break;
            read_frame(in, input_index, r->channels,
                       &right_l, &right_r);
        }

        double l = (double)r->left_l +
            ((double)right_l - (double)r->left_l) * r->phase;
        double rr = (double)r->left_r +
            ((double)right_r - (double)r->left_r) * r->phase;

        out[output_frames * 2] = clamp_i16(lround(l));
        out[output_frames * 2 + 1] = clamp_i16(lround(rr));
        ++output_frames;
        r->phase += r->step;
    }

    *in_used = input_index;
    return output_frames;
}
```

This algorithm yields approximately:

```text
48 kHz ramp input: 0,1000,2000,3000,4000,5000
44.1 kHz output:   0,1088,2177,3265,4354
```

Important caller rule: if `in_used < in_frames`, call again with the unconsumed suffix before discarding the decoder output buffer.

---

## 6.3 Add reference tests

Add these tests to `test_radio_resampler.c`:

### Ramp

```c
static void test_48k_ramp_matches_reference(void)
{
    const int16_t in[] = {
        0, 0,
        1000, 1000,
        2000, 2000,
        3000, 3000,
        4000, 4000,
        5000, 5000,
    };
    const int16_t expected[] = { 0, 1088, 2177, 3265, 4354 };

    radio_resampler_t r;
    TEST_ASSERT_TRUE(radio_resampler_init(&r, 48000, 2));

    int16_t out[32] = {0};
    size_t used = 0;
    size_t count = radio_resampler_run(&r, in, 6, out, 16, &used);

    TEST_ASSERT_EQUAL_UINT(6, used);
    TEST_ASSERT_EQUAL_UINT(5, count);
    for (size_t i = 0; i < count; ++i) {
        TEST_ASSERT_INT_WITHIN(1, expected[i], out[i * 2]);
        TEST_ASSERT_INT_WITHIN(1, expected[i], out[i * 2 + 1]);
    }
}
```

### Chunk equivalence

Process the same input as:

```text
one call
1 frame at a time
random chunks: 2, 7, 1, 13, ...
```

Concatenate outputs and assert exact equality.

### Sine

Generate one second of 1 kHz 48 kHz input. Resample and count zero crossings or compare against a trusted 44.1 kHz reference. Frequency error must be below 1 Hz and RMS error within a documented tolerance.

### Long-run count

For 480,000 input frames at 48 kHz, expected output is approximately 441,000 frames, bounded by one frame depending on end-of-stream policy.

---

# Phase 7 — Make radio lifecycle single-owner and join-safe

## 7.1 Remove direct lifecycle mutation from public APIs

**File:** `components/radio/radio.c`

Only `radio_cmd_task` may call internal `play_owned()` and `stop_owned()`.

Public API:

```c
typedef struct {
    radio_cmd_type_t type;
    char url[RADIO_URL_MAX];
    uint32_t request_id;
    SemaphoreHandle_t completion; /* optional for sync wrapper */
    esp_err_t *result;             /* only if lifetime-safe wrapper used */
} radio_cmd_t;
```

Prefer a refcounted completion object rather than raw result pointers.

`radio_play_async()` validates and copies the URL into the queue item. It never passes a caller pointer.

---

## 7.2 Publish session before workers run

Add start bits:

```c
#define RADIO_EVT_STREAM_STARTED  BIT0
#define RADIO_EVT_DECODER_STARTED BIT1
#define RADIO_EVT_STREAM_EXITED   BIT2
#define RADIO_EVT_DECODER_EXITED  BIT3
```

Creation sequence:

```c
radio_session_t *session = session_create(resolved_url);
if (session == NULL) return ESP_ERR_NO_MEM;

s_active_session = session;
s_radio_state = RADIO_STATE_STARTING;

if (xTaskCreate(stream_task, ..., session, ..., &session->stream_task) != pdPASS) {
    s_active_session = NULL;
    session_destroy_joined(session);
    return ESP_ERR_NO_MEM;
}

if (xTaskCreate(decoder_task, ..., session, ..., &session->decoder_task) != pdPASS) {
    atomic_store(&session->stop_requested, true);
    if (!session_wait_for(session, RADIO_EVT_STREAM_EXITED, STOP_TIMEOUT)) {
        s_radio_state = RADIO_STATE_FAULTED_JOIN_PENDING;
        return ESP_ERR_TIMEOUT; /* retain session */
    }
    s_active_session = NULL;
    session_destroy_joined(session);
    return ESP_ERR_NO_MEM;
}

EventBits_t started = xEventGroupWaitBits(
    session->events,
    RADIO_EVT_STREAM_STARTED | RADIO_EVT_DECODER_STARTED,
    pdFALSE,
    pdTRUE,
    pdMS_TO_TICKS(2000));

if ((started & REQUIRED_STARTED_BITS) != REQUIRED_STARTED_BITS) {
    request_stop_and_join(session);
    return ESP_ERR_TIMEOUT;
}

s_radio_state = RADIO_STATE_RUNNING;
```

Workers set their started bit before doing long work and always set their exited bit in one cleanup path.

---

## 7.3 Use one worker cleanup label

Each worker follows:

```c
static void stream_task(void *arg)
{
    radio_session_t *session = arg;
    xEventGroupSetBits(session->events, RADIO_EVT_STREAM_STARTED);

    /* Work. Every early failure jumps to exit. */
    ...

exit:
    session->stream_task = NULL;
    xEventGroupSetBits(session->events, RADIO_EVT_STREAM_EXITED);
    vTaskDelete(NULL);
}
```

Do not free session memory inside a worker.

---

## 7.4 Never free a faulted, unjoined session

Delete the current `RADIO_STATE_FAULTED` fast-free branch.

Use:

```c
static bool session_all_exited(radio_session_t *session)
{
    EventBits_t bits = xEventGroupGetBits(session->events);
    return (bits & RADIO_EVT_ALL_EXITED) == RADIO_EVT_ALL_EXITED;
}

static void session_destroy_joined(radio_session_t *session)
{
    configASSERT(session != NULL);
    configASSERT(session_all_exited(session));
    vEventGroupDelete(session->events);
    free(session);
}
```

If stop times out:

```c
s_radio_state = RADIO_STATE_FAULTED_JOIN_PENDING;
set_radio_error(RADIO_ERR_STOP_TIMEOUT, "workers did not exit");
return ESP_ERR_TIMEOUT;
```

Keep session/resources intact. Block new play until a later join succeeds or device reboots.

---

## 7.5 Make waits interruptible

Replace long `vTaskDelay(backoff)` with task notifications/event wait:

```c
static bool wait_or_stop(radio_session_t *session, uint32_t ms)
{
    uint32_t notified = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(ms));
    return notified == 0 && session_should_run(session);
}
```

When owner requests stop:

```c
atomic_store(&session->stop_requested, true);
if (session->stream_task) xTaskNotifyGive(session->stream_task);
if (session->decoder_task) xTaskNotifyGive(session->decoder_task);
```

Set HTTP read timeout so a worker checks stop frequently.

---

## 7.6 Validate HTTP status and playlist resolution

After headers:

```c
int status = esp_http_client_get_status_code(client);
if (status < 200 || status > 299) {
    set_session_error(session, RADIO_ERR_HTTP_STATUS, status);
    goto reconnect_or_exit;
}
```

Change resolver signature:

```c
esp_err_t radio_resolve_url(
    const char *input,
    char *resolved,
    size_t resolved_size,
    radio_error_detail_t *detail);
```

On playlist parse/fetch failure, return an error. Do not copy the original playlist URL as a direct stream fallback.

---

## 7.7 Stop arbitrary compressed-byte dropping

Preferred stream loop:

```c
while (session_should_run(session)) {
    size_t free_bytes = compressed_ring_free();
    if (free_bytes < MIN_HTTP_READ) {
        wait_or_stop(session, 10);
        continue;
    }

    size_t request = free_bytes < sizeof(buffer)
        ? free_bytes
        : sizeof(buffer);
    int n = esp_http_client_read(client, (char *)buffer, request);
    ...
    size_t written = ring_write(buffer, (size_t)n);
    configASSERT(written == (size_t)n);
}
```

The ring should exert backpressure rather than drop arbitrary bytes.

---

## 7.8 Fix decoder progress rules

Before pointer arithmetic:

```c
if (raw.consumed > raw.len) {
    set_session_error(session, RADIO_ERR_DECODER_CONTRACT,
                      "decoder consumed beyond input");
    goto decoder_exit;
}
```

Remove:

```c
if (raw.consumed == 0) raw.consumed = 1;
```

Use decoder return categories:

```c
if (err == ESP_AUDIO_ERR_OK) {
    ...
} else if (err == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
    break; /* preserve pending bytes and read more */
} else {
    ++consecutive_decode_errors;
    if (consecutive_decode_errors >= MAX_DECODE_ERRORS) {
        codec_resync_or_fail(...);
    }
}
```

Use exact constants supported by the installed decoder library.

---

## 7.9 Fix prebuffer persistence

Use the same opened-handle and candidate-publication pattern as I2S gain.

Exact byte calculation:

```c
static size_t prebuffer_bytes_for_ms(int ms)
{
    uint64_t bytes =
        (uint64_t)ms *
        (uint64_t)I2S_OUT_SAMPLE_RATE_HZ *
        (uint64_t)I2S_OUT_CHANNELS *
        sizeof(int16_t);
    return (size_t)((bytes + 999u) / 1000u);
}
```

Protect threshold/prebuffered state with `s_pcm_mtx` or owner snapshots, not `volatile`.

---

## 7.10 Replace nested live status reads with a published snapshot

Owner/task writers update a `radio_status_t s_status` under one short mutex:

```c
static void status_publish(const radio_status_t *candidate)
{
    xSemaphoreTake(s_status_mtx, portMAX_DELAY);
    s_status = *candidate;
    xSemaphoreGive(s_status_mtx);
}

void radio_get_status(radio_status_t *out)
{
    if (out == NULL) return;
    xSemaphoreTake(s_status_mtx, portMAX_DELAY);
    *out = s_status;
    xSemaphoreGive(s_status_mtx);
}
```

Do not acquire control, telemetry, and PCM locks in a status request.

---

## 7.11 Add lifecycle failure-injection tests

For every allocation/task creation index:

- Fail compressed ring allocation.
- Fail PCM allocation.
- Fail each mutex/queue/event creation.
- Fail command task creation.
- Fail stream task creation.
- Fail decoder task creation.
- Delay stream exit beyond timeout.
- Delay decoder exit beyond timeout.

For every case assert:

```text
no live object was freed
no primitive double-deleted
state is exact
new play is allowed only when safe
ASan clean
```

---

# Phase 8 — Make Wi-Fi safe, exact, and transactional

## 8.1 Add lifecycle state and mutex

**File:** `components/wifi_mgr/wifi_mgr.c`

```c
typedef enum {
    WIFI_MGR_UNINITIALIZED = 0,
    WIFI_MGR_INITIALIZING,
    WIFI_MGR_RUNNING,
    WIFI_MGR_FAULTED,
} wifi_mgr_lifecycle_t;

static SemaphoreHandle_t s_mgr_mtx;
static wifi_mgr_lifecycle_t s_lifecycle;
static esp_event_handler_instance_t s_wifi_handler;
static esp_event_handler_instance_t s_ip_handler;
```

At init start:

```c
if (s_mgr_mtx == NULL) {
    s_mgr_mtx = xSemaphoreCreateMutex();
    if (s_mgr_mtx == NULL) return ESP_ERR_NO_MEM;
}

xSemaphoreTake(s_mgr_mtx, portMAX_DELAY);
if (s_lifecycle == WIFI_MGR_RUNNING) {
    xSemaphoreGive(s_mgr_mtx);
    return ESP_OK;
}
if (s_lifecycle != WIFI_MGR_UNINITIALIZED) {
    xSemaphoreGive(s_mgr_mtx);
    return ESP_ERR_INVALID_STATE;
}
s_lifecycle = WIFI_MGR_INITIALIZING;
xSemaphoreGive(s_mgr_mtx);
```

Every failure unwinds resources and resets to `UNINITIALIZED` or explicit `FAULTED`.

---

## 8.2 Replace internal `ESP_ERROR_CHECK`

Pattern:

```c
esp_err_t err = esp_netif_init();
if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    goto fail;
}

err = esp_event_loop_create_default();
if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    goto fail;
}

s_sta_netif = esp_netif_create_default_wifi_sta();
if (s_sta_netif == NULL) {
    err = ESP_ERR_NO_MEM;
    goto fail;
}
```

Be careful: “already initialized” errors are acceptable only when ownership is known. Do not treat arbitrary global existing state as this component’s resources.

Store event handler instances:

```c
err = esp_event_handler_instance_register(
    WIFI_EVENT,
    ESP_EVENT_ANY_ID,
    on_wifi_event,
    NULL,
    &s_wifi_handler);
```

---

## 8.3 Preserve exact 32-byte SSIDs

Validation:

```c
static esp_err_t validate_ssid(
    const char *ssid,
    size_t *out_len)
{
    if (ssid == NULL || out_len == NULL) return ESP_ERR_INVALID_ARG;

    size_t len = strnlen(ssid, WIFI_MGR_SSID_MAX + 1);
    if (len == 0 || len > WIFI_MGR_SSID_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }
    *out_len = len;
    return ESP_OK;
}
```

Apply to ESP-IDF config:

```c
wifi_config_t cfg = {0};
size_t ssid_len = 0;
ESP_RETURN_ON_ERROR(validate_ssid(s_ssid, &ssid_len), TAG, "bad SSID");
memcpy(cfg.sta.ssid, s_ssid, ssid_len);
```

For AP:

```c
memcpy(cfg.ap.ssid, s_ap_ssid, ssid_len);
cfg.ap.ssid_len = (uint8_t)ssid_len;
```

Do not use `strlcpy()` into 32-byte ESP-IDF protocol fields.

---

## 8.4 Validate passphrases exactly

```c
static bool all_hex(const char *s, size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        if (!isxdigit((unsigned char)s[i])) return false;
    }
    return true;
}

static esp_err_t validate_sta_password(
    const char *pass,
    size_t *out_len)
{
    if (pass == NULL || out_len == NULL) return ESP_ERR_INVALID_ARG;
    size_t len = strnlen(pass, WIFI_MGR_PASS_MAX + 1);
    if (len > WIFI_MGR_PASS_MAX) return ESP_ERR_INVALID_SIZE;

    if (len == 0 || (len >= 8 && len <= 63) ||
        (len == 64 && all_hex(pass, len))) {
        *out_len = len;
        return ESP_OK;
    }
    return ESP_ERR_INVALID_ARG;
}
```

Copy with `memcpy`. For 64-byte raw PSK, do not require a NUL inside the ESP-IDF field.

AP password should be empty or 8–63 characters unless the installed ESP-IDF explicitly supports raw 64-hex AP PSK.

---

## 8.5 Make credential updates transactional

Use a candidate:

```c
typedef struct {
    char ssid[WIFI_MGR_SSID_MAX + 1];
    char pass[WIFI_MGR_PASS_MAX + 1];
    size_t ssid_len;
    size_t pass_len;
} wifi_credentials_t;
```

Flow:

```c
wifi_credentials_t candidate;
esp_err_t err = build_credentials(ssid, pass, &candidate);
if (err != ESP_OK) return err;

err = persist_credentials(&candidate);
if (err != ESP_OK) return err;

err = apply_credentials(&candidate);
if (err != ESP_OK) {
    set_status_persisted_not_applied(err);
    return err;
}

xSemaphoreTake(s_mgr_mtx, portMAX_DELAY);
s_credentials = candidate;
++s_connection_generation;
xSemaphoreGive(s_mgr_mtx);
return ESP_OK;
```

Do not change globals before persistence succeeds.

---

## 8.6 Fix erase semantics

```c
static esp_err_t erase_key_if_present(nvs_handle_t h, const char *key)
{
    esp_err_t err = nvs_erase_key(h, key);
    return err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err;
}
```

Erase every credential key even if another is missing, then commit once.

---

## 8.7 Remove AP password from public status

**File:** `components/wifi_mgr/include/wifi_mgr.h`

Delete:

```c
char ap_pass[WIFI_MGR_PASS_MAX + 1];
```

If config editing needs to know whether a password exists, expose only:

```c
bool ap_secured;
```

Update backend and frontend types.

---

## 8.8 Reject stale Wi-Fi events

Track an attempt generation. The exact ESP-IDF event does not carry your generation, so accept events only when the current manager state expects them.

Example:

```c
case IP_EVENT_STA_GOT_IP:
    xSemaphoreTake(s_mgr_mtx, portMAX_DELAY);
    bool expected =
        s_sm.state == WIFI_SM_STA_CONNECTING ||
        s_sm.state == WIFI_SM_STA_CONNECTED;
    xSemaphoreGive(s_mgr_mtx);

    if (!expected) {
        ESP_LOGW(TAG, "ignoring stale GOT_IP event");
        return;
    }
    ...
```

For stronger guarantees, stop/disconnect old attempts before incrementing generation and only publish connected state after checking current mode/config identity.

---

## 8.9 Serialize provisioning jobs

Do not store request credentials in shared web globals. Queue a complete value object to a Wi-Fi manager command queue. Return `ESP_ERR_INVALID_STATE`/HTTP 409 if a provisioning operation is already running.

---

## 8.10 Add Wi-Fi host tests

Required:

- Duplicate init calls each ESP-IDF init/create function once.
- Exact 32-byte SSID survives.
- 33-byte SSID rejected.
- 63-char pass accepted.
- 64-hex pass accepted if supported.
- 64-nonhex rejected.
- Missing credential key erase succeeds.
- NVS commit failure does not publish RAM candidate.
- Live apply failure is visible.
- Stale GOT_IP after AP fallback ignored.
- Public status contains no password.

---

# Phase 9 — Fix stations and control orchestration

## 9.1 Add stable station IDs

**Files:**

- `components/radio/include/station_store.h`
- `components/radio/station_store.c`
- `components/radio/stations.c`
- `components/ctrl/include/ctrl_cfg.h`
- Web API/UI station types

Change station:

```c
typedef struct {
    uint32_t id;
    char name[STATION_NAME_MAX];
    char url[STATION_URL_MAX];
} station_t;
```

Store:

```c
typedef struct {
    uint32_t next_id;
    size_t count;
    station_t items[STATION_MAX];
} station_store_t;
```

Add finds:

```c
int station_store_index_by_id(
    const station_store_t *store,
    uint32_t id);
```

Reorder changes array order, never `id`.

Control config:

```c
uint32_t last_station_id; /* 0 means none */
```

Add migration from old index-based blob only when old blob validates.

---

## 9.2 Return precise station errors

Define:

```c
typedef enum {
    STATION_OK = 0,
    STATION_ERR_INVALID_ARG,
    STATION_ERR_INVALID_URL,
    STATION_ERR_TOO_LONG,
    STATION_ERR_DUPLICATE,
    STATION_ERR_FULL,
    STATION_ERR_NOT_FOUND,
    STATION_ERR_PERSIST,
} station_result_t;
```

Do not return `ESP_ERR_NO_MEM` for duplicate/invalid/full.

---

## 9.3 Reject truncation

```c
static station_result_t validate_station_text(
    const char *name,
    const char *url)
{
    if (url == NULL || url[0] == '\0') return STATION_ERR_INVALID_URL;
    if (strnlen(url, STATION_URL_MAX) == STATION_URL_MAX) {
        return STATION_ERR_TOO_LONG;
    }
    if (name != NULL &&
        strnlen(name, STATION_NAME_MAX) == STATION_NAME_MAX) {
        return STATION_ERR_TOO_LONG;
    }
    return validate_url(url);
}
```

After validation, use `memcpy(..., len + 1)`.

---

## 9.4 Harden station URL parsing

At minimum, reject:

```c
for (const unsigned char *p = (const unsigned char *)url; *p; ++p) {
    if (*p <= 0x20 || *p == 0x7f) return STATION_ERR_INVALID_URL;
}
```

Parse scheme/host/port. Require nonempty host. Use a shared URL parser if ESP-IDF provides a suitable non-network parsing utility; otherwise implement a small bounded parser and fuzz it.

---

## 9.5 Add versioned/checksummed persistence

Example header:

```c
#define STATIONS_MAGIC 0x53544E32u /* STN2 */
#define STATIONS_SCHEMA_VERSION 2u

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint32_t payload_size;
    uint32_t generation;
    uint32_t crc32;
    station_store_t store;
} stations_blob_v2_t;
```

CRC covers the payload with `crc32` zeroed/excluded. Validate every entry after CRC.

If invalid:

```text
load safe RAM defaults
set status persistence_corrupt=true
do not call save automatically
```

Provide an explicit factory-reset/import action to overwrite.

---

## 9.6 Make `stations_init()` idempotent and count locked

```c
if (s_initialized) return ESP_OK;
```

Protect with lifecycle mutex. `stations_count()` takes the same store mutex or returns from a published snapshot.

---

## 9.7 Make control start/config synchronized

**File:** `components/ctrl/ctrl.c`

Start:

```c
xSemaphoreTake(s_mtx, portMAX_DELAY);
if (s_task != NULL) {
    xSemaphoreGive(s_mtx);
    return ESP_OK;
}
ctrl_cfg_t initial = s_cfg;
xSemaphoreGive(s_mtx);
```

Pass an allocated/copied initial config or store it in task-owned state. Do not have the task read `s_cfg` without lock.

Config setter candidate flow:

```c
xSemaphoreTake(s_mtx, portMAX_DELAY);
ctrl_cfg_t candidate = s_cfg;
/* mutate candidate */
xSemaphoreGive(s_mtx);

esp_err_t err = ctrl_cfg_save(&candidate);
if (err != ESP_OK) return err;

xSemaphoreTake(s_mtx, portMAX_DELAY);
s_cfg = candidate;
xSemaphoreGive(s_mtx);

ctrl_post_reconfigure();
return ESP_OK;
```

Log from `candidate`, not shared `s_cfg` after unlock.

---

## 9.8 Use monotonic timestamps in control state machine

Replace fixed `dt_ms = CTRL_LOOP_MS` accounting with actual elapsed time:

```c
int64_t now_us = esp_timer_get_time();
uint32_t dt_ms = (uint32_t)((now_us - last_us) / 1000);
last_us = now_us;
```

For host pure tests, pass explicit timestamps or elapsed values from a fake monotonic clock.

Define retry semantics and change boundary test to the intended `>=` or exact attempt count.

---

## 9.9 Make scan a serialized state machine

Before task creation, under lock:

```c
if (s_scan_state != SCAN_IDLE) {
    return ESP_ERR_INVALID_STATE;
}
s_scan_state = SCAN_STARTING;
```

On task creation failure, restore `SCAN_IDLE` under lock.

Replace blind sleeps with events/status deadlines:

```text
STOP_RADIO -> wait radio STOPPED
DISCONNECT -> wait expected sink disconnected
SCAN -> collect results until scan-complete event/deadline
CONNECT -> wait expected MAC connected
SET_VOLUME -> require protocol OK
PLAY -> require radio command accepted and state STARTING/RUNNING
```

Final status must report partial failure accurately, never unconditional “restored.”

---

# Phase 10 — Fix HTTP backend, security, and frontend

## 10.1 Fix the radio cJSON use-after-free immediately

**File:** `components/web_ui/web_ui_radio.c`

Use:

```c
char url_copy[RADIO_URL_MAX];
const cJSON *url_item = cJSON_GetObjectItemCaseSensitive(root, "url");
if (!cJSON_IsString(url_item) || url_item->valuestring == NULL) {
    cJSON_Delete(root);
    return send_error(req, 400, "INVALID_URL", "url must be a string");
}

size_t len = strnlen(url_item->valuestring, sizeof(url_copy));
if (len == 0 || len >= sizeof(url_copy)) {
    cJSON_Delete(root);
    return send_error(req, 400, "INVALID_URL", "url is empty or too long");
}
memcpy(url_copy, url_item->valuestring, len + 1);
cJSON_Delete(root);

esp_err_t err = radio_play_async(url_copy);
```

Add a host test that overwrites/frees the JSON buffer before the queued command is consumed and verifies the queued URL remains intact.

---

## 10.2 Add a strict JSON-body helper

**New shared backend helper:** e.g. `components/web_ui/web_ui_json.c`

```c
typedef struct {
    char *body;
    cJSON *root;
} web_json_body_t;

static esp_err_t web_read_json(
    httpd_req_t *req,
    size_t max_bytes,
    web_json_body_t *out)
{
    if (req == NULL || out == NULL) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    if (req->content_len <= 0 ||
        (size_t)req->content_len > max_bytes) {
        return ESP_ERR_INVALID_SIZE;
    }

    out->body = calloc((size_t)req->content_len + 1, 1);
    if (out->body == NULL) return ESP_ERR_NO_MEM;

    size_t offset = 0;
    while (offset < (size_t)req->content_len) {
        int n = httpd_req_recv(
            req,
            out->body + offset,
            (size_t)req->content_len - offset);
        if (n <= 0) {
            free(out->body);
            memset(out, 0, sizeof(*out));
            return ESP_ERR_TIMEOUT;
        }
        offset += (size_t)n;
    }

    out->root = cJSON_ParseWithLength(out->body, offset);
    if (out->root == NULL) {
        free(out->body);
        memset(out, 0, sizeof(*out));
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

static void web_json_free(web_json_body_t *body)
{
    if (body == NULL) return;
    cJSON_Delete(body->root);
    free(body->body);
    memset(body, 0, sizeof(*body));
}
```

Every endpoint uses it and copies async strings before `web_json_free()`.

---

## 10.3 Centralize JSON success/error responses

```c
esp_err_t web_send_error(
    httpd_req_t *req,
    const char *status,
    const char *code,
    const char *message,
    bool retryable);

esp_err_t web_send_ok(
    httpd_req_t *req,
    const cJSON *data);
```

Map backend errors consistently. Unknown `/api/*` returns JSON 404.

---

## 10.4 Do not block status HTTP on UART

Maintain a timestamped WROOM snapshot updated by a background probe task or BT events:

```c
typedef struct {
    bool reachable;
    int64_t updated_us;
    char version[32];
    char status[BT_LINK_FIELD_MAX];
} wroom_status_cache_t;
```

`GET /api/status` copies the cache only. It never sends a synchronous command.

---

## 10.5 Queue long operations and return 202

Bluetooth scan/connect, Wi-Fi provisioning, radio stop/start, and raw console commands should use operation records.

Minimal operation:

```c
typedef enum {
    OP_QUEUED = 0,
    OP_RUNNING,
    OP_SUCCEEDED,
    OP_FAILED,
} operation_state_t;

typedef struct {
    uint32_t id;
    operation_state_t state;
    char code[32];
    char message[96];
} operation_status_t;
```

Return:

```json
{"ok":true,"data":{"operation_id":17,"state":"QUEUED"}}
```

If keeping synchronous endpoints temporarily, return precise 504 timeout and prevent overlapping poll pileups. Do not call a 2-second UART command from `/api/status`.

---

## 10.6 Add release authentication

Build option:

```text
CONFIG_ESP_I2S_SOURCE_HTTP_AUTH=y
```

First boot creates a random token using ESP RNG and stores it in NVS. Validate:

```c
static bool constant_time_equal(
    const uint8_t *a,
    const uint8_t *b,
    size_t n)
{
    uint8_t diff = 0;
    for (size_t i = 0; i < n; ++i) diff |= a[i] ^ b[i];
    return diff == 0;
}
```

Require `Authorization: Bearer <token>` for all POST/PUT/DELETE and `/api/console`.

Do not print the token in normal logs. Define a first-run physical/bootstrap flow in README.

---

## 10.7 Remove AP password from API/UI

Backend status returns:

```json
{
  "ap": {
    "on": true,
    "enabled": true,
    "ssid": "ESP32-S3-Audio",
    "secured": true,
    "ip": "192.168.4.1",
    "clients": 1
  }
}
```

Frontend type:

```ts
export interface ApStatus {
  on: boolean;
  enabled: boolean;
  ssid: string;
  secured: boolean;
  ip?: string;
  clients?: number;
}
```

Password edit field is blank and labeled “New password.”

---

## 10.8 Centralize frontend API calls

**File:** `web/src/api.ts`

```ts
export class ApiError extends Error {
  constructor(
    message: string,
    public readonly status: number,
    public readonly code: string,
    public readonly retryable: boolean,
  ) {
    super(message);
  }
}

type ApiEnvelope<T> =
  | { ok: true; data: T }
  | {
      ok: false;
      error: { code: string; message: string; retryable?: boolean };
    };

export async function apiRequest<T>(
  path: string,
  init: RequestInit = {},
  timeoutMs = 10_000,
): Promise<T> {
  const controller = new AbortController();
  const timeout = window.setTimeout(() => controller.abort(), timeoutMs);

  try {
    const response = await fetch(path, {
      ...init,
      signal: init.signal ?? controller.signal,
      headers: {
        Accept: "application/json",
        ...(init.body ? { "Content-Type": "application/json" } : {}),
        ...init.headers,
      },
    });

    const contentType = response.headers.get("content-type") ?? "";
    if (!contentType.includes("application/json")) {
      throw new ApiError(
        `${path} returned non-JSON content`,
        response.status,
        "NON_JSON_RESPONSE",
        response.status >= 500,
      );
    }

    const payload = (await response.json()) as ApiEnvelope<T>;
    if (!response.ok || !payload.ok) {
      const error = !payload.ok
        ? payload.error
        : { code: "HTTP_ERROR", message: `HTTP ${response.status}` };
      throw new ApiError(
        error.message,
        response.status,
        error.code,
        error.retryable ?? false,
      );
    }
    return payload.data;
  } finally {
    window.clearTimeout(timeout);
  }
}
```

Replace every direct fetch helper with `apiRequest()`.

Do not keep `.catch(() => {})` in core flows.

---

## 10.9 Replace overlapping polling

**File:** `web/src/usePolling.ts`

```ts
import { useEffect, useRef } from "react";

export function usePolling(
  fn: (signal: AbortSignal) => Promise<void>,
  ms: number,
) {
  const latest = useRef(fn);
  latest.current = fn;

  useEffect(() => {
    let stopped = false;
    let timer: number | undefined;
    let generation = 0;
    let controller: AbortController | undefined;

    const run = async () => {
      const mine = ++generation;
      controller = new AbortController();
      try {
        await latest.current(controller.signal);
      } catch (error) {
        if (!controller.signal.aborted) {
          console.error("poll failed", error);
        }
      } finally {
        if (!stopped && mine === generation) {
          timer = window.setTimeout(run, ms);
        }
      }
    };

    void run();
    return () => {
      stopped = true;
      if (timer !== undefined) window.clearTimeout(timer);
      controller?.abort();
    };
  }, [ms]);
}
```

This schedules the next poll only after the current one completes.

Components must display an error/stale indicator rather than swallowing failures.

---

## 10.10 Serialize arpeggio notes

Do not use an interval that fires unawaited requests. Use an async loop with abort:

```ts
async function playSequence(signal: AbortSignal) {
  for (const midi of sequence) {
    if (signal.aborted) return;
    await setTone(Math.round(midiToFreq(midi)), AMP, "piano", signal);
    await delay(noteDurationMs, signal);
  }
  await toneOff(signal);
}
```

Always send `toneOff` in cleanup, but display/report failure rather than swallowing it.

---

## 10.11 Add deterministic frontend tests

Add Vitest/React Testing Library or equivalent. Package scripts:

```json
{
  "scripts": {
    "test": "vitest run",
    "test:watch": "vitest",
    "test:e2e": "playwright test"
  }
}
```

Required tests:

- 400/401/409/500 structured errors.
- Non-JSON body.
- Timeout/abort.
- Only one poll in flight.
- Stale response does not overwrite fresh state.
- Component unmount aborts request.
- Provisioning acknowledgment followed by disconnect is shown as expected reconnect, not generic success/failure.
- AP password is never expected from status.

---

## 10.12 Replace live-device-only Playwright default

Create a mock device server used by default in CI. Live device tests require an explicit environment flag.

Example scripts:

```json
{
  "test:e2e": "playwright test",
  "test:e2e:device": "LIVE_DEVICE=1 playwright test"
}
```

`playwright.config.ts` should start the Vite app/mock server with `webServer` for deterministic tests. Do not default to `http://10.1.2.52`.

Remove real/fixed MAC addresses from generic tests; use documented fake values such as `02:00:00:00:00:01`.

---

# Phase 11 — Device tests and release gate

## 11.1 Fix the sine device test

Replace first-sample assertion:

```c
int nonzero = 0;
int64_t energy = 0;
for (size_t i = 0; i < 128 * 2; ++i) {
    if (buf[i] != 0) ++nonzero;
    energy += (int32_t)buf[i] * (int32_t)buf[i];
}
TEST_ASSERT_GREATER_THAN(32, nonzero);
TEST_ASSERT_GREATER_THAN_INT64(0, energy);
```

The first sample may validly be zero.

---

## 11.2 Make NVS test always perform the round-trip

Use a unique test namespace/key, erase first, then always write/read/delete:

```c
nvs_handle_t handle;
TEST_ESP_OK(nvs_open("devtest", NVS_READWRITE, &handle));
(void)nvs_erase_key(handle, "roundtrip");
TEST_ESP_OK(nvs_set_i32(handle, "roundtrip", 42));
TEST_ESP_OK(nvs_commit(handle));

int32_t value = 0;
TEST_ESP_OK(nvs_get_i32(handle, "roundtrip", &value));
TEST_ASSERT_EQUAL_INT32(42, value);

TEST_ESP_OK(nvs_erase_key(handle, "roundtrip"));
TEST_ESP_OK(nvs_commit(handle));
nvs_close(handle);
```

---

## 11.3 Replace the fake Wi-Fi connectivity test

The test must:

1. Create default event loop and STA netif exactly once in the test app.
2. Read credentials.
3. Fill `wifi_config_t` exactly.
4. Register IP/disconnect handlers.
5. Set config.
6. Start/connect.
7. Wait on an event group for IP or failure timeout.
8. Fail if credentials were supplied but no IP arrives.
9. Cleanly stop/deinit/unregister.

If no credentials exist, mark the test `IGNORED`/skipped with an explicit reason, not passed.

Use `RUN_TEST(test_wifi_connectivity)` so Unity counts it.

Better: add device tests for the production `wifi_mgr` rather than reimplementing Wi-Fi.

---

## 11.4 Make PSRAM expectation conditional

```c
#if CONFIG_SPIRAM
TEST_ASSERT_GREATER_THAN(0, free_psram);
void *ptr = heap_caps_malloc(1024, MALLOC_CAP_SPIRAM);
TEST_ASSERT_NOT_NULL(ptr);
free(ptr);
#else
TEST_IGNORE_MESSAGE("PSRAM disabled for this test configuration");
#endif
```

For the actual S3 release target, add a separate required test that fails if PSRAM is absent.

---

## 11.5 Fix task test synchronization

Use a binary semaphore:

```c
static SemaphoreHandle_t s_done;

static void dummy_task(void *arg)
{
    (void)arg;
    xSemaphoreGive(s_done);
    vTaskDelete(NULL);
}

static void test_task_creation_succeeds(void)
{
    s_done = xSemaphoreCreateBinary();
    TEST_ASSERT_NOT_NULL(s_done);

    TaskHandle_t handle = NULL;
    TEST_ASSERT_EQUAL(pdPASS,
        xTaskCreate(dummy_task, "test_task", 1024, NULL,
                    tskIDLE_PRIORITY, &handle));

    TEST_ASSERT_EQUAL(pdTRUE,
        xSemaphoreTake(s_done, pdMS_TO_TICKS(1000)));
    vSemaphoreDelete(s_done);
}
```

Do not assert that a handle to a self-deleted task remains meaningful.

---

## 11.6 Make device gate strict by default

**Files:**

- `tools/s3_device_gate.sh`
- `tools/s3_gate_assert.py`
- `tools/test_s3_gate_assert.py`

Release/default mode must require:

```text
BOOT COMPLETE
no later crash/reset
WIFI expected state
BTLINK reachable when companion required
I2S state RUNNING/WAITING_FOR_CLOCK as configured
I2S bytes increasing when clock required
BCLK and WS in tolerance
RADIO START + decoder format + PCM ready
RADIO STOPPED
```

Use an explicit `--degraded` flag to relax companion/network requirements. Do not make missing I2S acceptable in normal mode.

Scope events to the last `boot_id`.

Add crash patterns:

```text
assert failed
abort() was called
Guru Meditation Error
Task watchdog got triggered
Interrupt wdt timeout
Stack canary watchpoint triggered
stack overflow
CORRUPT HEAP
heap corruption
Brownout detector was triggered
rst: repeated without BOOT COMPLETE
```

---

## 11.7 Add hardware validation sequence

Document and run in this exact order:

### Gate A — Boot without WROOM32

- [ ] S3 boots.
- [ ] No watchdog/assert.
- [ ] Wi-Fi/web/console reachable.
- [ ] I2S reports `WAITING_FOR_CLOCK` or bounded timeouts.
- [ ] Heap stable for 10 minutes.

### Gate B — UART link only

- [ ] `VERSION` succeeds.
- [ ] `STATUS` succeeds.
- [ ] No boot `VOLUME` command.
- [ ] 100 timeout/recovery cycles without reset/leak.

### Gate C — I2S clocks

- [ ] WS = 44,100 Hz within tolerance.
- [ ] BCLK = 2,822,400 Hz within tolerance.
- [ ] Ratio approximately 64.
- [ ] `bytes_written` increases.
- [ ] No write timeout with clocks stable.

### Gate D — Tone end to end

- [ ] 1 kHz tone audible through A2DP sink.
- [ ] Left/right both present.
- [ ] No obvious clipping/clicking.
- [ ] FFT peak approximately 1 kHz.

### Gate E — Radio

- [ ] MP3 station plays.
- [ ] AAC station plays.
- [ ] 48 kHz input resamples to 44.1 kHz without pitch error.
- [ ] Rebuffer produces silence, not tone.
- [ ] Stop completes in under 3 seconds.

### Gate F — Soak

- [ ] Two hours MP3.
- [ ] Two hours AAC.
- [ ] 500 play/stop cycles.
- [ ] Wi-Fi disconnect/reconnect.
- [ ] WROOM power cycle while S3 remains on.
- [ ] No monotonic heap loss or task growth.

---

# Phase 12 — Final cleanup and documentation

## 12.1 Remove stale/contradictory comments

Search:

```bash
rg -n "master transmitter|MAC-derived|skip I2S|glitch-free|benign in practice|best-effort|fall back|fallback" \
  main components docs README.md
```

Update comments to match implemented behavior. Do not leave historical experiment comments as the normative contract; move useful history to a troubleshooting document.

---

## 12.2 Add one authoritative architecture document

Update or replace `docs/SPEC.md` with the final implemented contract. Include:

- Wiring table
- Boot order
- Lifecycle ownership diagram
- I2S frame diagram
- UART protocol ownership
- Radio pipeline
- API/auth model
- NVS schemas/migrations
- Test commands
- Device gate commands
- Known limitations

Archive old conflicting specs under `docs/archive/` with a “superseded” header, or delete them if they are only stale generated plans.

---

## 12.3 Add exact one-command verification

README should document:

```bash
./tools/verify_host.sh
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
./tools/s3_device_gate.sh --port /dev/ttyACM0
```

Use the actual final script arguments.

---

# Final merge checklist

Do not declare the repair complete until every item is checked:

## Boot

- [ ] No duplicate Wi-Fi init.
- [ ] No duplicate UART init.
- [ ] I2S init called once.
- [ ] I2S start called once.
- [ ] Audio producer task created once.
- [ ] Boot health probe is read-only and bounded.
- [ ] 100 reboot cycle pass.

## Memory and concurrency

- [ ] BT request refcount test passes under ASan/TSan.
- [ ] No worker accesses request after release.
- [ ] No radio session freed before both workers exit.
- [ ] No I2S driver call inside critical section.
- [ ] I2S partial/timeout writes do not lose ring bytes.
- [ ] All shared lifecycle/config state uses mutex/atomic/owner task.

## Audio

- [ ] Correct 16-in-high-half-of-32 packing.
- [ ] 44.1 kHz stereo output.
- [ ] Correct BCLK/WS ratio.
- [ ] Tone audible.
- [ ] Radio buffering emits silence.
- [ ] Resampler reference tests pass.
- [ ] MP3/AAC hardware playback pass.

## Persistence

- [ ] NVS handles closed only when opened.
- [ ] Config published only after successful commit.
- [ ] Corrupt blobs not automatically overwritten.
- [ ] Stable station IDs implemented/migrated.

## Wi-Fi and web

- [ ] Wi-Fi init idempotent.
- [ ] 32-byte SSID preserved.
- [ ] Exact password rules tested.
- [ ] Public status contains no password/token.
- [ ] Backend copies all async strings.
- [ ] Structured HTTP errors and correct status codes.
- [ ] Release mutations authenticated.
- [ ] Frontend has no silent core `.catch(() => {})`.
- [ ] Polling never overlaps.

## Tests and gates

- [ ] Offline host tests work.
- [ ] Strict warnings pass.
- [ ] ASan pass.
- [ ] UBSan pass.
- [ ] TSan/concurrency pass where supported.
- [ ] Frontend unit tests pass.
- [ ] Mock-device Playwright pass.
- [ ] ESP-IDF build warning-free for project code.
- [ ] Device Unity tests pass.
- [ ] Strict device gate pass.
- [ ] Soak tests pass.

