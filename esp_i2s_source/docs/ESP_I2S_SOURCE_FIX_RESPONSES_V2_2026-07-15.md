# Responses: ESP_I2S_SOURCE_FIX_SPEC_V2_2026-07-15.md / ESP_I2S_SOURCE_FULL_CODE_REVIEW_2026-07-15.md

Fill in the `A:` line under each question, then share this file back (or paste the answers) before implementation begins.

---

### 1
Q: Which boot order is normative — §3.1's ("Normal boot": I2S init/start before UART link, radio, stations, and Wi-Fi) or §5.1's ("Required order": bt_link_init → radio_init → stations_init → wifi_mgr_init → i2s_out_init/start)? The two sections directly contradict each other and this blocks the first implementation step (fixing boot order).
A:

---

### 2
Q: Should release configuration treat blocking loopback/link-local/private-network radio URLs (§11.5) as a MUST, matching the code review's P1/security classification (STATION-008, called an "SSRF-style primitive"), rather than the spec's current SHOULD?
A:

---

### 3
Q: In §9.3 (BT link request lifetime), what owns/frees a `bt_link_request_t` if the worker never accepts the queued item (enqueue failure or timeout before acceptance, i.e. before `refs = 2` is established)?
A:

---

### 4
Q: §10.2 requires "64-character hex PSK if supported by the target API" — how should the implementation detect whether the target API supports hex PSK, and what should happen at the API boundary if it doesn't (reject the request, or fall back to WPA-passphrase-only validation)?
A:

---

### 5
Q: §6.7 states the "MUST NOT silently fall back to internal RAM, MUST return ESP_ERR_NO_MEM" rule explicitly for the I2S ring, but §11.9 (radio PCM ring) and the equivalent compressed-buffer requirement don't restate it, even though the code review flags silent PSRAM fallback in both places (I2S-014, RADIO-008) as P1. Should the no-fallback rule be stated once, generically, so it unambiguously covers the I2S ring, PCM ring, and compressed ring?
A:

---

### 6
Q: §12.3 introduces a new persistence schema (magic/version/CRC/stable IDs) for stations. Existing devices in the field have station data under the old, unversioned schema, which will fail the new checksum/version check and be treated as corrupt (quarantined, in-RAM defaults loaded). Is it acceptable that upgrading wipes existing on-device station data, or does this need an explicit migration path from the old schema?
A:

---

### 7
Q: §14.5 requires a "documented physical-presence or first-run flow" for auth-token bootstrap but doesn't specify what that flow actually is. What should it be — e.g., token printed to the serial console on first boot, a reset-button/first-run grace window, something else?
A:

---

### 8
Q: The code review's executive summary claims 12 P0 findings, but several findings in the body are dual-tagged `P0/P1` (I2S-011, RADIO-007, RADIO-023, RADIO-027, TEST-005, TEST-006, TEST-007, TEST-008, TEST-011) without resolving which severity applies. Should these dual-tagged findings be treated as P0/blocking for planning purposes?
A:

---

### 9
Q: No TODO file currently exists for this spec (only the code review, which isn't sized or ordered as discrete tasks). Do you want a proper sized/dependency-ordered TODO file generated before implementation starts — using the review's §8 "Recommended repair order" as a skeleton — or do you plan to work directly off the code review's finding list?
A:

---

### 10
Q: A few findings don't have an obvious corresponding host test in spec §17.2 — specifically MAIN-007/008/009 (PCNT telemetry timing/error-checking) and BTLINK-008 (head-of-line blocking from the global send mutex). Are these intentionally deferred as P1/P2 robustness items without dedicated tests, or should §17 be extended to cover them?
A:

---

### 11
Q: The spec's normative MUST/SHOULD/MAY language and the code review's P0/P1/P2 severity tags don't map 1:1 in at least one place (SSRF blocking, see item 2). When they conflict, which document is authoritative for release-gating decisions?
A:

---

### 12
Q: Several Definition-of-Done items (§19) and repair-order steps (§8, steps 3/6/12) require the physical S3 + WROOM32 hardware rig to close out (tone/radio audible end-to-end, BCLK/WS ratio in tolerance, soak tests). What's the current hardware access/timeline, so the repair order can be planned around when device validation is actually possible?
A:

---

Once filled in, share this file back (or paste the answers) and implementation planning can proceed.
