# ESP I2S Source Runtime Safety, Security, and State Integrity — FIX3 TODO

**Based on:**

- `docs/ESP_I2S_SOURCE_RUNTIME_SAFETY_INTEGRITY_FIX3_SPEC_2026-07-21.md`
- `docs/review-source/ESP_I2S_SOURCE_RUNTIME_SAFETY_INTEGRITY_FIX3_CODE_REVIEW_2026-07-21.md`

**Audience:** Claude Code, Qwen3.6, and human reviewers  
**Scope:** `esp_i2s_source/` only unless a task explicitly says otherwise  
**Execution rule:** Complete phases in order. Do not combine unrelated phases into one large patch.

# 0. Instructions for the coding model

Apply these rules to every phase:

1. Read the complete target file and relevant header before editing.
2. Do not silently ignore an `esp_err_t`, task-creation result, queue result, semaphore result, or NVS result.
3. Do not add a fallback unless the FIX3 spec explicitly permits it.
4. Never free a resource that a task may still access.
5. A timeout means “ownership unresolved,” not “safe to force-free.”
6. Do not report RUNNING, IDLE, RESTORED, READY, or DONE without observed evidence.
7. Build candidates locally, persist them, then publish them.
8. Reject oversized input. Never truncate credentials, commands, URLs, station names, JSON strings, or tokens.
9. Keep secrets out of normal logs and JSON responses.
10. Add regression tests in the same phase as the runtime fix.
11. Run strict host tests after every task group.
12. Run an ESP-IDF build after every phase touching `#ifdef ESP_PLATFORM` code.
13. Do not weaken a test to make broken behavior pass.
14. Do not claim hardware success without captured device markers/logs.
15. If an ESP-IDF signature differs from a snippet, verify the installed v5.5.1 header and adapt only the signature, not the required behavior.
16. Do not reference an assistant-created file unless it is included in this handoff or committed at the exact path named.

Recommended commit sequence:

```text
test(fix3): restore clean reproducible verification
fix(web): enforce bearer auth and initialize BT web state
fix(i2s): make lifecycle acknowledgement and reclamation safe
fix(bt-link): join workers and cancel requests safely
fix(stations): repair CRC validation and non-destructive recovery
fix(url): enforce stream destination policy
fix(wifi): correct NVS lengths and propagate driver failures
fix(radio): make session lifecycle and PSRAM allocation safe
fix(radio): bound reconnect playlist and decoder recovery
fix(ctrl): synchronize config and report truthful outcomes
fix(boot): enforce degraded capability boundaries
fix(ui): add authenticated mutation flow
verify(fix3): pass host device and endurance gates
```

---

# Phase 1 — Restore a clean verification baseline and configuration surface

## 1.1 Regenerate the frontend lockfile

**Files:**

- `web/package.json`
- `web/package-lock.json`

**Required work:**

1. Record supported versions in the commit message or README output:

```bash
node --version
npm --version
```

2. From `web/`, regenerate the lockfile with the current `package.json`:

```bash
rm -rf node_modules
npm install
npm ci
npm run build
npm test
```

3. Commit the resulting `package-lock.json`.
4. Confirm no generated `node_modules/` content is committed.

**Acceptance:**

- [ ] `npm ci` succeeds from a clean checkout.
- [ ] `npm run build` succeeds.
- [ ] `npm test` succeeds.
- [ ] The embedded gzip/checksum is regenerated only by the successful build path.

---

## 1.2 Add project Kconfig symbols

**Add:** `Kconfig.projbuild`

Use this baseline:

```kconfig
menu "ESP I2S Source"

config ESP_I2S_SOURCE_ALLOW_LOCAL_STREAMS
    bool "Allow radio streams to local/private network addresses"
    default n
    help
        Development-only override. When disabled, radio URLs, DNS results,
        redirects, and reconnects reject loopback, link-local, private,
        multicast, unspecified, and broadcast destinations.

config ESP_I2S_SOURCE_STA_HEX_PSK
    bool "Allow 64-character hexadecimal STA PSKs"
    default n
    help
        Permit an exact 64-hex-character raw WPA PSK. Non-hex 64-character
        values remain invalid and are never truncated.

endmenu
```

**Files to check:**

- `sdkconfig.defaults`
- `components/radio/include/station_store.h`
- `components/wifi_mgr/wifi_mgr.c`

Do not add preprocessor defaults that accidentally enable local streams.

**Acceptance:**

- [ ] `idf.py menuconfig` exposes both symbols.
- [ ] Default generated config has both disabled.
- [ ] Host tests compile both policy branches where practical.

---

## 1.3 Add a host-test target for each new pure helper

**File:** `test/host_test/CMakeLists.txt`

Plan targets now so later phases do not leave untested helpers:

- `test_web_auth`
- `test_i2s_lifecycle`
- `test_bt_link_shutdown`
- `test_stations_persistence`
- `test_url_policy`
- `test_wifi_validation`
- `test_radio_startup`
- `test_radio_recovery`
- `test_ctrl_runtime`
- `test_degraded_routes`

Targets may be introduced in their respective phases, but the final inventory must include them.

**Phase acceptance:**

```bash
./tools/run_host_tests.sh --strict
./tools/run_host_tests.sh --strict --asan
./tools/run_host_tests.sh --strict --ubsan
python3 -m pytest -q tools/test_s3_gate_assert.py
cd web && npm ci && npm run build && npm test
```

---

# Phase 2 — Enforce web authentication and initialize web submodules safely

## 2.1 Replace binary token generation with 64-character hex

**Files:**

- `components/web_ui/web_ui_auth.c`
- `components/web_ui/include/web_ui_internal.h`
- Add host-testable helper file if useful, for example:
  - `components/web_ui/web_ui_auth_core.c`
  - `components/web_ui/include/web_ui_auth_core.h`

Use fixed constants:

```c
#define AUTH_TOKEN_BYTES 32u
#define AUTH_TOKEN_HEX_LEN (AUTH_TOKEN_BYTES * 2u)
#define AUTH_TOKEN_BUF_LEN (AUTH_TOKEN_HEX_LEN + 1u)
```

Add a pure encoder:

```c
static void hex_encode_lower(const uint8_t *src, size_t src_len,
                             char *dst, size_t dst_size)
{
    static const char HEX[] = "0123456789abcdef";
    configASSERT(src != NULL);
    configASSERT(dst != NULL);
    configASSERT(dst_size >= (src_len * 2u) + 1u);

    for (size_t i = 0; i < src_len; ++i) {
        dst[i * 2u] = HEX[src[i] >> 4];
        dst[i * 2u + 1u] = HEX[src[i] & 0x0fu];
    }
    dst[src_len * 2u] = '\0';
}
```

Add exact validation:

```c
static bool token_is_valid(const char *token)
{
    if (!token) return false;
    size_t len = strnlen(token, AUTH_TOKEN_HEX_LEN + 1u);
    if (len != AUTH_TOKEN_HEX_LEN) return false;

    for (size_t i = 0; i < AUTH_TOKEN_HEX_LEN; ++i) {
        char c = token[i];
        bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        if (!ok) return false;
    }
    return true;
}
```

Do not accept uppercase unless the spec is intentionally revised. Canonical lowercase keeps comparison and UI behavior simple.

---

## 2.2 Implement exact constant-time comparison

Replace `ct_strcmp()` with an exact fixed-length helper:

```c
static bool token_equal_exact(const char *candidate, const char *expected)
{
    if (!candidate || !expected) return false;
    if (!token_is_valid(candidate) || !token_is_valid(expected)) return false;

    unsigned diff = 0;
    for (size_t i = 0; i < AUTH_TOKEN_HEX_LEN; ++i) {
        diff |= (unsigned)((unsigned char)candidate[i] ^
                           (unsigned char)expected[i]);
    }
    return diff == 0u;
}
```

Header parsing must require an exact length:

```c
#define BEARER_PREFIX "Bearer "
#define BEARER_PREFIX_LEN 7u

size_t header_len = httpd_req_get_hdr_value_len(req, "Authorization");
if (header_len != BEARER_PREFIX_LEN + AUTH_TOKEN_HEX_LEN) {
    return false;
}
```

Then retrieve into a buffer sized for prefix, token, and NUL. Reject any retrieval error or prefix mismatch.

**Tests:**

- Correct token passes.
- One-character mismatch fails.
- Empty fails.
- 63/65-character tokens fail.
- Correct token plus suffix fails.
- Prefix whitespace changes fail.
- Uppercase token fails under canonical policy.

---

## 2.3 Make auth initialization persist-before-publish

Replace the current initialization flow with explicit result handling.

Recommended structure:

```c
static esp_err_t load_token(char out[AUTH_TOKEN_BUF_LEN])
{
    nvs_handle_t h = 0;
    esp_err_t err = nvs_open("web_auth", NVS_READONLY, &h);
    if (err != ESP_OK) return err;

    size_t len = AUTH_TOKEN_BUF_LEN;
    err = nvs_get_str(h, AUTH_NVS_KEY, out, &len);
    nvs_close(h);

    if (err != ESP_OK) return err;
    if (len != AUTH_TOKEN_BUF_LEN || !token_is_valid(out)) {
        return ESP_ERR_INVALID_CRC; /* or a project-specific invalid-data code */
    }
    return ESP_OK;
}

static esp_err_t persist_token(const char token[AUTH_TOKEN_BUF_LEN])
{
    nvs_handle_t h = 0;
    esp_err_t err = nvs_open("web_auth", NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    err = nvs_set_str(h, AUTH_NVS_KEY, token);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}
```

Generation:

```c
esp_err_t web_ui_auth_generate_token(void)
{
    uint8_t random_bytes[AUTH_TOKEN_BYTES];
    char candidate[AUTH_TOKEN_BUF_LEN];

    esp_fill_random(random_bytes, sizeof(random_bytes));
    hex_encode_lower(random_bytes, sizeof(random_bytes), candidate, sizeof(candidate));

    esp_err_t err = persist_token(candidate);
    if (err != ESP_OK) {
        memset(random_bytes, 0, sizeof(random_bytes));
        memset(candidate, 0, sizeof(candidate));
        s_token_ready = false;
        return err;
    }

    memcpy(s_token, candidate, sizeof(s_token));
    s_token_ready = true;
    printf("AUTH|BOOTSTRAP_TOKEN|%s\n", s_token);
    fflush(stdout);

    memset(random_bytes, 0, sizeof(random_bytes));
    memset(candidate, 0, sizeof(candidate));
    return ESP_OK;
}
```

Initialization must distinguish first boot from corruption:

```c
esp_err_t web_ui_auth_init(void)
{
    char candidate[AUTH_TOKEN_BUF_LEN] = {0};
    esp_err_t err = load_token(candidate);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return web_ui_auth_generate_token();
    }
    if (err != ESP_OK) {
        s_token_ready = false;
        return err;
    }

    memcpy(s_token, candidate, sizeof(s_token));
    s_token_ready = true;
    return ESP_OK;
}
```

Be careful: `nvs_open(..., NVS_READONLY, ...)` may return a not-found code for a missing namespace. Treat only the exact documented not-found cases as first boot.

**Forbidden:** Return `ESP_OK` after token persistence failure.

---

## 2.4 Add centralized authenticated route dispatch

**File:** `components/web_ui/web_ui.c`

Do not duplicate an auth check manually in every handler. Use a static route context:

```c
typedef esp_err_t (*web_handler_fn_t)(httpd_req_t *req);

typedef struct {
    web_handler_fn_t handler;
    bool auth_required;
    const char *capability;
} web_route_ctx_t;

static esp_err_t route_dispatch(httpd_req_t *req)
{
    const web_route_ctx_t *ctx = (const web_route_ctx_t *)req->user_ctx;
    if (!ctx || !ctx->handler) return ESP_FAIL;

    if (ctx->auth_required && !web_ui_auth_check(req)) {
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Bearer");
        return web_send_error(req, "401 Unauthorized", "AUTH_REQUIRED",
                              "A valid bearer token is required", false);
    }

    return ctx->handler(req);
}
```

Create static-lifetime contexts, not stack locals:

```c
static const web_route_ctx_t S_WIFI_POST = {
    .handler = wifi_post,
    .auth_required = true,
    .capability = "wifi",
};
```

Register protected routes with `.handler = route_dispatch` and `.user_ctx = &S_WIFI_POST`.

