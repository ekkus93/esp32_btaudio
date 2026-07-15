# Responses: `ESP_I2S_SOURCE_FIX_SPEC_V2_2026-07-15.md` / `ESP_I2S_SOURCE_FULL_CODE_REVIEW_2026-07-15.md`

These answers are **normative errata** for the current specification and TODO. Where an answer below conflicts with the existing specification, TODO, or review wording, this file controls until the specification and TODO are revised.

---

### 1

Q: Which boot order is normative — §3.1's ("Normal boot": I2S init/start before UART link, radio, stations, and Wi-Fi) or §5.1's ("Required order": bt_link_init → radio_init → stations_init → wifi_mgr_init → i2s_out_init/start)? The two sections directly contradict each other and this blocks the first implementation step (fixing boot order).

A: **Neither existing list is normative exactly as written. Replace both lists with the following exact dependency-safe order:**

```text
1.  NVS initialization/recovery
2.  Boot diagnostics and boot-ID creation
3.  i2s_out_init
4.  i2s_out_start
    - MUST return promptly as RUNNING or WAITING_FOR_CLOCK
    - MUST NOT require the WROOM32 to be present
5.  bt_link_init
6.  radio_init
7.  stations_init, including any persistence migration
8.  ctrl_init
9.  audio_out_start
    - start only after both i2s_out and radio interfaces are initialized
10. wifi_mgr_init
11. console_start
12. web_ui_start, as an optional/degraded component
13. ctrl_start
14. emit BOOT_COMPLETE
15. launch the bounded read-only WROOM health probe asynchronously
16. run low-rate diagnostics outside app_main's critical startup path
```

The product-level intent of §3.1 is correct: initialize and start the local audio path early and do not make it depend on WROOM clocks or Wi-Fi. However, `audio_out_task` must not run before the radio interface it calls has been initialized. Section §5.1 must be rewritten to match the sequence above.

Every singleton initializer and task start must still occur exactly once. Optional failures must be recorded in the boot result and must not cause a duplicate retry from `app_main()`.

---

### 2

Q: Should release configuration treat blocking loopback/link-local/private-network radio URLs (§11.5) as a MUST, matching the code review's P1/security classification (STATION-008, called an "SSRF-style primitive"), rather than the spec's current SHOULD?

A: **Yes. This is a MUST for release builds.**

Release builds must reject radio destinations that resolve to loopback, link-local, private, unique-local, multicast, unspecified, or broadcast addresses. Validation must apply to:

- Literal IPv4 and IPv6 addresses.
- Every DNS result before a connection is attempted.
- Every redirect target.
- Every reconnect or playlist-resolved URL.

Do not validate only the original hostname string; that would remain vulnerable to redirects and DNS rebinding.

Local streams may be enabled only through an explicit authenticated setting such as:

```text
CONFIG_ESP_I2S_SOURCE_ALLOW_LOCAL_STREAMS=n
```

The default must be `n`. When enabled, status must expose `local_streams_allowed=true`, the UI must show a security warning, and authentication must still be required. There must be no automatic fallback that retries a blocked destination with the protection disabled.

---

### 3

Q: In §9.3 (BT link request lifetime), what owns/frees a `bt_link_request_t` if the worker never accepts the queued item (enqueue failure or timeout before acceptance, i.e. before `refs = 2` is established)?

A: The specification's phrase "only when the worker has accepted" is too ambiguous. **Acceptance means `xQueueSend()` returned `pdTRUE`, not that the worker has already dequeued or started the request.**

Use this exact ownership protocol:

1. `request_create()` creates the request with `refs = 1`; this is the caller reference.
2. The caller reserves the worker reference by calling `request_retain()` **before** `xQueueSend()`.
3. If `xQueueSend()` fails or times out, the queue never accepted the request. The caller releases both references and the object is freed synchronously.
4. If `xQueueSend()` succeeds, ownership of the reserved worker reference transfers to the worker. The caller must never release that reference.
5. A caller timeout after successful enqueue releases only the caller reference. The worker later completes or cancels the request and releases the worker reference.
6. The final release deletes the semaphore and frees the request.

Required pattern:

```c
bt_link_request_t *req = request_create(cmd); /* refs = 1: caller */
if (req == NULL) {
    return ESP_ERR_NO_MEM;
}

request_retain(req); /* refs = 2: reserve worker ownership */

if (xQueueSend(s_cmd_queue, &req, enqueue_timeout) != pdTRUE) {
    request_release(req); /* release untransferred worker reservation */
    request_release(req); /* release caller */
    return ESP_ERR_TIMEOUT;
}

/* Queue now owns the worker reference. */
```

There must never be a successfully queued pointer with only the caller reference, because the worker could dequeue and finish before the caller increments the count.

