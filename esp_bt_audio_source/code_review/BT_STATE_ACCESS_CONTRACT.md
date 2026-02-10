# BT State Access Contract & Threading Model

**Created**: 2026-02-09  
**Task**: CODE_REVIEW8 P2 - Define BT State Access Contract  
**Author**: System analysis + Kent Beck TDD principles  
**Status**: 🚧 **DRAFT** - Analysis complete, implementation pending

---

## Executive Summary

### Problem Statement

The `bt_ctx` structure (defined in `bt_manager_internal.h`) is accessed from **multiple FreeRTOS task contexts** without synchronization:

1. **BtAppTask** (priority 10): Bluetooth event handlers update state
2. **cmd_proc** (priority 2): Command handlers read state for STATUS/diagnostics
3. **Potential ESP-IDF callbacks**: GAP/A2DP/AVRC events from Bluedroid stack

**Risk**: Race conditions leading to:
- Torn reads (e.g., reading `bt_ctx.connected` while it's being updated)
- Inconsistent state (e.g., `connected=true` but `connected_mac=""`)
- Diagnostic data corruption (STATUS command shows stale/mixed values)

### Current State (No Explicit Contract)

```c
// bt_manager_internal.h
typedef struct {
    bool initialized;
    char device_name[32];
    bool scanning;
    bool connected;
    bool audio_playing;
    int volume;
    bt_device_list_t discovered_devices;
    bt_device_list_t paired_devices;
    bt_connected_cb connected_callback;
    bt_disconnected_cb disconnected_callback;
    char connected_mac[18];
    char connected_name[32];
} bt_manager_context_t;

extern bt_manager_context_t bt_ctx;  // ⚠️ No synchronization!
```

**Access Pattern Analysis:**
- ✅ **Writes**: Always from BtAppTask (via `bt_app_work_dispatch()` event queue)
- ❌ **Reads**: From cmd_proc task without synchronization
  - `cmd_handle_status()` → reads `bt_ctx.connected`, `bt_ctx.audio_playing`, `bt_ctx.connected_mac`
  - `bt_get_status()` → reads multiple fields
  - `bt_get_streaming_info()` → reads state for diagnostics

### Recommended Solution: **Option A - QueueBased State Access (Serialized via BtAppTask)**

**Contract**: All BT state access **must** go through BtAppTask event queue.

**Rationale:**
1. ✅ **Already 90% there**: All writes use `bt_app_work_dispatch()` queue
2. ✅ **Zero overhead**: No mutex contention, no priority inversion
3. ✅ **Thread-safe by design**: Serial execution eliminates races
4. ✅ **Testable**: Queue can be drained in host tests for deterministic validation
5. ✅ **ESP-IDF pattern**: Matches Bluedroid's own BTC task design

**Implementation Effort**: ~2-3 hours (modify command handlers, add request/response queue)

---

## Current Threading Model (Detailed Analysis)

### Task Inventory

#### 1. **BtAppTask** (components/bt_manager/bt_app_core.c)
```c
// Created in bt_app_task_start_up()
xTaskCreate(bt_app_task_handler, "BtAppTask", 8192, NULL, 10, &s_bt_app_task_handle);

// Queue: s_bt_app_queue (20 slots of bt_app_evt_msg_t)
static QueueHandle_t s_bt_app_queue = NULL;
```

**Purpose**: Execute Bluetooth event callbacks in serialized order

**Responsibilities**:
- Process GAP events (device discovery, authentication requests)
- Process A2DP events (connection state, audio state)
- Process AVRC events (remote control commands)
- **Update bt_ctx state** based on events

**Priority**: 10 (high - BT stack requires responsive event handling)

**Stack**: 8192 bytes (increased from 4096 to accommodate deep call stacks)

**Work Dispatch Mechanism**:
```c
bool bt_app_work_dispatch(bt_app_cb_t p_cback, uint16_t event, 
                          void *p_params, int param_len, 
                          bt_app_copy_cb_t p_copy_cback)
{
    // Posts callback to queue
    // BtAppTask dequeues and executes: p_cback(event, p_params);
}
```

**State Updates (all via BtAppTask)**:
- `bt_connection_state_cb()` → sets `bt_ctx.connected`, `bt_ctx.connected_mac`
- `bt_audio_state_cb()` → sets `bt_ctx.audio_playing`
- `bt_scan_handle_disc_res()` → appends to `bt_ctx.discovered_devices`
- `bt_pairing_*()` functions → update `bt_ctx.paired_devices`

#### 2. **cmd_proc task** (main/main.c)
```c
// Created in app_main()
xTaskCreate(cmd_process_task, "cmd_proc", 4096, NULL, 
            tskIDLE_PRIORITY + 1, NULL);

static void cmd_process_task(void* arg) {
    for (;;) {
        cmd_process();  // Polls UART for commands
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
```

**Purpose**: Process UART commands from external control interface

**Responsibilities**:
- Poll UART for incoming commands (every 20ms)
- Parse command strings (SCAN, STATUS, CONNECT, etc.)
- Execute command handlers (`cmd_handle_*`)
- **Read bt_ctx state** for STATUS/diagnostics (❌ **no synchronization**)

**Priority**: tskIDLE_PRIORITY + 1 = 2 (low - not time-critical)

**Stack**: 4096 bytes

**State Reads (from cmd_proc context - UNSAFE)**:
```c
// cmd_handlers_bt.c
cmd_status_t cmd_handle_status(const cmd_context_t *ctx)
{
    // ❌ Direct read without synchronization!
    if (bt_ctx.connected) {
        // bt_ctx.connected_mac could be mid-update
        snprintf(buf, sizeof(buf), "CONNECTED=%s", bt_ctx.connected_mac);
    }
}
```

#### 3. **ESP-IDF Bluedroid Stack Tasks**

**BTC Task** (Bluedroid Task Controller):
- Priority: BT_TASK_MAX_PRIORITIES - 6
- Purpose: Dispatch Bluedroid profile events to application callbacks
- Work queue: 2 queues with priority-based dispatch

**BTU Task** (Bluetooth Upper Layer):
- Priority: BT_TASK_MAX_PRIORITIES - 5
- Purpose: Core Bluetooth protocol stack processing
- Work queue: Single queue for stack events

**Callback Flow**:
```
BTU Task → BTC Task → bt_manager callbacks → bt_app_work_dispatch() → BtAppTask
```

**Key Insight**: ESP-IDF callbacks execute in **BTC task context**, NOT BtAppTask. They post work to BtAppTask via `bt_app_work_dispatch()` which provides the serialization boundary.

### Race Condition Examples

#### Race 1: STATUS Command During Connection Event

**Timeline**:
```
T0: cmd_proc reads bt_ctx.connected = false
T1: BtAppTask processes ESP_A2D_CONNECTION_STATE_CONNECTED
T2: BtAppTask sets bt_ctx.connected = true
T3: BtAppTask copies MAC to bt_ctx.connected_mac = "AA:BB:CC:DD:EE:FF"
T4: cmd_proc reads bt_ctx.connected_mac (could be partial string!)
T5: cmd_proc emits "STATUS|CONNECTED=false|MAC=AA:BB"  ← CORRUPT!
```

**Impact**: External control systems see inconsistent state, may retry commands unnecessarily.

#### Race 2: Discovery Complete During SCAN STATUS Query

**Timeline**:
```
T0: cmd_proc reads bt_ctx.discovered_devices.count = 2
T1: BtAppTask processes GAP_DISC_RES_EVT (device #3 found)
T2: BtAppTask modifies bt_ctx.discovered_devices (count=3, appends entry)
T3: cmd_proc loops: for (i = 0; i < 2; i++) { ... }  ← Misses device #3
T4: cmd_proc emits "DEVICES=2" but bt_ctx.discovered_devices.count=3
```

**Impact**: Test harness sees incomplete scan results, may timeout waiting for expected devices.

#### Race 3: Torn Read of Multi-Field State

**Timeline**:
```
T0: cmd_proc reads bt_ctx.connected = true
T1: cmd_proc reads bt_ctx.connected_mac = "AA:BB:CC:DD:EE:FF"
T2: BtAppTask processes ESP_A2D_CONNECTION_STATE_DISCONNECTED
T3: BtAppTask sets bt_ctx.connected = false
T4: BtAppTask clears bt_ctx.connected_mac = ""
T5: cmd_proc reads bt_ctx.audio_playing = false  ← OK
T6: cmd_proc emits "CONNECTED=true|MAC=AA:BB:CC:DD:EE:FF|AUDIO=false"
    But device is ACTUALLY disconnected now!
```

**Impact**: External systems may send STOP command to disconnected device, causing errors.

---

## Solution Options

### Option A: Queue-Based State Access (Serialized via BtAppTask) ✅ **RECOMMENDED**

**Contract**: All BT state access routed through BtAppTask event queue.

#### Implementation Design

**Step 1: Add "Get Status" Request/Response Messages**

```c
// bt_manager_internal.h
typedef enum {
    BT_MGR_MSG_GET_STATUS,
    BT_MGR_MSG_GET_STREAMING_INFO,
    BT_MGR_MSG_GET_DISCOVERED_DEVICES,
} bt_mgr_request_type_t;

typedef struct {
    bt_mgr_request_type_t type;
    void *response_buf;  // Caller-provided buffer for response
    size_t response_size;
    SemaphoreHandle_t done_sem;  // Semaphore to signal completion
} bt_mgr_request_t;
```

**Step 2: Add Request Handler in BtAppTask**

```c
// bt_manager.c
static void bt_mgr_handle_get_status(bt_mgr_request_t *req)
{
    bt_status_t *status = (bt_status_t *)req->response_buf;
    
    // Safe to read bt_ctx - we're in BtAppTask context!
    status->connected = bt_ctx.connected;
    status->audio_playing = bt_ctx.audio_playing;
    safe_copy_str(status->connected_mac, sizeof(status->connected_mac), 
                  bt_ctx.connected_mac);
    
    // Signal completion
    xSemaphoreGive(req->done_sem);
}

// Dispatcher integration (add to bt_app_work_dispatch signal types)
case BT_APP_SIG_GET_STATUS_REQUEST:
    bt_mgr_handle_get_status((bt_mgr_request_t *)evt_msg.param);
    break;
```

**Step 3: Modify Command Handlers to Use Request/Response**

```c
// cmd_handlers_bt.c
cmd_status_t cmd_handle_status(const cmd_context_t *ctx)
{
    bt_status_t status;
    SemaphoreHandle_t done_sem = xSemaphoreCreateBinary();
    
    bt_mgr_request_t req = {
        .type = BT_MGR_MSG_GET_STATUS,
        .response_buf = &status,
        .response_size = sizeof(status),
        .done_sem = done_sem
    };
    
    // Post request to BtAppTask
    bt_app_work_dispatch(bt_mgr_request_handler, 
                         BT_MGR_MSG_GET_STATUS, 
                         &req, sizeof(req), NULL);
    
    // Wait for response (with timeout to prevent deadlock)
    if (xSemaphoreTake(done_sem, pdMS_TO_TICKS(100)) != pdTRUE) {
        vSemaphoreDelete(done_sem);
        return CMD_ERROR; // Timeout - BtAppTask not responding
    }
    
    vSemaphoreDelete(done_sem);
    
    // Now status is filled with consistent snapshot from BtAppTask
    if (status.connected) {
        cmd_send_response("OK", "STATUS", "CONNECTED", status.connected_mac);
    } else {
        cmd_send_response("OK", "STATUS", "DISCONNECTED", NULL);
    }
    
    return CMD_SUCCESS;
}
```

#### Advantages

1. ✅ **Thread-safe by design**: Serial execution eliminates all race conditions
2. ✅ **Zero mutex overhead**: No contention, no priority inversion
3. ✅ **Already 90% implemented**: Reuses existing `bt_app_work_dispatch()` infrastructure
4. ✅ **Blocking reads OK**: cmd_proc priority=2 (low), timeout prevents deadlock
5. ✅ **Testable**: Host tests can drain queue for deterministic validation
6. ✅ **ESP-IDF pattern**: Matches Bluedroid's BTC task design

#### Disadvantages

1. ⚠️ **Slight latency**: 1 context switch + queue delay (~1-5ms typical)
2. ⚠️ **Requires semaphore**: Adds 1 semaphore per concurrent request (limited by heap)
3. ⚠️ **Complexity**: More code than direct locking (but safer at runtime)

#### Implementation Effort

- **Time**: 2-3 hours
- **Files modified**: 5-7 (bt_manager.c, CMD_handlers, bt_manager_internal.h, tests)
- **Tests**: Add host tests for concurrent access validation

---

### Option B: Mutex-Protected State Reads ⚠️ **NOT RECOMMENDED**

**Contract**: Protect `bt_ctx` with FreeRTOS mutex.

#### Implementation Design (Conceptual)

```c
// bt_manager_internal.h
extern SemaphoreHandle_t bt_ctx_mutex;

#define BT_CTX_LOCK()    xSemaphoreTake(bt_ctx_mutex, portMAX_DELAY)
#define BT_CTX_UNLOCK()  xSemaphoreGive(bt_ctx_mutex)

// Usage in cmd_handle_status()
BT_CTX_LOCK();
bool connected = bt_ctx.connected;
char mac_copy[18];
safe_copy_str(mac_copy, sizeof(mac_copy), bt_ctx.connected_mac);
BT_CTX_UNLOCK();
```

#### Advantages

1. ✅ **Familiar pattern**: Most developers understand mutexes
2. ✅ **Low latency**: No queue delay, just mutex acquire/release (~microseconds)

#### Disadvantages

1. ❌ **Priority inversion risk**: cmd_proc (prio 2) could block BtAppTask (prio 10)
2. ❌ **Widespread locking**: Must wrap ALL bt_ctx accesses (100+ locations)
3. ❌ **Error-prone**: Easy to forget locks, causes intermittent races
4. ❌ **Deadlock risk**: Nested locks or callback chains could deadlock
5. ❌ **Testing complexity**: Race conditions hard to reproduce in host tests

#### Implementation Effort

- **Time**: 4-6 hours (must audit and wrap ALL bt_ctx accesses)
- **Files modified**: 10+ (all bt_manager modules)
- **Risk**: High - easy to miss access points, introduces new failure modes

---

## Chosen Solution: **Option A - Queue-Based Access**

**Rationale**:
1. **ESP32 Best Practice**: Matches ESP-IDF's own design (BTC task, event queues)
2. **Safer**: Eliminates race conditions by design, not by discipline
3. **Testable**: Queue drain in host tests gives deterministic validation
4. **Incremental**: Can implement one command at a time, test, repeat

**Decision Log**:
- **Date**: 2026-02-09
- **Alternatives Considered**: Mutex locking (Option B)
- **Rejected Because**: Priority inversion risk, widespread changes, error-prone
- **Acceptance Criteria**: All command handlers use queue, zero direct bt_ctx reads from cmd_proc

---

## Implementation Plan

### Phase 1: Add Request/Response Infrastructure (1 hour)

**Tasks**:
1. Add `bt_mgr_request_type_t` enum to `bt_manager_internal.h`
2. Add `bt_mgr_request_t` struct (type, response_buf, done_sem)
3. Add `BT_APP_SIG_MGR_REQUEST` signal to `bt_app_core.h`
4. Implement `bt_mgr_request_handler()` dispatcher in `bt_manager.c`
5. Add `bt_mgr_handle_get_status()` handler (first implementation)

**Validation**:
- Compiles without warnings
- Host test: post request, verify semaphore signals

### Phase 2: Convert STATUS Command Handler (30 min)

**Tasks**:
1. Modify `cmd_handle_status()` to use request/response
2. Add timeout handling (100ms)
3. Add error path for timeout/failure

**Validation**:
- Device test: STATUS command returns correct data
- Host test: Concurrent STATUS calls don't race

### Phase 3: Convert Remaining Command Handlers (1 hour)

**Tasks**:
1. `bt_get_streaming_info()` → queue-based
2. `cmd_handle_audio_status()` → queue-based
3. Any other direct bt_ctx readers in command_interface

**Validation**:
- All command handlers compile
- Full device test suite passes

### Phase 4: Documentation & Assertions (30 min)

**Tasks**:
1. Document threading model in ARCH.md (new section)
2. Add assert in direct bt_ctx access locations: `assert(is_bt_app_task())`
3. Update README_TESTS.md with concurrency test guidance

**Validation**:
- Assertions catch violations in unit tests
- CI tests pass with new contract

---

## Testing Strategy

### Unit Tests (Host)

```c
// test/test_host/bt_manager/test_bt_state_access.c

TEST_CASE("Concurrent STATUS reads return consistent state")
{
    // Setup: Initialize BT manager, connect to device
    bt_manager_init();
    bt_connect("AA:BB:CC:DD:EE:FF");
    bt_app_core_drain(100); // Drain event queue
    
    // Action: Simulate concurrent STATUS commands
    bt_status_t status1, status2;
    SemaphoreHandle_t sem1 = xSemaphoreCreateBinary();
    SemaphoreHandle_t sem2 = xSemaphoreCreateBinary();
    
    bt_mgr_request_t req1 = {.type=BT_MGR_MSG_GET_STATUS, .response_buf=&status1, .done_sem=sem1};
    bt_mgr_request_t req2 = {. type=BT_MGR_MSG_GET_STATUS, .response_buf=&status2, .done_sem=sem2};
    
    // Post both requests
    bt_app_work_dispatch(bt_mgr_request_handler, BT_MGR_MSG_GET_STATUS, &req1, sizeof(req1), NULL);
    bt_app_work_dispatch(bt_mgr_request_handler, BT_MGR_MSG_GET_STATUS, &req2, sizeof(req2), NULL);
    
    // Process queue (serial execution)
    bt_app_core_drain(10);
    
    // Verify: Both responses identical (no torn reads)
    TEST_ASSERT_EQUAL(status1.connected, status2.connected);
    TEST_ASSERT_EQUAL_STRING(status1.connected_mac, status2.connected_mac);
}
```

### Device Tests (Integration)

```c
// test/test_app/main/bt_manager_concurrency_test.c

TEST_CASE("STATUS command during connection event")
{
    // Setup: Start connection in background
    bt_connect("AA:BB:CC:DD:EE:FF");
    
    // Action: Spam STATUS commands during connection
    for (int i = 0; i < 10; i++) {
        cmd_context_t ctx = {.type = CMD_TYPE_STATUS};
        cmd_execute(&ctx);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    // Verify: No torn reads (connected=true XOR connected_mac="")
    // (exact assertion depends on connection timing)
}
```

---

## Migration Checklist

- [ ] **Phase 1**: Add request/response infrastructure
  - [ ] Define `bt_mgr_request_t` struct
  - [ ] Add `BT_APP_SIG_MGR_REQUEST` signal
  - [ ] Implement `bt_mgr_request_handler()` dispatcher
  - [ ] Add `bt_mgr_handle_get_status()` first handler
  - [ ] Host test: Request/response round-trip

- [ ] **Phase 2**: Convert STATUS command
  - [ ] Modify `cmd_handle_status()` to queue-based
  - [ ] Add timeout handling
  - [ ] Device test: STATUS command correctness

- [ ] **Phase 3**: Convert remaining commands
  - [ ] `bt_get_streaming_info()` → queue
  - [ ] `cmd_handle_audio_status()` → queue
  - [ ] Audit: grep for `bt_ctx\.` in command_interface/
  - [ ] Full test suite validation

- [ ] **Phase 4**: Documentation & enforcement
  - [ ] Add "BT State Threading Model" section to ARCH.md
  - [ ] Add assertions: `assert(is_bt_app_task())` on direct bt_ctx writes
  - [ ] Update README_TESTS.md
  - [ ] Code review: Verify no missed access points

- [ ] **Commit & Push**
  - [ ] Commit type: `refactor(bt): Add BT state access contract (queue-based)`
  - [ ] BREAKING: Direct bt_ctx reads from non-BT-task now forbidden
  - [ ] Message: Reference CODE_REVIEW8 Task P2

---

## Future Enhancements

### Potential Optimizations

1. **Read-only snapshot caching** (if latency becomes issue):
   - BtAppTask publishes immutable snapshot every 100ms
   - cmd_proc reads snapshot (lock-free)
   - Trade-off: Staleness up to 100ms for zero wait

2. **Batched requests**:
   - Single request fetches all status fields
   - Reduces queue overhead for complex queries

3. **Async notifications** (event-driven STATUS):
   - BtAppTask pushes state changes to subscribers
   - cmd_proc maintains cached copy
   - Zero latency for reads, complexity for invalidation

### Rejected Alternatives

**RCU (Read-Copy-Update)**: Too complex for embedded, no kernel support  
**Lock-free algorithms**: Hard to verify, high cognitive load  
**Thread-local copies**: Staleness issues, synchronization complexity  

---

## References

- **CODE_REVIEW8.md**: Task P2 - Define BT State Access Contract
- **ESP-IDF BTC Task**: examples/bluetooth/bluedroid/classic_bt/  
- **FreeRTOS Queues**: https://www.freertos.org/a00018.html  
- **Priority Inversion**: https://www.freertos.org/FAQMutx.html  

**Timestamp**: 2026-02-09 22:15 UTC  
**Status**: 📋 Analysis complete, ready for implementation