All POST/PUT/DELETE routes listed in the spec must use the dispatcher. Add a host/static test that enumerates the route table and asserts every mutating route has `auth_required=true`.

---

## 2.5 Fail web startup when auth cannot initialize

In `web_ui_start()`:

```c
esp_err_t err = web_ui_auth_init();
if (err != ESP_OK) {
    ESP_LOGE(TAG, "auth init failed: %s", esp_err_to_name(err));
    printf("DIAG|AUTH|ERROR|stage=init,err=%s\n", esp_err_to_name(err));
    fflush(stdout);
    return err;
}
```

Do this before allocating web mutexes or starting HTTP.

---

## 2.6 Make the BT web submodule explicit and degradable

**Files:**

- `components/web_ui/web_ui_bt.c`
- `components/web_ui/include/web_ui_internal.h`
- `components/web_ui/web_ui.c`

Change API:

```c
esp_err_t web_ui_bt_init(void);
void web_ui_bt_deinit(void);
bool web_ui_bt_available(void);
```

Recommended behavior:

```c
static bool s_bt_available;
static int s_bt_subscription = -1;

esp_err_t web_ui_bt_init(void)
{
    if (s_bt_mtx) return ESP_OK;

    s_bt_mtx = xSemaphoreCreateMutex();
    if (!s_bt_mtx) return ESP_ERR_NO_MEM;

    if (!bt_link_is_initialized()) {
        s_bt_available = false;
        return ESP_OK; /* web remains usable; BT endpoints return 503 */
    }

    s_bt_subscription = bt_link_subscribe(on_bt_event, NULL);
    if (s_bt_subscription < 0) {
        vSemaphoreDelete(s_bt_mtx);
        s_bt_mtx = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_bt_available = true;
    bt_link_cmd_state_t state = BT_LINK_CMD_TIMEOUT;
    esp_err_t err = bt_link_send("PAIRED", &state, NULL, 0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "initial paired-list request failed: %s", esp_err_to_name(err));
    }
    return ESP_OK;
}
```

Do not treat failed initial list priming as successful list data. It may be a visible degraded warning while the submodule remains available.

Every BT handler begins with a common guard:

```c
static esp_err_t require_bt(httpd_req_t *req)
{
    if (s_bt_mtx && s_bt_available) return ESP_OK;
    return web_send_error(req, "503 Service Unavailable",
                          "BT_LINK_UNAVAILABLE",
                          "Bluetooth control link is unavailable", true);
}
```

Call it before taking the mutex or sending commands.

`web_ui_bt_deinit()` must:

1. Mark unavailable.
2. Stop/join helper tasks such as `s_conn_vol_task` using a stop flag and exit event; do not `vTaskDelete()` them while they access state.
3. Unsubscribe while `bt_link` is still initialized.
4. Delete the mutex.
5. Clear buffered state.

Call `web_ui_bt_init()` before route registration. Call `web_ui_bt_deinit()` from every web startup failure path and from `web_ui_stop()`.

---

## 2.7 Add local token rotation command

**Files:**

- `components/cmd_console/console.c`
- `components/web_ui/web_ui_auth.c`
- `components/web_ui/include/web_ui_internal.h` or a public auth header

Add a local command:

```text
AUTH ROTATE
```

It must call a rotation function that persists a candidate before publication. It must never be forwarded to the WROOM32.

Rotation output:

```text
AUTH|BOOTSTRAP_TOKEN|<new-token>
AUTH|TOKEN_ROTATED
```

Do not add an unauthenticated HTTP rotate route.

---

## 2.8 Tests

Add tests for:

- Hex encoder known input.
- Token validation and exact compare.
- NVS length exactly 65.
- Missing token creates one.
- Read error does not create a replacement.
- Malformed stored token fails closed.
- Commit failure leaves `s_token_ready=false` and prints no token.
- Every mutating route is protected.
- Unauthorized handler does not invoke inner handler.
- BT GET before BT init returns 503 and does not take a null semaphore.
- Web startup auth failure never calls `httpd_start()`.

**Phase acceptance:**

- [ ] Host strict/ASan/UBSan pass.
- [ ] Clean ESP-IDF build passes.
- [ ] `grep` confirms no direct mutating route bypasses dispatcher.
- [ ] Manual curl without token returns 401.
- [ ] Manual curl with token reaches handler.

---

# Phase 3 — Repair I2S lifecycle, state truthfulness, and diagnostics

## 3.1 Add JOIN_PENDING and channel-enabled tracking

**Files:**

- `components/i2s_out/include/i2s_out.h`
- `components/i2s_out/i2s_out.c`

Extend enum:

```c
typedef enum {
    I2S_STATE_UNINITIALIZED = 0,
    I2S_STATE_IDLE,
    I2S_STATE_STARTING,
    I2S_STATE_RUNNING,
    I2S_STATE_WAITING_FOR_CLOCK,
    I2S_STATE_STOPPING,
    I2S_STATE_FAULTED,
    I2S_STATE_FAULTED_JOIN_PENDING,
} i2s_out_state_t;
```

Add:

```c
static bool s_channel_enabled; /* protected by s_lifecycle_mtx */
```

Do not infer channel-enabled from public state.

---

## 3.2 Split task-entered, operational-ready, and exited events

Replace event bits:

```c
#define I2S_EVT_WRITER_ENTERED BIT(0)
#define I2S_EVT_WRITER_READY   BIT(1)
#define I2S_EVT_WRITER_EXITED  BIT(2)
```

In the writer:

```c
xEventGroupSetBits(s_events, I2S_EVT_WRITER_ENTERED);
bool first_result = true;

/* after each write result and state publication */
if (first_result) {
    xEventGroupSetBits(s_events, I2S_EVT_WRITER_READY);
    first_result = false;
}
```

For a timeout:

```c
i2s_set_state(I2S_STATE_WAITING_FOR_CLOCK, ESP_OK);
```

Do not record expected missing-clock timeout as terminal `last_error`; the timeout counter is sufficient.

For non-timeout error:

```c
i2s_set_state(I2S_STATE_FAULTED, err);
if (first_result) xEventGroupSetBits(s_events, I2S_EVT_WRITER_READY);
break;
```

At exit, set EXITED. Do not free resources. Prefer not to clear `s_writer_task` in the worker; the lifecycle owner clears it after observing EXITED.

---

## 3.3 Add a join helper used by start cancellation and stop

Suggested skeleton, called with lifecycle mutex held:

```c
static esp_err_t join_writer_locked(TickType_t timeout)
{
    if (!s_writer_task) return ESP_OK;

    EventBits_t bits = xEventGroupWaitBits(
        s_events, I2S_EVT_WRITER_EXITED,
        pdFALSE, pdTRUE, timeout);

    if ((bits & I2S_EVT_WRITER_EXITED) == 0) {
        atomic_store(&s_state, I2S_STATE_FAULTED_JOIN_PENDING);
        return ESP_ERR_TIMEOUT;
    }

    s_writer_task = NULL;
    return ESP_OK;
}
```

Do not use `pdTRUE` to clear EXITED before lifecycle code has finished all checks. Clear lifecycle bits only when preparing a new start.

---

## 3.4 Rewrite `i2s_out_start()`

Required sequence:

1. Take lifecycle mutex.
2. Validate state.
3. Clear all writer event bits.
4. Clear last terminal error for a new attempt.
5. Set STARTING.
6. Enable channel.
7. On success set `s_channel_enabled=true`.
8. Create writer.
9. Wait for READY or EXITED.
10. Read state set by writer.
11. Return OK for RUNNING/WAITING.
12. On timeout, cancel/join/disable.
13. Never unconditionally store RUNNING.

Reference flow:

```c
EventBits_t bits = xEventGroupWaitBits(
    s_events,
    I2S_EVT_WRITER_READY | I2S_EVT_WRITER_EXITED,
    pdFALSE, pdFALSE, pdMS_TO_TICKS(1000));

if ((bits & (I2S_EVT_WRITER_READY | I2S_EVT_WRITER_EXITED)) == 0) {
    atomic_store(&s_stop_requested, true);
    esp_err_t join_err = join_writer_locked(pdMS_TO_TICKS(I2S_STOP_TIMEOUT_MS));
    if (join_err != ESP_OK) {
        xSemaphoreGive(s_lifecycle_mtx);
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t disable_err = ESP_OK;
    if (s_channel_enabled) {
        disable_err = i2s_channel_disable(s_tx_chan);
        if (disable_err == ESP_OK) s_channel_enabled = false;
    }
    atomic_store(&s_state,
                 disable_err == ESP_OK ? I2S_STATE_IDLE : I2S_STATE_FAULTED);
    xSemaphoreGive(s_lifecycle_mtx);
    return ESP_ERR_TIMEOUT;
}

i2s_out_state_t observed = atomic_load(&s_state);
esp_err_t result = ESP_FAIL;
if (observed == I2S_STATE_RUNNING ||
    observed == I2S_STATE_WAITING_FOR_CLOCK) {
    result = ESP_OK;
} else if (observed == I2S_STATE_FAULTED) {
    result = (esp_err_t)s_stats.last_error;
}
```

Protect `s_stats.last_error` while reading or add a helper.

Task-create failure must disable channel and preserve disable failure:

```c
if (created != pdPASS) {
    esp_err_t disable_err = i2s_channel_disable(s_tx_chan);
    if (disable_err == ESP_OK) {
        s_channel_enabled = false;
        atomic_store(&s_state, I2S_STATE_IDLE);
        return ESP_ERR_NO_MEM;
    }
    i2s_set_state(I2S_STATE_FAULTED, disable_err);
    return disable_err;
}
```

---

## 3.5 Rewrite stop and deinit gates

Stop must accept STARTING and JOIN_PENDING recovery attempts.

Reference sequence:

```c
atomic_store(&s_state, I2S_STATE_STOPPING);
atomic_store(&s_stop_requested, true);

esp_err_t err = join_writer_locked(pdMS_TO_TICKS(I2S_STOP_TIMEOUT_MS));
if (err != ESP_OK) {
    /* state already JOIN_PENDING; resources retained */
    return err;
}

if (s_channel_enabled) {
    err = i2s_channel_disable(s_tx_chan);
    if (err != ESP_OK) {
        i2s_set_state(I2S_STATE_FAULTED, err);
        return err;
    }
    s_channel_enabled = false;
}

atomic_store(&s_state, I2S_STATE_IDLE);
return ESP_OK;
```

Deinit condition:

```c
if (state != I2S_STATE_IDLE || s_writer_task || s_channel_enabled) {
    return ESP_ERR_INVALID_STATE;
}
```

Check `i2s_del_channel()`:

```c
esp_err_t err = i2s_del_channel(s_tx_chan);
if (err != ESP_OK) return err;
s_tx_chan = NULL;
```

Only after successful channel deletion may event group and ring be destroyed.

---

## 3.6 Fix stats

Populate peak:

```c
out->ring_peak = s_ring ? pcm_ring_peak_used(s_ring) : 0;
```

Update `audio_out_task()` backoff condition to include JOIN_PENDING and STOPPING if writes cannot drain.

Add a diagnostic when state enters JOIN_PENDING.

---

## 3.7 Add lifecycle tests

Create an I2S driver mock or extract a pure lifecycle reducer. Tests must prove:

- Writer enters WAITING before start returns; start returns OK and state remains WAITING.
- Writer faults before start returns; start returns error and does not overwrite FAULTED.
- Start acknowledgement timeout joins and returns IDLE when cancellation succeeds.
- Start timeout becomes JOIN_PENDING when task does not exit.
- Stop timeout retains ring/event/channel and blocks deinit.
- Disable failure leaves FAULTED and `channel_enabled=true`.
- Repeated stop retries disable and can reach IDLE.
- Deinit rejects STARTING/RUNNING/WAITING/STOPPING/FAULTED/JOIN_PENDING.
- Peak ring statistic is populated.

**Phase acceptance:**

- [ ] Strict/ASan/UBSan pass.
- [ ] Clean IDF build passes.
- [ ] Hardware boot without WROOM clock reaches WAITING, not false RUNNING.
- [ ] No watchdog reset while clocks are absent.

---

# Phase 4 — Repair `bt_link` startup, shutdown, and request cancellation

## 4.1 Add lifecycle state and lifecycle mutex

**Files:**

- `components/bt_link/bt_link.c`
- `components/bt_link/include/bt_link.h`

Add internal state:

```c
typedef enum {
    BT_LINK_STATE_UNINITIALIZED = 0,
    BT_LINK_STATE_STARTING,
    BT_LINK_STATE_RUNNING,
    BT_LINK_STATE_STOPPING,
    BT_LINK_STATE_STOPPED,
    BT_LINK_STATE_FAULTED_JOIN_PENDING,
} bt_link_state_t;

static SemaphoreHandle_t s_lifecycle_mtx;
static _Atomic bt_link_state_t s_lifecycle_state;
```

`bt_link_is_initialized()` should mean resources exist; add `bt_link_is_running()` for send capability, or redefine/document clearly.

Add task-entered bits in addition to exit bits:

```c
#define BT_LINK_EVT_TASK_ENTERED       BIT(0)
#define BT_LINK_EVT_EVENT_TASK_ENTERED BIT(1)
#define BT_LINK_EVT_TASK_EXITED        BIT(2)
#define BT_LINK_EVT_EVENT_TASK_EXITED  BIT(3)
```

Workers set ENTERED at task entry and EXITED at task exit.

---

## 4.2 Add transport result and one completion helper

Extend request:

```c
typedef struct {
    atomic_uint refs;
    char cmd[BT_LINK_LINE_MAX];
    bt_link_cmd_state_t state;
    esp_err_t transport_err;
    char result[BT_LINK_FIELD_MAX];
    char data[BT_LINK_FIELD_MAX];
    SemaphoreHandle_t done_sem;
} bt_link_request_t;
```

Initialize `transport_err=ESP_OK` and state PENDING.

One worker-side completion helper:

```c
static void request_complete_worker(bt_link_request_t *req,
                                    esp_err_t transport_err,
                                    bt_link_cmd_state_t state,
                                    const char *result,
                                    const char *data)
{
    if (!req) return;
    req->transport_err = transport_err;
    req->state = state;
    if (result) strlcpy(req->result, result, sizeof(req->result));
    if (data) strlcpy(req->data, data, sizeof(req->data));
    xSemaphoreGive(req->done_sem);
    request_release(req); /* worker reference */
}
```

Call it exactly once for every enqueued request.

`bt_link_send()` returns `req->transport_err` when non-OK. Command terminal error remains represented by `out_state=BT_LINK_CMD_DONE_ERR` with transport `ESP_OK`.

---

## 4.3 Fail immediately on UART write errors

Replace log-only behavior:

```c
int written = uart_write_bytes(BT_LINK_UART, out, (size_t)n);
if (written != n) {
    bt_link_request_t *failed = s_active;
    s_active = NULL;
    request_complete_worker(failed, ESP_FAIL, BT_LINK_CMD_TIMEOUT,
                            "LOCAL_UART_WRITE_FAILED", "");
    continue;
}
```

If the IDF function can return a more specific negative code only as count, map it to a project transport error and log written/requested.

Also check the initial CRLF write during init. If it fails, initialization fails before tasks start.

---

## 4.4 Cancel active and queued requests on stop

At worker exit:

```c
if (s_active) {
    bt_link_request_t *active = s_active;
    s_active = NULL;
    request_complete_worker(active, ESP_ERR_INVALID_STATE,
                            BT_LINK_CMD_TIMEOUT,
                            "CANCELLED", "shutdown");
}

bt_link_request_t *pending = NULL;
while (xQueueReceive(s_cmd_queue, &pending, 0) == pdTRUE) {
    request_complete_worker(pending, ESP_ERR_INVALID_STATE,
                            BT_LINK_CMD_TIMEOUT,
                            "CANCELLED", "shutdown");
}
```

This differs from the current drain, which releases the worker reference without waking the caller.

Do not assign `s_active=NULL` in deinit as a substitute for completion.

---

## 4.5 Gate sends on RUNNING before and after the send lock

```c
if (atomic_load(&s_lifecycle_state) != BT_LINK_STATE_RUNNING) {
    return ESP_ERR_INVALID_STATE;
}

xSemaphoreTake(s_send_mutex, portMAX_DELAY);
if (atomic_load(&s_lifecycle_state) != BT_LINK_STATE_RUNNING) {
    xSemaphoreGive(s_send_mutex);
    return ESP_ERR_INVALID_STATE;
}
```

Do not expose the queue after STOPPING begins.

---

## 4.6 Make partial initialization join-safe

Create resources, then event task, then UART task. If a later task fails:

1. Set stop requested.
2. Wait only for tasks actually created.
3. Reclaim only after exit bits.
4. If wait times out, retain resources and state JOIN_PENDING.

Reference helper:

```c
static esp_err_t wait_created_tasks_locked(EventBits_t required, TickType_t timeout)
{
    EventBits_t bits = xEventGroupWaitBits(
        s_lifecycle_events, required, pdFALSE, pdTRUE, timeout);
    if ((bits & required) != required) {
        atomic_store(&s_lifecycle_state, BT_LINK_STATE_FAULTED_JOIN_PENDING);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}
```

Do not delete `s_event_queue`, `s_subs_mtx`, or event group while event task may run.

Lifecycle owner clears task handles after observed exit. Worker tasks should only set bits and delete themselves.

---

## 4.7 Make stop/deinit retryable

`bt_link_stop()`:

- Idempotent from STOPPED/UNINITIALIZED.
- Sets STOPPING.
- Requests stop.
- Waits for both exit bits.
- On success clears task handles and sets STOPPED.
- On timeout sets JOIN_PENDING and retains resources.
- A later stop call from JOIN_PENDING retries the wait.

`bt_link_deinit()`:

- Legal only from STOPPED with no task handles and no active request.
- Deletes UART and synchronization objects.
- Sets UNINITIALIZED.
- Checks `uart_driver_delete()` return.

---

## 4.8 Tests

Failure-injection tests must cover:

- Event task creates, UART task creation fails, event task exits before resources are deleted.
- Event task refuses to exit; init returns timeout/JOIN_PENDING and retains resources.
- Active request receives cancellation and caller wakes.
- Queued requests receive cancellation and callers wake.
- Send after stop returns invalid state immediately and allocates no request.
- Stop racing with send is closed by second state check.
- UART short write returns local transport failure immediately.
- Every enqueued request reaches exactly one completion and two matching reference releases.
- Repeated stop/deinit are idempotent in valid states.

**Phase acceptance:** strict/ASan/UBSan + clean IDF build.

---

# Phase 5 — Repair station persistence and enforce URL destination policy

## 5.1 Move CRC and blob validation into host-testable helpers — DONE (Phase 5A, commit pending)

**Files:**

- `components/radio/stations.c`
- Add `components/radio/stations_persist_core.c`
- Add `components/radio/include/stations_persist_core.h`
- `components/radio/CMakeLists.txt`
- `test/host_test/CMakeLists.txt`

Correct CRC:

```c
uint32_t stations_crc32_ieee(const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = UINT32_C(0xffffffff);

    for (size_t i = 0; i < len; ++i) {
        crc ^= p[i];
        for (unsigned bit = 0; bit < 8; ++bit) {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
            crc = (crc >> 1) ^ (UINT32_C(0xedb88320) & mask);
        }
    }
    return crc ^ UINT32_C(0xffffffff);
}
```

Tests:

```c
TEST_ASSERT_EQUAL_HEX32(0xCBF43926u,
    stations_crc32_ieee("123456789", 9));
```

Also prove changing every byte position changes CRC for representative blobs.

---

## 5.2 Centralize V2 blob construction — DONE (Phase 5A, commit pending)

Avoid repeating header setup in every mutation.

```c
static void build_blob_from_store(const station_store_t *store,
                                  stations_blob_v2_t *blob)
{
    memset(blob, 0, sizeof(*blob));
    blob->magic = STATIONS_V2_MAGIC;
    blob->version = STATIONS_V2_VERSION;
    blob->header_size = (uint16_t)offsetof(stations_blob_v2_t, store);
    blob->payload_size = (uint32_t)sizeof(blob->store);
    blob->next_id = store->next_id;
    blob->store = *store;
    blob->crc32 = stations_crc32_ieee(&blob->store, sizeof(blob->store));
}
```

Prefer removing redundant `next_id` in a future version, but FIX3 can require equality.

---

## 5.3 Implement full blob validation — DONE (Phase 5A, commit pending)

Add a result enum with precise reasons, for example:

```c
typedef enum {
    STATIONS_BLOB_OK = 0,
    STATIONS_BLOB_BAD_SIZE,
    STATIONS_BLOB_BAD_MAGIC,
    STATIONS_BLOB_BAD_VERSION,
    STATIONS_BLOB_BAD_HEADER_SIZE,
    STATIONS_BLOB_BAD_PAYLOAD_SIZE,
    STATIONS_BLOB_BAD_CRC,
    STATIONS_BLOB_BAD_COUNT,
    STATIONS_BLOB_BAD_STRING,
    STATIONS_BLOB_BAD_URL,
    STATIONS_BLOB_DUPLICATE_URL,
    STATIONS_BLOB_BAD_ID,
    STATIONS_BLOB_DUPLICATE_ID,
    STATIONS_BLOB_BAD_NEXT_ID,
} stations_blob_result_t;
```

Reference validator core:

```c
static bool field_terminated(const char *s, size_t cap)
{
    return memchr(s, '\0', cap) != NULL;
}

stations_blob_result_t stations_blob_validate(
    const stations_blob_v2_t *blob, size_t blob_size)
{
    if (!blob || blob_size != sizeof(*blob)) return STATIONS_BLOB_BAD_SIZE;
    if (blob->magic != STATIONS_V2_MAGIC) return STATIONS_BLOB_BAD_MAGIC;
    if (blob->version != STATIONS_V2_VERSION) return STATIONS_BLOB_BAD_VERSION;
    if (blob->header_size != offsetof(stations_blob_v2_t, store))
        return STATIONS_BLOB_BAD_HEADER_SIZE;
    if (blob->payload_size != sizeof(blob->store))
        return STATIONS_BLOB_BAD_PAYLOAD_SIZE;
    if (stations_crc32_ieee(&blob->store, sizeof(blob->store)) != blob->crc32)
        return STATIONS_BLOB_BAD_CRC;
    if (blob->store.count < 0 || blob->store.count > STATION_MAX)
        return STATIONS_BLOB_BAD_COUNT;
    if (blob->next_id != blob->store.next_id)
        return STATIONS_BLOB_BAD_NEXT_ID;

    uint32_t max_id = 0;
    for (int i = 0; i < blob->store.count; ++i) {
        const station_t *item = &blob->store.items[i];
        if (!field_terminated(item->name, sizeof(item->name)) ||
            !field_terminated(item->url, sizeof(item->url)))
            return STATIONS_BLOB_BAD_STRING;
        if (item->id == STATION_ID_NONE) return STATIONS_BLOB_BAD_ID;
        if (station_validate_url(item->url) != STATION_OK)
            return STATIONS_BLOB_BAD_URL;
        if (item->id > max_id) max_id = item->id;

        for (int j = 0; j < i; ++j) {
            if (blob->store.items[j].id == item->id)
                return STATIONS_BLOB_DUPLICATE_ID;
            if (strcmp(blob->store.items[j].url, item->url) == 0)
                return STATIONS_BLOB_DUPLICATE_URL;
        }
    }

    if (blob->store.next_id == STATION_ID_NONE ||
        blob->store.next_id <= max_id)
        return STATIONS_BLOB_BAD_NEXT_ID;

    return STATIONS_BLOB_OK;
}
```

If URL policy DNS checks are device-only, blob validation should at least perform pure syntax/literal checks. DNS is rechecked at playback.

---

## 5.4 Distinguish absent, corrupt, legacy, and NVS-error cases — DONE (Phase 5A, commit pending)

Do not use a single `bool loaded`.

Use a load result:

```c
typedef enum {
    STATIONS_LOAD_OK = 0,
    STATIONS_LOAD_NOT_FOUND,
    STATIONS_LOAD_CORRUPT,
    STATIONS_LOAD_UNSUPPORTED,
    STATIONS_LOAD_NVS_ERROR,
    STATIONS_LOAD_NO_MEM,
} stations_load_result_t;
```

Read blob size first:

```c
size_t size = 0;
esp_err_t err = nvs_get_blob(h, NVS_KEY, NULL, &size);
if (err == ESP_ERR_NVS_NOT_FOUND) return STATIONS_LOAD_NOT_FOUND;
if (err != ESP_OK) return STATIONS_LOAD_NVS_ERROR;
if (size != sizeof(stations_blob_v2_t)) return STATIONS_LOAD_CORRUPT;
```