---

### 4

Q: §10.2 requires "64-character hex PSK if supported by the target API" — how should the implementation detect whether the target API supports hex PSK, and what should happen at the API boundary if it doesn't (reject the request, or fall back to WPA-passphrase-only validation)?

A: **Do not attempt runtime feature probing and do not infer semantic support merely from the size of `wifi_sta_config_t.password`. Make support an explicit build-time capability.**

Add a Kconfig capability such as:

```text
CONFIG_ESP_I2S_SOURCE_STA_HEX_PSK=y
```

Enable it only for an ESP-IDF version/target combination that has a compile test and a device association test proving that a 64-character hexadecimal STA PSK works. The project's pinned target can enable it after that test exists.

API validation must be:

```text
length 0:       accepted as an open network
length 8..63:   accepted as a WPA/WPA2/WPA3 passphrase
length 64:      accepted only when every character is hexadecimal and
                CONFIG_ESP_I2S_SOURCE_STA_HEX_PSK=y
all other:      rejected
```

If a valid 64-character hexadecimal PSK is supplied while support is disabled, reject it with `ESP_ERR_NOT_SUPPORTED`; the HTTP layer should return HTTP 422 with a stable code such as `WIFI_HEX_PSK_UNSUPPORTED`.

A 64-character non-hexadecimal string is invalid and must be rejected as `ESP_ERR_INVALID_ARG`/HTTP 422. **Never truncate it, reinterpret it as a 63-character passphrase, or silently fall back to passphrase-only behavior.**

SoftAP credentials remain open or 8–63 characters. Do not accept a 64-character raw/hex SoftAP key unless SoftAP support is separately documented and device-tested.

---

### 5

Q: §6.7 states the "MUST NOT silently fall back to internal RAM, MUST return ESP_ERR_NO_MEM" rule explicitly for the I2S ring, but §11.9 (radio PCM ring) and the equivalent compressed-buffer requirement don't restate it, even though the code review flags silent PSRAM fallback in both places (I2S-014, RADIO-008) as P1. Should the no-fallback rule be stated once, generically, so it unambiguously covers the I2S ring, PCM ring, and compressed ring?

A: **Yes. Add one generic normative memory-class rule and reference it from all three subsystems.**

Use wording equivalent to:

> Every buffer designated `PSRAM_REQUIRED` must be allocated with the required SPIRAM capability. If the requested capacity cannot be allocated from PSRAM, initialization must return `ESP_ERR_NO_MEM`, publish the failed buffer name and requested size in status, and leave the subsystem in an explicit degraded/fault state. It must not retry from unrestricted/internal heap.

This rule applies at minimum to:

- The 256 KiB I2S source ring.
- The radio compressed-input ring.
- The radio decoded-PCM ring.

Small control objects, task stacks, synchronization objects, and ESP-IDF-required internal/DMA buffers are not covered by this large-buffer rule.

Each buffer requires a host allocation-failure test proving that internal fallback is not attempted. Do not add a development fallback unless the specification is explicitly revised to make it opt-in, visibly reported, capacity-bounded, and excluded from release builds.

---

### 6

Q: §12.3 introduces a new persistence schema (magic/version/CRC/stable IDs) for stations. Existing devices in the field have station data under the old, unversioned schema, which will fail the new checksum/version check and be treated as corrupt (quarantined, in-RAM defaults loaded). Is it acceptable that upgrading wipes existing on-device station data, or does this need an explicit migration path from the old schema?

A: **Upgrading must not wipe valid existing station data. An explicit one-time migration path from the existing `STA1` schema is required.**

Migration requirements:

1. Read the NVS blob size before choosing a parser.
2. If the new magic/version/size match, parse the new schema normally.
3. Otherwise, if the blob has the exact legacy size and `magic == 0x53544131` (`STA1`), parse it with an explicitly declared `stations_blob_v1_t`.
4. Validate the legacy count, every string terminator, every URL, and URL uniqueness before migration.
5. Preserve station order, names, and URLs.
6. Assign stable IDs deterministically. For the first migration, sequential IDs `1..count` are acceptable; initialize `next_id = count + 1` and never reuse an ID after deletion.
7. Migrate the old control configuration's `last_station` index to the corresponding new `last_station_id` in the same upgrade operation.
8. Write the new blob to a new key such as `stations_v2`, commit it, read it back, and validate CRC/content before publishing it in RAM.
9. Do not delete or overwrite the legacy `stations` key during the first release containing migration support. Retain it as rollback evidence.
10. If any migration step fails, continue using the validated legacy data in RAM where possible, report `migration_pending`/the precise error, and leave the legacy key untouched.

If a legacy blob is genuinely invalid, treat it as corrupt: preserve/quarantine it, load safe in-RAM defaults, and do not automatically overwrite the original data.

Add host tests for successful migration, invalid legacy data, persistence failure during migration, read-back verification failure, and `last_station` index-to-ID conversion.

---

### 7

Q: §14.5 requires a "documented physical-presence or first-run flow" for auth-token bootstrap but doesn't specify what that flow actually is. What should it be — e.g., token printed to the serial console on first boot, a reset-button/first-run grace window, something else?

A: Use the following concrete baseline flow:

1. On the first boot with no token in NVS, generate 32 random bytes and store the encoded token in NVS before starting the HTTP server.
2. Print the token **once** to the local USB serial console using a dedicated marker such as:

   ```text
   AUTH|BOOTSTRAP_TOKEN|<token>
   ```

3. Never include the token in `/api/status`, normal logs, crash summaries, mDNS, AP metadata, or any unauthenticated HTTP response.
4. The web UI may load without authentication, but every mutating endpoint remains locked. The UI presents a token-entry screen and sends the token only in the `Authorization` header.
5. There is no unauthenticated first-run grace period for mutations.
6. For recovery/rotation, require the configured physical button to be held continuously for five seconds **while firmware is already running**. The firmware then:
   - Generates and persists a new token transactionally.
   - Invalidates the old token immediately after the new value is committed.
   - Prints the new token once to USB serial.
   - Emits a non-secret `AUTH|TOKEN_ROTATED` marker.
7. If the target board has no usable physical button, token rotation must require a local USB-console command and must not be exposed as an unauthenticated network action.

Document which GPIO/button is used on the actual ESP32-S3 board. Do not rely on holding a boot-strapping button during reset, because that may enter the ROM download path rather than the application.

---

### 8

Q: The code review's executive summary claims 12 P0 findings, but several findings in the body are dual-tagged `P0/P1` (I2S-011, RADIO-007, RADIO-023, RADIO-027, TEST-005, TEST-006, TEST-007, TEST-008, TEST-011) without resolving which severity applies. Should these dual-tagged findings be treated as P0/blocking for planning purposes?

A: **All of them are release-blocking, but the labels should be normalized rather than calling every item a runtime P0. Do not use the executive-summary count of 12 as authoritative.**

Classify them as follows:

Runtime P0 defects:

- `I2S-011` — invalid/uninitialized NVS-handle cleanup can crash or corrupt state.
- `RADIO-007` — zero capacity can reach modulo/division operations.
- `RADIO-023` — invalid/uninitialized NVS-handle cleanup can crash or corrupt state.
- `RADIO-027` — unchecked decoder consumption can cause out-of-bounds state/memory handling.

P1 test-system defects that are nevertheless mandatory release gates:

- `TEST-005` — BT lifetime tests miss the dangerous interleaving.
- `TEST-006` — resampler tests do not verify signal correctness.
- `TEST-007` — no boot-sequence regression test.
- `TEST-008` — no web asynchronous-lifetime test.
- `TEST-011` — device Wi-Fi test does not test a real connection.

For planning, schedule every item above before release. The test findings should be implemented in the same phase as the runtime defect they are intended to prevent from recurring.

---

### 9