Rules:

- Only `NOT_FOUND` permits checking legacy.
- Only both keys `NOT_FOUND` permits seeding defaults.
- Corrupt current data is never overwritten automatically.
- Initial seed persist/read-back failure means init fails.

Emit reason-specific diagnostics.

---

## 5.5 Fix legacy schema and migration — DONE (Phase 5A, commit pending)

Replace the incorrect `v1_blob_t` that embeds current `station_store_t`.

Use:

```c
typedef struct {
    char name[STATION_NAME_MAX];
    char url[STATION_URL_MAX];
} station_v1_t;

typedef struct {
    uint32_t magic;
    int32_t count;
    station_v1_t items[STATION_MAX];
} stations_blob_v1_t;
```

Migration algorithm:

1. Read exact legacy size.
2. Validate magic/count/terminators/URLs/duplicates.
3. Build a new `station_store_t` with `station_store_add()` in original order.
4. Assert assigned IDs are `i + 1`.
5. Set `next_id=count+1`.
6. Build V2 blob.
7. Persist to `stations_v2`.
8. Read V2 back and validate.
9. Publish candidate.
10. Leave legacy key unchanged.

Coordinate control migration. Fix `ctrl_cfg.c` conversion:

```c
if (v0_blob.last_station >= 0 &&
    v0_blob.last_station < migrated_store->count) {
    out->last_station_id =
        migrated_store->items[v0_blob.last_station].id;
} else {
    out->last_station_id = CTRL_LAST_STATION_NONE;
}
```

The current direct cast is wrong because index 0 becomes ID 0, which means none.

If cross-component atomic migration is too invasive, implement an explicit migration coordinator called after station migration and before `ctrl_init()` publishes config. Document the exact order and test power-loss points.

---

## 5.6 Centralize persist/commit/close/read-back — DONE (Phase 5A, commit pending)

One helper:

```c
static esp_err_t persist_blob_verified(const stations_blob_v2_t *blob)
{
    nvs_handle_t h = 0;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    err = nvs_set_blob(h, NVS_KEY, blob, sizeof(*blob));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) return err;

    return read_back_and_validate(blob);
}
```

`read_back_and_validate()` compares validated semantic content, not just CRC.

All add/update/remove/move paths call this helper. Delete repeated open/set/commit blocks and all unconditional `nvs_close(h)` calls.

---

## 5.7 Make station mutations candidate-based — DONE (Phase 5A, commit pending)

Pattern:

```c
xSemaphoreTake(s_mtx, portMAX_DELAY);
station_store_t candidate = s_store;
station_result_t result = station_store_update(&candidate, idx, name, url);
if (result != STATION_OK) { ... }

stations_blob_v2_t *blob = heap_caps_calloc(1, sizeof(*blob), MALLOC_CAP_8BIT);
if (!blob) { ... }
build_blob_from_store(&candidate, blob);

esp_err_t err = persist_blob_verified(blob);
if (err == ESP_OK) s_store = candidate;
```

A 12 KiB candidate on stack may be unsafe. Allocate large candidates from appropriate heap or use one heap blob. Do not silently fall back after allocation failure.

---

## 5.8 Implement pure IP address policy — DONE (Phase 5B, commit pending; redirect/reconnect-specific interception deferred to Phase 8, which owns the HTTP client internals)

**Add:**

- `components/radio/url_policy.c`
- `components/radio/include/url_policy.h`
- host tests

IPv4 helper:

```c
bool url_policy_ipv4_allowed(uint32_t addr_be)
{
#ifdef CONFIG_ESP_I2S_SOURCE_ALLOW_LOCAL_STREAMS
    (void)addr_be;
    return true;
#else
    uint32_t a = ntohl(addr_be);
    uint8_t o1 = (uint8_t)(a >> 24);
    uint8_t o2 = (uint8_t)(a >> 16);

    if (o1 == 0) return false;
    if (o1 == 10) return false;
    if (o1 == 127) return false;
    if (o1 == 169 && o2 == 254) return false;
    if (o1 == 172 && o2 >= 16 && o2 <= 31) return false;
    if (o1 == 192 && o2 == 168) return false;
    if (o1 >= 224) return false;
    return true;
#endif
}
```

IPv6 helper must reject the ranges in the spec and detect IPv4-mapped addresses. Use byte checks or standard macros available in lwIP; keep a host-compatible pure implementation.

Tests must include boundary addresses immediately inside and outside every range.

---

## 5.9 Apply policy to syntax, DNS, redirects, and reconnects — DONE (Phase 5B, commit pending; redirect/reconnect-specific interception deferred to Phase 8, which owns the HTTP client internals)

`station_validate_url()` remains pure for:

- Null/empty.
- Exact length.
- Control characters.
- HTTP/HTTPS scheme.
- Parseable host and port.
- Literal IP policy.

Add a device helper for DNS:

```c
esp_err_t url_policy_resolve_and_check(const char *host, const char *port)
{
    struct addrinfo hints = {0};
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;

    struct addrinfo *results = NULL;
    int rc = getaddrinfo(host, port, &hints, &results);
    if (rc != 0 || !results) return ESP_ERR_NOT_FOUND;

    esp_err_t err = ESP_OK;
    for (const struct addrinfo *it = results; it; it = it->ai_next) {
        if (!url_policy_sockaddr_allowed(it->ai_addr)) {
            err = ESP_ERR_INVALID_ARG;
            break;
        }
    }
    freeaddrinfo(results);
    return err;
}
```

Call it:

- Before initial HTTP open.
- After every playlist resolution.
- Before every redirect follow.
- Before every reconnect.

If `esp_http_client` automatically follows redirects internally, disable automatic redirect following and handle redirects explicitly so policy can run first.

Direct `/api/radio` play must call the same validation before enqueueing.

---

## 5.10 Tests

Add tests for:

- CRC known-answer.
- CRC catches one-byte corruption in header/payload.
- Exact blob header fields.
- Unterminated name and URL.
- Zero/duplicate IDs.
- Duplicate URLs.
- Bad `next_id` and header/store mismatch.
- V2 absent -> legacy path.
- V2 corrupt -> no legacy fallback and no seed overwrite.
- Both absent -> seed, persist, read back, publish.
- Seed persist failure -> not initialized.
- Legacy migration successful.
- Legacy index 0 -> stable ID 1.
- Legacy persistence/read-back failure leaves old key untouched.
- Every blocked IPv4/IPv6 range and boundary.
- Local-stream Kconfig override branch.
- Direct radio URL path invokes validation.

**Phase acceptance:** strict/ASan/UBSan + clean IDF build.

---

# Phase 6 — Correct Wi-Fi string handling, defaults, lifecycle, and error propagation

## 6.1 Fix validators so overflow is detectable — DONE (Phase 6, commit pending)

**File:** `components/wifi_mgr/wifi_mgr.c`

```c
static esp_err_t bounded_length(const char *value, size_t max_payload,
                                bool allow_empty, size_t *out_len)
{
    if (!value || !out_len) return ESP_ERR_INVALID_ARG;
    size_t len = strnlen(value, max_payload + 1u);
    if (len > max_payload) return ESP_ERR_INVALID_SIZE;
    if (!allow_empty && len == 0) return ESP_ERR_INVALID_SIZE;
    *out_len = len;
    return ESP_OK;
}
```

Use for SSID, STA password, and AP password. Do not use `strnlen(value, max)` followed by `len > max`.

---

## 6.2 Add exact NVS string loader — DONE (Phase 6, commit pending)

```c
static esp_err_t nvs_get_string_exact(nvs_handle_t h, const char *key,
                                      char *dst, size_t dst_capacity,
                                      size_t max_payload,
                                      size_t *out_payload_len)
{
    if (!key || !dst || dst_capacity == 0 || !out_payload_len)
        return ESP_ERR_INVALID_ARG;

    size_t stored_len = dst_capacity;
    esp_err_t err = nvs_get_str(h, key, dst, &stored_len);
    if (err != ESP_OK) return err;
    if (stored_len == 0 || stored_len > dst_capacity)
        return ESP_ERR_INVALID_SIZE;
    if (dst[stored_len - 1u] != '\0')
        return ESP_ERR_INVALID_CRC;

    size_t payload_len = stored_len - 1u;
    if (payload_len > max_payload)
        return ESP_ERR_INVALID_SIZE;

    *out_payload_len = payload_len;
    return ESP_OK;
}
```

Use it for STA and AP credentials. Never copy `stored_len` bytes and then append another terminator.

For credential pairs, distinguish:

- Both keys absent: no credentials.
- SSID present/pass absent: corruption unless open-network format intentionally stores empty pass key.
- Non-not-found NVS error: initialization error.

---

## 6.3 Initialize documented AP defaults before overrides — DONE (Phase 6, commit pending)

Replace `derive_ap_password()` with:

```c
static void set_default_ap_creds(void)
{
    static const char DEFAULT_SSID[] = "ESP32-S3-Audio";
    static const char DEFAULT_PASS[] = "password";

    memcpy(s_ap_ssid, DEFAULT_SSID, sizeof(DEFAULT_SSID));
    s_ap_ssid_len = sizeof(DEFAULT_SSID) - 1u;
    memcpy(s_ap_pass, DEFAULT_PASS, sizeof(DEFAULT_PASS));
    s_ap_pass_len = sizeof(DEFAULT_PASS) - 1u;
}
```

Call it before `load_ap_creds()`.

`load_ap_creds()` returns `esp_err_t`. Missing override keys keep defaults. Corrupt override keys return a visible error; do not partially apply one valid override and one corrupt override without documenting that behavior.

---

## 6.4 Make mode/config/action helpers return errors — DONE (Phase 6, commit pending)

Change signatures:

```c
static esp_err_t ensure_ap_config(void);
static esp_err_t apply_sta(void);
static esp_err_t apply_ap(void);
static esp_err_t apply_action(wifi_sm_action_t action);
static esp_err_t start_mdns(void);
```

Example AP config:

```c
static esp_err_t ensure_ap_config(void)
{
    wifi_config_t cfg = {0};
    if (s_ap_ssid_len == 0 || s_ap_ssid_len > sizeof(cfg.ap.ssid))
        return ESP_ERR_INVALID_SIZE;
    if (s_ap_pass_len > sizeof(cfg.ap.password))
        return ESP_ERR_INVALID_SIZE;

    memcpy(cfg.ap.ssid, s_ap_ssid, s_ap_ssid_len);
    cfg.ap.ssid_len = (uint8_t)s_ap_ssid_len;

    if (s_ap_pass_len > 0) {
        memcpy(cfg.ap.password, s_ap_pass, s_ap_pass_len);
        cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        cfg.ap.authmode = WIFI_AUTH_OPEN;
    }
    cfg.ap.max_connection = 4;
    return esp_wifi_set_config(WIFI_IF_AP, &cfg);
}
```

Example start:

```c
if (!s_wifi_started) {
    err = esp_wifi_start();
    if (err != ESP_OK) return err;
    s_wifi_started = true;
}
```

Check `set_mode`, AP config, STA config, disconnect/connect.

---

## 6.5 Do not publish RUNNING until initial action succeeds — DONE (Phase 6, commit pending)

In `wifi_mgr_init()`:

```c
wifi_sm_action_t action = wifi_sm_start(&s_sm);
err = apply_action(action);
if (err != ESP_OK) goto fail;

xSemaphoreTake(s_mgr_mtx, portMAX_DELAY);
s_state = WIFI_MGR_STATE_RUNNING;
xSemaphoreGive(s_mgr_mtx);
```

Record last error and diagnostic on failure.

---

## 6.6 Implement complete failure unwind — DONE (Phase 6, commit pending)

Track ownership:

```c
bool wifi_driver_inited = false;
bool sta_netif_created = false;
bool ap_netif_created = false;
bool wifi_handler_registered = false;
bool ip_handler_registered = false;
```

On failure, unwind in reverse order:

1. Stop Wi-Fi if started.
2. Unregister handlers created by this attempt.
3. Deinit Wi-Fi driver if initialized by this attempt.
4. Destroy default AP/STA netifs created by this attempt.
5. Reset pointers and flags.
6. Set lifecycle state UNINITIALIZED or FAULTED according to recoverability.

Do not destroy the global default event loop/netif subsystem if this component did not create/own it.

Verify ESP-IDF v5.5.1 signatures for default Wi-Fi netif destruction.

---

## 6.7 Guard every public API before taking mutexes — DONE (Phase 6, commit pending)

Add:

```c
static bool wifi_mgr_running(void)
{
    return s_mgr_mtx && s_state == WIFI_MGR_STATE_RUNNING;
}
```

For mutation APIs:

```c
if (!wifi_mgr_running()) return ESP_ERR_INVALID_STATE;
```

For snapshot APIs, return an explicit unavailable state in the output rather than reading uninitialized `s_sm`.

Do not call `xSemaphoreTake(NULL, ...)`.

---

## 6.8 Make AP enabled/config updates transactional — DONE (Phase 6, commit pending)

`wifi_mgr_set_ap_enabled()` currently publishes regardless of persistence error and ignores live apply errors.

Implement:

1. Snapshot old value.
2. Persist candidate.
3. Apply candidate to driver.
4. Publish runtime value only after driver success.
5. If driver application fails, persist old value back.
6. If rollback fails, enter FAULTED/MISMATCH and emit diagnostic.

Pseudo-code:

```c
bool old_enabled = s_ap_enabled;
esp_err_t err = save_ap_enabled(enabled);
if (err != ESP_OK) return err;

err = apply_ap_enabled_live(enabled);
if (err != ESP_OK) {
    esp_err_t rollback = save_ap_enabled(old_enabled);
    if (rollback != ESP_OK) {
        wifi_record_fault("ap_enabled_rollback", rollback);
    }
    return err;
}

s_ap_enabled = enabled;
return ESP_OK;
```

Use equivalent logic for AP credentials. Preserve old runtime strings until both persistence and live apply succeed.

---

## 6.9 Make event-handler connection failures visible — DONE (Phase 6, commit pending)

Check `esp_wifi_connect()` in STA_START and retry paths. Add a helper:

```c
static void wifi_async_error(const char *operation, esp_err_t err)
{
    if (err == ESP_OK) return;
    s_last_error = err; /* protect appropriately */
    ESP_LOGE(TAG, "%s failed: %s", operation, esp_err_to_name(err));
    printf("DIAG|WIFI|ERROR|op=%s,err=%s\n",
           operation, esp_err_to_name(err));
    fflush(stdout);
}
```

Do not create a tight reconnect loop. Feed failure into a bounded retry state.

---

## 6.10 mDNS — DONE (Phase 6, commit pending)

Check every call:

- `mdns_init`
- `mdns_hostname_set`
- `mdns_instance_name_set`
- `mdns_service_add`

If mDNS fails after Wi-Fi succeeds, Wi-Fi may remain RUNNING with `mdns_available=false`, but status and diagnostics must report the failure. This is an allowed degraded subcapability, not silent success.

---

## 6.11 Tests — DONE (Phase 6, commit pending)

Required tests:

- Exact 32-byte SSID accepted.
- 33-byte SSID rejected.
- Empty SSID rejected.
- 63-byte pass accepted.
- 64 non-hex rejected.
- 64 hex accepted only in enabled branch.
- NVS returned length includes terminator and payload length subtracts one.
- Maximum NVS strings do not overflow.
- Missing AP override retains default SSID/password.
- Corrupt override returns error.
- `esp_wifi_start()` failure leaves `s_wifi_started=false` and init not RUNNING.
- Mode/config/connect/disconnect failures propagate.
- API before init returns invalid state without semaphore call.
- Initialization failure fully unwinds and retry succeeds.
- AP-setting persistence failure preserves runtime state.
- Live-apply failure triggers rollback.

**Phase acceptance:** strict/ASan/UBSan + clean IDF build + first-boot AP hardware observation.

---

# Phase 7 — Repair radio session lifecycle, command worker, and PSRAM allocation

## 7.1 Change deinit to return `esp_err_t` — DONE (Phase 7, commit pending)

**Files:**

- `components/radio/include/radio.h`
- `components/radio/radio.c`
- all callers/tests

```c
esp_err_t radio_deinit(void);
```

A deinit timeout/failure must be visible to the caller.

Delete `session_destroy_force()`.

---

## 7.2 Add BUFFERING and preserve JOIN_PENDING ownership — DONE (Phase 7, commit pending)

Extend `radio_state_t`:

```c
typedef enum {
    RADIO_STATE_STOPPED = 0,
    RADIO_STATE_STARTING,
    RADIO_STATE_BUFFERING,
    RADIO_STATE_RUNNING,
    RADIO_STATE_STOPPING,
    RADIO_STATE_FAULTED,
    RADIO_STATE_FAULTED_JOIN_PENDING,
} radio_state_t;
```

The active session pointer remains set while JOIN_PENDING.

---

## 7.3 Define task and readiness bits — DONE (Phase 7, commit pending)

Update event definitions in `radio.h` or internal header:

```c
#define RADIO_EVT_STREAM_ENTERED   BIT(0)
#define RADIO_EVT_DECODER_ENTERED  BIT(1)
#define RADIO_EVT_STREAM_READY     BIT(2)
#define RADIO_EVT_DECODER_READY    BIT(3)
#define RADIO_EVT_STREAM_EXITED    BIT(4)
#define RADIO_EVT_DECODER_EXITED   BIT(5)
```

Command-worker exit uses a module lifecycle event group or task notification, not polling a handle.

Workers set ENTERED immediately, READY only after the required operational checks, and EXITED at the final line before self-delete.

---

## 7.4 Implement safe session join/destroy — DONE (Phase 7, commit pending)

```c
static bool session_all_exited(const radio_session_t *s)
{
    EventBits_t bits = xEventGroupGetBits(s->events);
    return (bits & (RADIO_EVT_STREAM_EXITED | RADIO_EVT_DECODER_EXITED)) ==
           (RADIO_EVT_STREAM_EXITED | RADIO_EVT_DECODER_EXITED);
}

static esp_err_t session_join(radio_session_t *s, TickType_t timeout)
{
    EventBits_t bits = xEventGroupWaitBits(
        s->events,
        RADIO_EVT_STREAM_EXITED | RADIO_EVT_DECODER_EXITED,
        pdFALSE, pdTRUE, timeout);
    return ((bits & RADIO_EVT_ALL_EXITED) == RADIO_EVT_ALL_EXITED)
        ? ESP_OK : ESP_ERR_TIMEOUT;
}

static void session_destroy_joined(radio_session_t *s)
{
    configASSERT(s != NULL);
    configASSERT(session_all_exited(s));
    vEventGroupDelete(s->events);
    free(s);
}
```

No function may free if the assertion is false.

---

## 7.5 Rewrite `radio_stop_sync()` — DONE (Phase 7, commit pending)

Required behavior:

```c
xSemaphoreTake(g_radio_control_mtx, portMAX_DELAY);
radio_session_t *session = s_active_session;
if (!session) {
    g_radio_state = RADIO_STATE_STOPPED;
    xSemaphoreGive(g_radio_control_mtx);
    return ESP_OK;
}

g_radio_state = RADIO_STATE_STOPPING;
atomic_store(&session->stop_requested, true);
xSemaphoreGive(g_radio_control_mtx);

esp_err_t join_err = session_join(session, pdMS_TO_TICKS(RADIO_STOP_TIMEOUT_MS));

xSemaphoreTake(g_radio_control_mtx, portMAX_DELAY);
if (join_err != ESP_OK) {
    g_radio_state = RADIO_STATE_FAULTED_JOIN_PENDING;
    radio_set_error_locked(RADIO_ERR_STOP_TIMEOUT, "worker join timeout");
    xSemaphoreGive(g_radio_control_mtx);
    return join_err;
}

if (s_active_session == session) s_active_session = NULL;
g_radio_state = RADIO_STATE_STOPPED;
xSemaphoreGive(g_radio_control_mtx);
session_destroy_joined(session);
return ESP_OK;
```

If a newer generation could replace the pointer, compare generation/pointer before clearing. Serialization should normally prevent overlap.

---

## 7.6 Rewrite `radio_deinit()` — DONE (Phase 7, commit pending)

Sequence:

1. If active session exists, call stop/join.
2. If stop fails, return immediately with all resources retained.
3. Signal command-worker shutdown.
4. Wait for command-worker EXITED bit.
5. If timeout, return with queue/mutex/rings retained.
6. Delete command queue.
7. Delete mutexes.
8. Free rings.
9. Reset globals.

Never dereference a saved session after a function that may have freed it.

---

## 7.7 Fix partial worker creation — DONE (Phase 7, commit pending)

In play:

1. Allocate session and events.
2. Create stream task.
3. If decoder create fails, set stop and join stream.
4. If stream join times out, attach the session as active JOIN_PENDING so it remains owned and recoverable.
5. If join succeeds, destroy session and return task-create error.

Do not ignore wait bits.

---

## 7.8 Publish BUFFERING, not RUNNING, after task entry — DONE (Phase 7, commit pending)

Wait for both ENTERED bits with timeout. If a worker exits before both enter, treat startup as failure and join.

After both enter:

```c
g_radio_state = RADIO_STATE_BUFFERING;
```

Transition to RUNNING only after both READY bits, or after decoder produces valid PCM according to the final chosen contract. Keep the rule in one helper:

```c
void radio_try_publish_running(radio_session_t *s)
{
    EventBits_t bits = xEventGroupGetBits(s->events);
    if ((bits & (RADIO_EVT_STREAM_READY | RADIO_EVT_DECODER_READY)) ==
        (RADIO_EVT_STREAM_READY | RADIO_EVT_DECODER_READY)) {
        radio_set_state_for_generation(s->generation, RADIO_STATE_RUNNING);
    }
}
```

Generation-check every worker state update so stale workers cannot overwrite a newer session.

---

## 7.9 Make radio initialization all-or-nothing — DONE (Phase 7, commit pending)

Allocate local candidates:

```c
uint8_t *compressed = heap_caps_malloc(ring_bytes,
    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
if (!compressed) return ESP_ERR_NO_MEM;

uint8_t *pcm = heap_caps_malloc(PCM_RING_BYTES,
    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
if (!pcm) {
    heap_caps_free(compressed);
    return ESP_ERR_NO_MEM;
}
```

Then create mutexes/queue/task. On any failure, delete every local object and free both buffers. Publish globals only at the end.

Do not call plain `malloc()` as fallback.

Use `heap_caps_free()` consistently for capability allocations.

Reject `ring_bytes == 0` before allocation/ring arithmetic.

---

## 7.10 Add command-worker exit acknowledgement — DONE (Phase 7, commit pending)

Create module event bit:

```c
#define RADIO_MODULE_EVT_CMD_EXITED BIT(0)
```

At command task exit:

```c
xEventGroupSetBits(s_module_events, RADIO_MODULE_EVT_CMD_EXITED);
vTaskDelete(NULL);
```

Lifecycle owner clears `s_radio_cmd_task` after observing the bit. Do not poll the handle for four seconds and then delete its queue anyway.

---

## 7.11 Fix prebuffer default and error reporting — DONE (Phase 7, commit pending)

Initialize atomic at compile time to default bytes or explicitly store default before NVS read:

```c
static _Atomic int g_radio_prebuffer_ms = PREBUF_MS_DEFAULT;

static esp_err_t radio_prebuffer_load(void)
{
    atomic_store(&g_radio_prebuffer_ms, PREBUF_MS_DEFAULT);

    nvs_handle_t h = 0;
    esp_err_t err = nvs_open(NVS_NS_RADIO, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;

    int32_t value = PREBUF_MS_DEFAULT;
    err = nvs_get_i32(h, NVS_KEY_PREBUF, &value);
    nvs_close(h);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    if (value < PREBUF_MS_MIN || value > PREBUF_MS_MAX)
        return ESP_ERR_INVALID_SIZE;

    atomic_store(&g_radio_prebuffer_ms, value);
    return ESP_OK;
}
```

If init chooses to continue with default after a non-not-found read error, record it in status and diagnostics; do not return/log as if load succeeded.

---

## 7.12 Tests — DONE (Phase 7, commit pending)

Required tests:

- Successful stop joins before destroy.
- Stop timeout retains active session and event group.
- Deinit after stop timeout returns timeout and frees nothing.
- Retry stop after exit bits succeeds and then frees.
- No saved-pointer dereference after destroy.
- Decoder-task create failure joins stream before free.
- Join timeout on partial creation retains session.
- Both task ENTERED bits required before BUFFERING.
- READY bits required before RUNNING.
- Stale generation cannot update state.
- Compressed PSRAM allocation failure does not attempt generic malloc.
- PCM PSRAM allocation failure frees compressed buffer and does not publish globals.
- Queue/task creation failure unwinds all allocations.
- Command-worker timeout retains queue/mutex/rings.
- Fresh missing prebuffer key yields 3000 ms.