Q: No TODO file currently exists for this spec (only the code review, which isn't sized or ordered as discrete tasks). Do you want a proper sized/dependency-ordered TODO file generated before implementation starts — using the review's §8 "Recommended repair order" as a skeleton — or do you plan to work directly off the code review's finding list?

A: A dependency-ordered, code-heavy TODO already exists:

```text
ESP_I2S_SOURCE_IMPLEMENTATION_TODO_2026-07-15.md
```

It contains 12 phases, discrete tasks, code snippets, acceptance criteria, hardware gates, and a final merge checklist. **Use that TODO as the implementation plan. Do not work directly from the review finding list and do not generate a competing TODO.**

Before implementation begins:

1. Put the specification, full review, implementation TODO, and this answered-response file together in the repository/workspace.
2. Apply the errata in this response to the relevant TODO tasks, especially boot order, URL security, BT request ownership, station migration, auth bootstrap, and additional tests.
3. Keep task completion atomic: one TODO item or tightly coupled sub-item per commit where practical.

The review remains the evidence/findings catalog. The specification defines required behavior. The TODO defines execution order.

---

### 10

Q: A few findings don't have an obvious corresponding host test in spec §17.2 — specifically MAIN-007/008/009 (PCNT telemetry timing/error-checking) and BTLINK-008 (head-of-line blocking from the global send mutex). Are these intentionally deferred as P1/P2 robustness items without dedicated tests, or should §17 be extended to cover them?

A: **Extend §17. They are not intentionally untested.** Their severity may remain P1/P2, but the fixes require regression coverage before release.

Add the following tests.

#### Clock/PCNT diagnostics

Extract clock measurement and scheduling policy behind injectable PCNT/timer operations, then add host tests for:

- Every PCNT setup/start/read/stop/cleanup failure being propagated and reported.
- Cleanup occurring only for handles that were successfully created.
- No invalid frequency being published as a successful measurement.
- Measurement cadence being no more frequent than the configured low-rate period.
- Diagnostic work not blocking boot or the main control path.
- Correct handling of counter limits/accumulation or a measurement window chosen to avoid overflow.

Add a device test that measures a known WROOM clock, verifies BCLK and WS are within tolerance, verifies the ratio is approximately 64, and verifies no PCNT error marker occurs.

A simple implementation can avoid BCLK counter wrap pressure by using a short BCLK measurement window and a longer WS window rather than using one duration for both.

#### BT head-of-line behavior

A single UART protocol worker may serialize wire commands, but public callers must not hold a global send mutex while waiting for another request's full UART timeout.

Add a pthread/FreeRTOS-mock concurrency test in which:

1. Request A is accepted and deliberately stalls until its protocol timeout.
2. Request B is issued concurrently.
3. B either enqueues within the documented enqueue bound or receives a precise `QUEUE_FULL`/timeout result within that bound.
4. B's caller is not blocked on a global mutex for A's complete protocol duration.
5. Cached status reads remain immediate while A is stalled.
6. Both request objects are freed exactly once after completion/timeout.

Also add queue-depth and worst-case latency limits to the public contract so the test has explicit bounds.

---

### 11

Q: The spec's normative MUST/SHOULD/MAY language and the code review's P0/P1/P2 severity tags don't map 1:1 in at least one place (SSRF blocking, see item 2). When they conflict, which document is authoritative for release-gating decisions?

A: **The normative specification is authoritative for product behavior and release gates.** The review explains evidence and severity; it does not override normative requirements. The TODO controls implementation order, not final behavior.

Until the documents are revised, use this precedence order:

```text
1. This answered-response/errata file
2. The latest revised repair specification
3. The implementation TODO
4. The full code review
```

After these answers are incorporated into a new specification revision, that revised specification becomes the single normative authority.

If an unresolved conflict remains, apply the stricter requirement and treat release as blocked until the specification is corrected. A P1 review label does not weaken a spec MUST, and a spec SHOULD must be promoted to MUST when this errata explicitly says so.

---

### 12

Q: Several Definition-of-Done items (§19) and repair-order steps (§8, steps 3/6/12) require the physical S3 + WROOM32 hardware rig to close out (tone/radio audible end-to-end, BCLK/WS ratio in tolerance, soak tests). What's the current hardware access/timeline, so the repair order can be planned around when device validation is actually possible?

A: Current known access is:

- The user has the ESP32-S3/WROOM32 project hardware and has already flashed the S3 and collected `idf.py monitor` logs.
- Claude Code must assume it has **no direct physical hardware access** unless the user explicitly provides a remote flashing/serial workflow.
- No fixed hardware-response timeline should be invented.

Implementation should proceed host-first, with explicit user-run hardware checkpoints rather than blocking all work on continuous device access:

#### Hardware checkpoint 1 — after boot/I2S startup fixes

User flashes the S3 and returns the last complete boot log. Required evidence:

- No duplicate-netif assertion.
- Every initializer occurs once.
- `BOOT_COMPLETE` appears.
- I2S reports `RUNNING` or `WAITING_FOR_CLOCK` without hanging.
- Device remains controllable with WROOM32 absent.

#### Hardware checkpoint 2 — after I2S and BT lifetime phases

With WROOM32 connected:

- Read-only UART probe succeeds or returns a bounded explicit failure.
- WS is approximately 44.1 kHz.
- BCLK is approximately 2.8224 MHz.
- BCLK/WS is approximately 64.
- I2S accepted-byte counters increase.
- Tone is audible end to end.

#### Hardware checkpoint 3 — after radio/resampler/lifecycle work

- MP3 station is audible and stable.
- AAC station is audible and stable.
- Stop/restart and Wi-Fi interruption recover without crash or monotonic heap loss.

#### Final hardware gate

Run the defined boot-cycle, play/stop, reconnect, and two-hour stream soaks. These tasks may be marked `REQUIRES_USER_HARDWARE` or `BLOCKED_HARDWARE`, but they must never be marked complete based only on host tests or successful compilation.

Host implementation phases can continue between checkpoints. A release branch cannot be declared done until the user supplies the required hardware evidence.

---

Implementation planning may proceed using these answers as the current normative errata.