**Phase acceptance:** strict/ASan/UBSan + clean IDF build.

---

# Phase 8 — Repair radio reconnect, playlist, HTTP, decoder, and resampler recovery

## 8.1 Replace event-bit backoff with a stop-aware delay — DONE (Phase 8, commit pending)

**File:** `components/radio/radio_stream.c`

Do not wait on a permanently set STARTED bit.

Reference helper:

```c
static bool wait_backoff_or_stop(radio_session_t *s, uint32_t delay_ms)
{
    const uint32_t quantum_ms = 100;
    uint32_t waited = 0;
    while (waited < delay_ms) {
        if (atomic_load(&s->stop_requested)) return false;
        uint32_t step = (delay_ms - waited < quantum_ms)
            ? (delay_ms - waited) : quantum_ms;
        vTaskDelay(pdMS_TO_TICKS(step));
        waited += step;
    }
    return !atomic_load(&s->stop_requested);
}
```

Backoff calculation:

```c
static uint32_t reconnect_delay_ms(uint32_t attempt)
{
    static const uint32_t schedule[] = {500, 1000, 2000, 4000, 8000, 15000};
    size_t idx = attempt < (sizeof(schedule) / sizeof(schedule[0]))
        ? attempt : (sizeof(schedule) / sizeof(schedule[0]) - 1u);
    return schedule[idx];
}
```

Reset attempts only after a documented stable-success threshold, not merely task entry.

---

## 8.2 Make playlist resolution typed and fail closed — DONE (Phase 8, commit pending)

Replace a best-effort `resolve_url()` with:

```c
typedef enum {
    RADIO_INPUT_DIRECT = 0,
    RADIO_INPUT_PLAYLIST,
} radio_input_kind_t;

typedef struct {
    radio_input_kind_t kind;
    char resolved_url[RADIO_URL_MAX];
} radio_resolution_t;

esp_err_t radio_resolve_input(const char *input, radio_resolution_t *out);
```

Behavior:

- Detect known playlist extensions case-insensitively before query/fragment.
- Fetch playlist with size limit.
- Accept supported M3U/PLS syntax.
- Reject empty/malformed/oversized playlists.
- Validate selected URL and destination policy.
- If classified playlist resolution fails, return the failure.
- If classified direct, copy only after exact-length validation.

Delete the fallback that restores the original playlist URL after parse failure.

Add error details such as `playlist_fetch`, `playlist_parse`, `playlist_empty`, `playlist_url_blocked`.

---

## 8.3 Validate redirects explicitly — DONE (Phase 8, commit pending)

Configure HTTP client so redirects are surfaced to code. For each 3xx:

1. Read Location.
2. Resolve relative URL against current URL.
3. Validate exact length and scheme.
4. Run literal/DNS destination policy.
5. Enforce a maximum redirect count, for example 5.
6. Only then reconnect/follow.

Reject redirects to local/private destinations even when initial URL was public.

---

## 8.4 Classify permanent versus transient stream failures — DONE (Phase 8, commit pending)

Permanent failures should fault without endless reconnect:

- Unsupported scheme/content.
- SSRF policy rejection.
- Malformed playlist.
- Unsupported codec/content type.
- Repeated decoder contract violation.

Transient failures may reconnect with backoff:

- DNS temporarily unavailable.
- TCP/TLS open failure.
- Read timeout/disconnect.
- 5xx response, within a bounded policy.

4xx responses should normally be permanent except explicitly retryable statuses.

Store HTTP status and exact error detail.

---

## 8.5 Bound decoder-open failures — DONE (Phase 8, commit pending)

**File:** `components/radio/radio_decode.c`

Add constants:

```c
#define DECODER_MAX_OPEN_FAILURES 3u
#define DECODER_MAX_NO_PROGRESS 64u
#define DECODER_MAX_RESYNC_DROP_BYTES 4096u
#define RESAMPLER_MAX_NO_PROGRESS 8u
```

Track counters per session/connection. On decoder-open failure:

```c
open_failures++;
g_radio_decode_errors++;
if (open_failures >= DECODER_MAX_OPEN_FAILURES) {
    radio_session_fault(s, RADIO_ERR_DECODER_OPEN_FAILED,
                        "decoder open threshold");
    break;
}
```

Reset counter only after a successful decoder open.

---

## 8.6 Check decoder info and consumption contracts — DONE (Phase 8, commit pending)

Every decoder call must check return status. Example:

```c
esp_audio_simple_dec_info_t info = {0};
esp_err_t err = esp_audio_simple_dec_get_info(decoder, &info);
if (err != ESP_OK || info.sample_rate <= 0 ||
    (info.channel != 1 && info.channel != 2)) {
    radio_session_fault(s, RADIO_ERR_DECODER_CONTRACT,
                        "invalid decoder info");
    break;
}
```

When decoder reports consumed bytes:

```c
if (consumed > input_len) {
    radio_session_fault(s, RADIO_ERR_DECODER_CONTRACT,
                        "consumed beyond input");
    break;
}
```

No unsigned underflow or buffer advance may occur.

---

## 8.7 Bound no-progress byte resynchronization — DONE (Phase 8, commit pending)

Current silent one-byte drops must become explicit and bounded:

```c
if (consumed == 0 && produced == 0 && input_full) {
    no_progress_count++;
    if (resync_drop_bytes >= DECODER_MAX_RESYNC_DROP_BYTES ||
        no_progress_count >= DECODER_MAX_NO_PROGRESS) {
        radio_session_fault(s, RADIO_ERR_DECODER_STALLED,
                            "decoder no progress");
        break;
    }

    drop_one_input_byte();
    resync_drop_bytes++;
    if ((resync_drop_bytes % 256u) == 1u) {
        ESP_LOGW(TAG, "decoder resync dropped %u bytes", resync_drop_bytes);
    }
} else {
    no_progress_count = 0;
}
```

Expose resync drop count in status if practical.

---

## 8.8 Check resampler initialization and progress — DONE (Phase 8, commit pending)

```c
esp_err_t err = radio_resampler_init(&rs, in_rate, in_channels,
                                     I2S_OUT_SAMPLE_RATE_HZ, 2);
if (err != ESP_OK) {
    radio_session_fault(s, RADIO_ERR_RESAMPLER_STALLED,
                        "resampler init failed");
    break;
}
rs_ready = true;
```

During processing, if neither input is consumed nor output produced, increment a counter. Fault after `RESAMPLER_MAX_NO_PROGRESS`.

Never set `rs_ready=true` after failed init.

---

## 8.9 Make worker fault publication generation-safe — DONE (Phase 8, commit pending)

Add one helper in radio core:

```c
void radio_session_fault(radio_session_t *s, radio_err_t error,
                         const char *detail)
{
    atomic_store(&s->stop_requested, true);
    xSemaphoreTake(g_radio_control_mtx, portMAX_DELAY);
    if (s_active_session == s && s->generation == s_active_session->generation) {
        g_radio_last_error = error;
        strlcpy(g_radio_last_error_detail, detail ? detail : "",
                sizeof(g_radio_last_error_detail));
        g_radio_state = RADIO_STATE_FAULTED;
    }
    xSemaphoreGive(g_radio_control_mtx);
}
```

Do not let a stale session fault a newer generation.

If peer worker has not exited, lifecycle may ultimately expose JOIN_PENDING rather than plain FAULTED.

---

## 8.10 Tests — DONE (Phase 8, commit pending)

Required tests:

- Backoff waits after a previous STARTED/READY event.
- Backoff schedule caps at 15 seconds.
- Stop interrupts backoff.
- `.pls` fetch/parse failure does not return original URL.
- Direct URL passes through.
- Redirect to private IP is rejected.
- Redirect limit enforced.
- Unsupported content is permanent.
- Open failure threshold faults on third consecutive failure.
- Successful open resets threshold.
- Decoder consumed > supplied faults.
- No-progress byte drops stop at bounded threshold.
- Resampler init failure faults.
- Resampler no-progress threshold faults.
- Stale generation cannot publish error/state.

**Phase acceptance:** strict/ASan/UBSan + clean IDF build + hardware network interruption test.

---

# Phase 9 — Synchronize control configuration and make resume/scan truthful

## 9.1 Pass immutable config snapshots to actions — DONE (Phase 9, commit pending)

**File:** `components/ctrl/ctrl.c`

Change:

```c
static ctrl_action_t do_action(ctrl_action_t act)
```

To:

```c
static ctrl_action_t do_action(ctrl_action_t act, const ctrl_cfg_t *cfg)
```

Use `cfg->sink_mac`, `cfg->volume`, and `cfg->last_station_id`. Do not read `s_cfg` inside action execution.

At the beginning of each operation or loop iteration:

```c
ctrl_cfg_t cfg;
ctrl_get_cfg(&cfg);
```

For a long-running orchestration, decide whether config changes should apply immediately. Baseline FIX3 behavior: use a stable snapshot for one connection/resume attempt; take a new snapshot before the next independent retry cycle.

Delete:

```c
s_cfg = initial_cfg;
```

---

## 9.2 Prevent duplicate orchestrator tasks — DONE (Phase 9, commit pending)

Under `s_mtx`:

```c
if (s_task != NULL) {
    xSemaphoreGive(s_mtx);
    return ESP_ERR_INVALID_STATE;
}
```

Task exit must set an exit bit; lifecycle/controller code clears handle after acknowledgement, or set/clear under the same mutex with a documented safe pattern.

Add a stop API if restart is required. Do not overwrite a live task handle.

---

## 9.3 Persist control candidates before publish — DONE (Phase 9, commit pending)

`ctrl_set_sink()`:

```c
xSemaphoreTake(s_mtx, portMAX_DELAY);
ctrl_cfg_t candidate = s_cfg;
xSemaphoreGive(s_mtx);

/* validate and mutate candidate */
esp_err_t err = ctrl_cfg_save(&candidate);
if (err != ESP_OK) return err;

xSemaphoreTake(s_mtx, portMAX_DELAY);
s_cfg = candidate;
xSemaphoreGive(s_mtx);
return ESP_OK;
```

To avoid lost updates when two setters race, serialize the complete update with a dedicated update mutex, or add a generation counter and compare/retry before publication.

Simplest safe option:

- Add `s_update_mtx`.
- Hold it across snapshot, persistence, and publication.
- Continue to use `s_mtx` only for short in-memory access.

Use the same pattern for `ctrl_note_station()`.

Do not assign `s_cfg` before `ctrl_cfg_save()`.

---

## 9.4 Repair old control migration — DONE (Phase 9, commit pending)

Current V0 conversion casts station index directly to stable ID. Replace with the station migration mapping. Index 0 must map to ID 1.

Because `ctrl_cfg_load()` currently has no station-store parameter, choose one explicit design:

1. Move V0 migration into a coordinator that has the migrated station store; or
2. Load V0 into a temporary structure with `migration_pending=true`, then finalize after stations initialize.

Do not guess the stable ID.

Add CRC/version validation to control persistence if not already covered by a later schema. At minimum, malformed blobs must not silently become valid config without a diagnostic.

---

## 9.5 Make resume success conditional — DONE (Phase 9, commit pending)

Refactor resume into a result function:

```c
typedef enum {
    CTRL_RESUME_OK = 0,
    CTRL_RESUME_VOLUME_FAILED,
    CTRL_RESUME_NO_STATION,
    CTRL_RESUME_STATION_NOT_FOUND,
    CTRL_RESUME_PLAY_ENQUEUE_FAILED,
} ctrl_resume_result_t;
```

Required flow:

1. Set volume.
2. Require transport `ESP_OK` and command state DONE_OK.
3. Resolve station ID.
4. Require found station.
5. Enqueue radio play.
6. Only then emit RESUME_DONE/ok.

On failure, emit a new FSM event such as `CTRL_EV_RESUME_FAILED` with reason, or transition to a documented failure state.

Do not treat “no station configured” as resume success when the state machine expected a station.

---

## 9.6 Make scan phases return results and rollback explicitly — DONE (Phase 9, commit pending)

Add:

```c
typedef enum {
    CTRL_SCAN_OK = 0,
    CTRL_SCAN_RADIO_STOP_FAILED,
    CTRL_SCAN_DISCONNECT_FAILED,
    CTRL_SCAN_COMMAND_FAILED,
    CTRL_SCAN_RECONNECT_FAILED,
    CTRL_SCAN_VOLUME_FAILED,
    CTRL_SCAN_RADIO_RESUME_FAILED,
} ctrl_scan_result_t;
```

Store final result and failed phase in a status object protected by mutex/atomic fields.

For every WROOM command, check both transport and command state:

```c
esp_err_t err = bt_link_send("DISCONNECT", &st, NULL, 0, NULL, 0);
if (err != ESP_OK || st != BT_LINK_CMD_DONE_OK) {
    result = CTRL_SCAN_DISCONNECT_FAILED;
    phase = SCAN_ROLLBACK;
    break;
}
```

If radio stop times out, do not continue to inquiry.

If scan command fails, do not sleep 15 seconds pretending inquiry is active.

Rollback should attempt restoration only for state that was actually changed. Track booleans:

```c
bool radio_stopped;
bool sink_disconnected;
bool sink_reconnected;
bool volume_restored;
bool radio_resumed;
```

Final diagnostic:

```c
printf("DIAG|CTRL|SCAN_DONE|restored=%d,result=%d,failed_phase=%s\n",
       restored ? 1 : 0, (int)result, phase_name);
```

Log “A2DP restored” only when `restored=true`.

---

## 9.7 Update scan start-state recognition — DONE (Phase 9, commit pending)

With new radio state, a resumed radio is accepted when it reaches BUFFERING or RUNNING. STARTING alone is not enough evidence of operational startup.

Fault/JOIN_PENDING should terminate the wait immediately with failure rather than waiting until timeout.

---

## 9.8 Tests — DONE (Phase 9, commit pending)

Required tests:

- Orchestrator uses snapshot while concurrent config setter changes global.
- `ctrl_start()` rejects duplicate task.
- Persistence failure leaves `s_cfg` unchanged.
- Two concurrent setters do not lose updates.
- V0 station index 0 maps to stable ID 1.
- Volume failure does not emit RESUME_DONE.
- Missing station does not emit RESUME_DONE.
- Play enqueue failure does not emit RESUME_DONE.
- Radio stop timeout aborts scan before disconnect/scan.
- Scan command failure skips inquiry wait.
- Reconnect failure reports restore failure.
- Volume failure reports partial restore failure.
- Radio resume fault exits wait early.
- Final restored marker is truthful.

**Phase acceptance:** strict/ASan/UBSan + clean IDF build.

---

# Phase 10 — Harden degraded boot and capability boundaries

## 10.1 Add runtime capability structure — DONE (Phase 10, commit pending)

**Files:**

- `main/boot_status.h`
- `main/main.c`
- Add `main/runtime_capabilities.c/.h` or a suitable component-visible module

Example:

```c
typedef struct {
    bool i2s;
    bool audio_task;
    bool bt_link;
    bool radio;
    bool stations;
    bool ctrl;
    bool wifi;
    bool web;
} runtime_capabilities_t;

void runtime_capabilities_publish(const runtime_capabilities_t *caps);
void runtime_capabilities_get(runtime_capabilities_t *out);
```

Use a mutex or atomic bitset. Web status and handlers use this rather than assuming boot success.

---

## 10.2 Correct dependency checks — DONE (Phase 10, commit pending)

Audio task creation currently checks only I2S despite its comment. Decide actual dependency:

- The task can safely run with I2S and no radio because it can generate tone/silence.
- Therefore update the comment and source-selection code to treat radio unavailable explicitly.
- Do not call radio APIs if radio init failed unless those APIs are guaranteed safe and return unavailable.

Control start must be skipped or return unavailable when required components are missing.

Web may start without BT/radio/stations, but dependent routes return 503.

---

## 10.3 Add centralized capability guards to HTTP handlers — DONE (Phase 10, commit pending)

Example:

```c
static esp_err_t require_capability(httpd_req_t *req, bool available,
                                    const char *code, const char *message)
{
    if (available) return ESP_OK;
    return web_send_error(req, "503 Service Unavailable",
                          code, message, true);
}
```

Use stable codes:

- `I2S_UNAVAILABLE`
- `BT_LINK_UNAVAILABLE`
- `RADIO_UNAVAILABLE`
- `STATIONS_UNAVAILABLE`
- `CTRL_UNAVAILABLE`
- `WIFI_UNAVAILABLE`

The handler must return immediately after sending 503.

---

## 10.4 Check every task creation — DONE (Phase 10, commit pending)

In `main.c` and `clock_diag.c`, check:

- Audio task.
- Link probe task.
- Clock diagnostic task.
- Any helper task introduced in previous phases.

Example:

```c
BaseType_t created = xTaskCreate(...);
if (created != pdPASS) {
    printf("DIAG|BOOT|DEGRADED|component=link_probe,err=NO_MEM\n");
}
```

Do not claim the optional task started.

---

## 10.5 Make destructive NVS recovery visible — DONE (Phase 10, commit pending)

Before erase:

```c
printf("DIAG|NVS|ERASE_REQUIRED|reason=%s\n", esp_err_to_name(err));
fflush(stdout);
```

After successful erase/reinit:

```c
printf("DIAG|NVS|ERASED|credentials_lost=1,stations_lost=1,auth_lost=1\n");
```

Do not log old secret values.

---

## 10.6 Update status API — DONE (Phase 10, commit pending)

Expose:

- Capability booleans.
- I2S state and join-pending.
- Radio state and join-pending.
- Wi-Fi manager last error.
- Station persistence status/corruption/migration pending.
- Auth ready boolean only, never token.
- Control scan result/failed phase.

The frontend must distinguish unavailable from empty/idle.

---

## 10.7 Tests — DONE (Phase 10, commit pending)

Required tests:

- Web starts with BT unavailable; BT route returns 503.
- Web starts with radio unavailable; radio route returns 503.
- Wi-Fi setter before init returns invalid state.
- Station route when persistence corrupt returns appropriate unavailable/recovery status.
- Control start skipped/fails visibly when dependencies absent.
- Optional task creation failure marks degraded.
- No null semaphore/queue calls in any degraded route.

**Phase acceptance:** strict/ASan/UBSan + clean IDF build.

---

# Phase 11 — Add frontend authenticated mutation flow and truthful errors

## 11.1 Centralize API requests — DONE (Phase 11, commit pending)

**Files:**

- `web/src/api.ts`
- all components that call `fetch`

Reference helper:

```ts
const TOKEN_KEY = "esp_i2s_auth_token";

export function getAuthToken(): string {
  return sessionStorage.getItem(TOKEN_KEY) ?? "";
}

export function setAuthToken(token: string): void {
  if (!/^[0-9a-f]{64}$/.test(token)) {
    throw new Error("Token must be exactly 64 lowercase hexadecimal characters");
  }
  sessionStorage.setItem(TOKEN_KEY, token);
}

export async function apiRequest<T>(
  path: string,
  init: RequestInit = {},
): Promise<T> {
  const method = (init.method ?? "GET").toUpperCase();
  const mutating = method === "POST" || method === "PUT" || method === "DELETE";
  const headers = new Headers(init.headers);
  headers.set("Accept", "application/json");

  if (mutating) {
    const token = getAuthToken();
    if (!token) throw new ApiError(401, "AUTH_REQUIRED", "Enter the device token");
    headers.set("Authorization", `Bearer ${token}`);
  }

  const response = await fetch(path, { ...init, method, headers });
  const text = await response.text();
  let payload: unknown = null;
  if (text) {
    try { payload = JSON.parse(text); }
    catch { throw new ApiError(response.status, "INVALID_RESPONSE", text); }
  }

  if (!response.ok) {
    const body = payload as { code?: string; message?: string } | null;
    throw new ApiError(
      response.status,
      body?.code ?? "HTTP_ERROR",
      body?.message ?? response.statusText,
    );
  }
  return payload as T;
}
```

No component should call raw `fetch()` for API mutation after this phase.

---

## 11.2 Add token-entry UI — DONE (Phase 11, commit pending)

Add a small authentication panel/dialog:

- Exact 64-lowercase-hex validation.
- Password-style input.
- Save to session storage.
- Clear token action.
- On 401, show token invalid/required and open the panel.
- Do not render the token in normal text.
- Do not include it in URL/query strings.

Optional “remember on this browser” must be explicit and may use localStorage only after user choice.

---

## 11.3 Surface 503 and operation failures — DONE (Phase 11, commit pending)

Each UI action must distinguish:

- 401 auth.
- 409 operation already running/state conflict.
- 422 invalid input.
- 503 component unavailable.
- 500 persistence/driver failure.

Do not convert a rejected mutation into optimistic success. Roll optimistic UI state back on failure.

---

## 11.4 Update tests — DONE (Phase 11, commit pending)

Add Vitest tests:

- Mutating request adds Authorization header.
- GET request does not require token.
- Missing token prevents mutation before network call.
- Invalid token rejected by setter.
- 401 opens auth flow.
- 503 displays capability-specific error.
- Invalid JSON response is visible.
- No direct mutation fetch remains.

Update Playwright mock server to require auth for mutations and test both unauthorized and authorized flows.

---

## 11.5 Rebuild embedded SPA — DONE (Phase 11, commit pending)

From clean dependencies:

```bash
cd web
rm -rf node_modules
npm ci
npm run build
npm test
```

Confirm `main/www/index.html.gz` and `.sha256` correspond to the new build. Review the diff size and ensure no token is embedded.

**Phase acceptance:** frontend unit/E2E mock tests + full host verification + clean IDF build.

---

# Phase 12 — Final verification, hardware gates, and documentation reconciliation

**STATUS: DONE (with documented exceptions) — 2026-07-22.** See
`ESP_I2S_SOURCE_RUNTIME_SAFETY_INTEGRITY_FIX3_SUMMARY_2026-07-22.md` for the
full implementation summary (commits by phase, evidence, deviations,
remaining limitations). Per-item status below.

## 12.1 Run clean host verification — DONE

`rm -rf test/host_test/build_host_tests_* web/node_modules && ./tools/verify_host.sh`
exit 0. 26 host-test suites, 23 frontend (vitest) tests, strict warnings +
ASan + UBSan all enabled. Re-run again after the i2s/console fixes
(commits 232f7d90, 62d3afa7) — still exit 0.

From `esp_i2s_source/`:

```bash
rm -rf test/host_test/build_host_tests_*
rm -rf web/node_modules
./tools/verify_host.sh
```

**Required:** exit status 0.

Record:

- Test count.
- Compiler version.
- Node/npm version.
- Sanitizer modes.
- Frontend test count.

---

## 12.2 Run clean ESP-IDF build — DONE

`idf.py fullclean && idf.py set-target esp32s3 && idf.py build` exit 0.
`idf.py size`: DIRAM 60.22%, IRAM 100%, no static-memory red flags. Binary
0x1bf240 B, 71% of the app partition free. Rebuilt clean again after the
final fixes; partition fit unchanged.

---

## 12.3 Run authentication hardware checks — PARTIAL (see exceptions)

1. Erase or use clean NVS for first-boot test.
2. Capture one `AUTH|BOOTSTRAP_TOKEN|...` marker.
3. Verify token is exactly 64 lowercase hex.
4. Verify it appears only once after commit.
5. Curl a mutating route without auth -> 401.
6. Curl with wrong auth -> 401.
7. Curl with correct auth -> handler result.
8. Force NVS token commit failure using test injection/device test; web must not start.
9. Rotate over local USB console; old token fails and new token succeeds.

Do not paste the real token into committed logs.

**Evidence:** item 9 (AUTH ROTATE over local USB console: old token invalidated,
new token active) verified live via curl against the running device.
Items 1, 8 require a fresh/erased NVS or fault-injection harness this
session deliberately did not run — **user explicitly chose to skip the
destructive fresh-NVS-erase gate** (would also wipe WiFi creds/stations/
ctrl config) rather than have it run unattended; documented here as **not
re-verified**, not silently dropped. Items 5–7 (401/401/200 on missing/
wrong/correct auth) were exercised repeatedly all night as an ordinary
side effect of every authenticated API call made during live debugging —
every mutating call in this session required and used the live token.

---

## 12.4 Run I2S hardware checks — PARTIAL (see exceptions)

1. Boot S3 with WROOM32 absent/no clocks.
2. Confirm boot completes and I2S is WAITING_FOR_CLOCK.
3. Confirm no watchdog reset.
4. Start WROOM32 clocks; confirm RUNNING transition.
5. Remove clocks; confirm WAITING transition.
6. Reapply clocks; confirm recovery without restart.
7. Stop/deinit/reinit through a device test and confirm no join timeout or invalid delete.
8. Confirm ring peak telemetry changes.

**Evidence:** items 2–3 and 8 observed organically and repeatedly this
session — the WROOM32 was live and clocking throughout, and the DMA-
starvation bug fixed in commit d2dc7f67 was itself discovered and
diagnosed via exactly this telemetry (`DIAG|I2S|state=...` transitioning
WAITING_FOR_CLOCK/RUNNING correctly post-fix, `ringpeak` populated and
non-zero). Items 1, 4–7 require physically disconnecting/reconnecting the
WROOM32's clock lines — **user explicitly chose to skip this destructive/
physical gate**; documented as **not re-verified**, not silently dropped.
Item 7 (deinit/reinit lifecycle) is covered by host tests
(`test_i2s_lifecycle`, 14 cases) but not repeated on hardware this
session.

---

## 12.5 Run BT link failure checks — PARTIAL

1. Boot with WROOM32 absent.
2. Verify web BT endpoints return 503, not crash.
3. Reconnect WROOM32 and initialize/reboot according to supported lifecycle.
4. Inject UART write failure or disconnect cable during a command.
5. Verify immediate local transport error, not only peer timeout.
6. Stop while one active and at least one queued request exists; all callers must wake with cancellation.

**Evidence:** the BT link was live and exercised heavily all session (scan,
connect/disconnect/reconnect cycles via `/api/bt`, tone/radio streaming
over A2DP for hours) with zero crashes or hangs. Items 1, 4, 6 (absent-
WROOM32 boot, UART fault injection, queued-request cancellation) are
covered by host tests but not repeated as a live physical fault-injection
on hardware this session — **not re-verified**, consistent with the same
user decision to skip physical/destructive gates.

---

## 12.6 Run station persistence checks — DONE (organic corruption, not injected)

1. Verify CRC known-answer in host tests.
2. Save stations and reboot.
3. Flip one byte in V2 blob using a device-test helper.
4. Reboot and confirm corruption marker.
5. Confirm original V2 key is not overwritten.
6. Test valid legacy migration and last-station mapping.
7. Power-cycle after V2 commit and read-back.

**Evidence:** item 1 covered by host tests (`stations_crc32_ieee` known-
answer test). Items 4–5 verified for real, not via deliberate byte-flip
(item 3 not separately run) — the device's actual persisted `stations_v2`
blob was found genuinely CRC-corrupt (`STATIONS_BLOB_BAD_CRC`, likely
stale from earlier in the multi-day FIX3 effort) during this session's
station add/delete stressor. Confirmed exactly the required behavior:
`DIAG|STATIONS|CORRUPT|key=stations_v2,reason=validation` fired, the
corrupt key was never auto-overwritten (stayed corrupt across a reboot
until explicitly cleared), and `capabilities.stations` correctly reported
false with station mutations failing closed (`STATIONS_UNAVAILABLE`, not
a crash). This gap led to a genuine bug **found this session**: no
recovery path existed short of a full NVS erase. Fixed in commit
62d3afa7 (`stations_reset_persisted()` + `STATIONS RESET` console
command); verified on hardware (recovery to 5 seeded stations, add/delete
round-trip against the live list afterward). Items 2, 6–7 (fresh
save+reboot, legacy migration, power-cycle read-back) not separately
re-run this session — covered by host tests only.

---

## 12.7 Run Wi-Fi checks — PARTIAL

1. Fresh boot AP is `ESP32-S3-Audio` / `password`.
2. Exact 32-byte SSID provisions without overflow.
3. Oversized SSID/password is rejected.
4. Invalid 64-character non-hex password is rejected.
5. Inject `esp_wifi_start`/set-config/connect failure in device test and confirm no false RUNNING.
6. Confirm retry after cleaned failed init.
7. Confirm AP config persistence and live-apply rollback behavior.

**Evidence:** item 1 confirmed organically — observed the device's own
`MODE=AP,SSID=ESP32-S3-Audio` fallback live when its STA connection
dropped after a router channel change. STA reprovisioning (`WIFI <ssid>
<pass>` over serial console) was exercised twice this session, live,
across two different real networks, both successful. A new (non-FIX3-
spec) observation: on losing its STA connection the device falls back to
AP correctly, but does **not** auto-reconnect to the original SSID once
it reappears — it required a manual reprovision. Not treated as a bug
tonight (out of Phase 12 scope) but flagged for follow-up. Items 2–7
require fault-injection/oversized-input device tests not run live this
session — covered by host tests only (`test_wifi_creds_core*`).

---

## 12.8 Run radio checks — PARTIAL

1. Direct MP3.
2. Direct AAC if supported.
3. Valid PLS/M3U.
4. Malformed playlist -> explicit failure, not decoder input.
5. Public redirect -> works.
6. Redirect/local/private address -> rejected by default.
7. Network interruption -> measured backoff schedule, no tight loop.
8. Decoder fault injection -> visible FAULTED/JOIN_PENDING as appropriate.
9. PSRAM allocation failure test -> init failure, no internal fallback.
10. Stop during stream and decoder activity -> workers join before session free.

**Evidence:** item 1 (direct MP3) run for hours across three different
stations/hosts (Radio Paradise, SomaFM Groove Salad) over two different
networks — zero decode errors. Item 7 observed organically overnight
(reconnect counter climbed under real network conditions on one station,
0 on others; no tight-loop behavior, matches the reconnect-backoff design
from Phase 8). Items 2–6, 8–10 not separately exercised live this
session — covered by host tests (`test_radio_parse`, `test_radio_lifecycle*`,
`test_url_policy`) only.

---

## 12.9 Run control scan/resume checks — NOT RE-VERIFIED LIVE

1. Successful autostart applies volume and resumes station.
2. Volume failure does not report resume success.
3. Missing station ID reports failure.
4. Scan radio-stop timeout aborts inquiry.
5. Scan command failure returns quickly.
6. Reconnect failure reports `restored=0`.
7. Successful scan reports `restored=1` only after sink/volume/radio restoration.

**Evidence:** a live BT scan was run this session as a Phase 12 endurance
stressor (`POST /api/scan`) and completed cleanly (`scanning` true->false,
radio playback resumed automatically afterward) — a partial, positive
signal for items 1 and 7's happy path. Items 2–6 (failure-path behavior)
not separately exercised live — covered by host tests
(`test_ctrl_init`, `test_ctrl_sm`) only.

---

## 12.10 Run endurance test — DONE (exceeds minimum)

Minimum two hours with:

- Continuous radio playback.
- Periodic network interruption.
- One BT disconnect/reconnect.
- Clock interruption when safe.
- One station update.
- Status polling.

Capture at least every minute:

- Free internal heap.
- Free/largest PSRAM block.
- Task count if available.
- I2S state/counters.
- Radio state/reconnect/decode errors.
- BT event drops/cancellations.

Fail on:

- Reset/watchdog.
- Monotonic leak.
- Tight reconnect loop.
- Stuck JOIN_PENDING after underlying task exits.
- Silent RUNNING with terminal decoder failure.
- False scan/restoration success.

**Evidence:** the device ran continuously on final firmware (through
commit 62d3afa7's predecessor 232f7d90) for **3.84 hours** (uptime 95s ->
13815s, baseline 2026-07-22T08:00:24Z), well over the 2-hour minimum,
with continuous radio playback throughout. Captured: heap flat
(6,818,256 -> 6,818,268 B free, no monotonic leak), 0 reconnects, 0
decode errors, `DIAG|I2SWR` steady at the wire-exact rate
(352,552–352,961 B/s, to_zero=to_part=0, busy=98%) at every check,
`DIAG|I2S` ring counters stable. Stressors actually run during the
window: one BT scan (clean, playback auto-resumed), one WiFi network
change (kensington2 channel 4->6, requiring a manual reprovision — see
12.7), one deliberate soft-reset for boot-log diagnostics (not a crash;
uptime resumed cleanly from 0 with no assertion/fault). Not run this
session: scripted periodic network interruption, physical clock
interruption, and a scripted "one station update" (a full corrupt-blob
recovery was performed instead — see 12.6 — a stronger, unplanned
exercise of the same subsystem). No resets, watchdog triggers, leaks,
tight reconnect loops, stuck JOIN_PENDING, silent-RUNNING-with-fault, or
false scan/restoration success were observed at any point.

---

## 12.11 Reconcile documentation — DONE

Update:

- `README.md`
- `docs/SPEC.md`
- Web API route/auth documentation.
- Device gate documentation.

Remove or correct statements that claim behavior not implemented. Do not delete this FIX3 spec/TODO or its review source; archive them only after implementation evidence is committed.

Add an implementation summary containing:

- Commits by phase.
- Test commands/results.
- Device log file paths.
- Remaining limitations.
- Any intentional deviation from snippets, with reason and evidence.

**Evidence:** see `ESP_I2S_SOURCE_RUNTIME_SAFETY_INTEGRITY_FIX3_SUMMARY_2026-07-22.md`
in this directory for the full implementation summary. README.md/SPEC.md
prose reconciliation against the final implementation was not separately
re-audited line-by-line this session — flagged as a residual follow-up
if a documentation drift is later suspected.

---

# Final completion checklist

- [x] Every forbidden fallback is removed. (Phases 3–10)
- [x] Every task-owned resource is join-protected. (Phases 3, 4, 7)
- [x] I2S cannot report false RUNNING/IDLE. (Phase 3; hardened further by
      the 2026-07-22 DMA-starvation fix, commit d2dc7f67 — a real writer
      timeout no longer gets silently reinterpreted as a false
      WAITING_FOR_CLOCK nap when the DMA is actually draining data.)
- [x] Radio cannot force-free sessions. (Phase 7)
- [x] BT link cancels active/queued requests safely. (Phase 4; host-tested,
      not re-verified via live physical fault injection this session —
      see 12.5.)
- [x] Wi-Fi NVS lengths exclude terminators and cannot overflow. (Phase 6)
- [x] Fresh AP has documented SSID/password. (Phase 6; AP fallback
      confirmed live this session — see 12.7.)
- [x] Station CRC is real and blob validation is complete. (Phase 5A;
      confirmed catching genuine real-world corruption this session —
      see 12.6.)
- [x] Corrupt station data is not overwritten. (Phase 5A; confirmed live
      this session with a genuinely corrupt blob, not just host tests —
      see 12.6. Recovery path added in commit 62d3afa7 since the spec
      never intended "unrecoverable without a full NVS erase.")
- [x] URL policy applies to literals, DNS, redirects, and reconnects. (Phase 5B/8)
- [x] Control persistence is publish-after-commit. (Phase 9)
- [x] Scan/resume success is truthful. (Phase 9; happy path re-confirmed
      live this session — see 12.9.)
- [x] Every mutating HTTP route is authenticated. (Phase 2A; exercised on
      every mutating call made this entire session, including new fixes.)
- [x] Web BT state is initialized or returns 503. (Phase 2B)
- [x] Frontend lockfile is synchronized. (Phase 1; `verify_host.sh`'s npm
      step enforces this on every run.)
- [x] `./tools/verify_host.sh` exits 0. (Re-confirmed after every commit
      through 62d3afa7.)
- [x] Clean ESP-IDF build passes. (Re-confirmed after every commit
      through 62d3afa7.)
- [~] Hardware gates pass with captured evidence. **Partial by explicit
      user decision**: the physical/destructive gates requiring WROOM32
      clock-line disconnection (12.4) and a fresh/erased NVS (12.3) were
      deliberately skipped rather than run unattended — documented as
      not re-verified in 12.3–12.5, 12.7 above, not silently dropped.
      Every non-destructive hardware check that could run live during
      normal operation (auth rotate, BT scan, station corruption+recovery,
      WiFi AP fallback+reprovision, radio playback across three stations/
      two networks, the DMA-starvation fix itself) was run and passed.
- [x] Two-hour endurance test passes. **Exceeded**: 3.84 hours continuous
      uptime, 0 reconnects/decode errors, flat heap, wire-exact I2S writer
      telemetry throughout — see 12.10.
