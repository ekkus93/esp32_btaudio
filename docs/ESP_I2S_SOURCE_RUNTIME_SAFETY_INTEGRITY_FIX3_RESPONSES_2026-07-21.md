# Responses: ESP_I2S_SOURCE_RUNTIME_SAFETY_INTEGRITY_FIX3 (SPEC + TODO, 2026-07-21)

Covers the `/spec-todo` review of `ESP_I2S_SOURCE_RUNTIME_SAFETY_INTEGRITY_FIX3_SPEC_2026-07-21.md` and
`ESP_I2S_SOURCE_RUNTIME_SAFETY_INTEGRITY_FIX3_TODO_2026-07-21.md` (currently sitting, mis-nested, at
`docs/esp_i2s_source/docs/`). Fill in the `A:` line under each question and share this file back.

---

1. Q: The handoff package's files are at the wrong path (`docs/esp_i2s_source/docs/...` instead of
   `esp_i2s_source/docs/...`, per the manifest's own installation instructions). Should I move them into
   `esp_i2s_source/docs/` (and `esp_i2s_source/docs/review-source/`) before anything else, and
   archive/delete the zip + manifest once moved?

   A: Yes. Move the documents before implementation begins so they exist at the canonical paths named by the manifest:

- `esp_i2s_source/docs/ESP_I2S_SOURCE_RUNTIME_SAFETY_INTEGRITY_FIX3_SPEC_2026-07-21.md`
- `esp_i2s_source/docs/ESP_I2S_SOURCE_RUNTIME_SAFETY_INTEGRITY_FIX3_TODO_2026-07-21.md`
- `esp_i2s_source/docs/review-source/ESP_I2S_SOURCE_RUNTIME_SAFETY_INTEGRITY_FIX3_CODE_REVIEW_2026-07-21.md`

After confirming all three files exist there and match the handoff copies, remove the duplicate mis-nested `docs/esp_i2s_source/` tree. Do not commit the handoff ZIP into the source repository. Keep the ZIP and manifest outside the repository until the move is verified; they may then be archived elsewhere or deleted from the working tree. Keep the review-source document in the repository because the SPEC and TODO reference it.

2. Q: Do you want FIX3 implemented as one continuous multi-session effort (like the prior RH-*
   reliability pass), or phase-by-phase with your check-in between phases?

   A: Implement FIX3 as one continuous multi-session effort, but execute and commit it phase-by-phase in the documented order. Do not wait for my approval between ordinary phases. Each phase must have focused commits, regression tests, strict host verification, and a clean ESP-IDF build when it changes device code. Stop only for a genuine blocker that cannot be resolved from the repository or specification, such as unavailable credentials, an irreversible product decision, or required physical-hardware action. Hardware-only gates may remain pending as described in answer 9 without stopping the software work.

3. Q: TODO Phase 2 bundles two largely independent concerns — bearer-auth/token lifecycle, and the
   BT-web-submodule init/deinit — under one phase with one recommended commit
   (`fix(web): enforce bearer auth and initialize BT web state`). Should these be split into two
   commits/phases, or is bundling intentional?

   A: Split this into two sub-phases and two commits:

- **Phase 2A — authentication and token lifecycle:** token encoding/storage/rotation, fail-closed initialization, exact bearer validation, route-dispatch enforcement, and authentication tests. Commit: `fix(web): enforce bearer authentication and token lifecycle`.
- **Phase 2B — BT web-module lifecycle:** `web_ui_bt_init()`, availability guards, subscriptions, helper-task join, deinitialization, startup unwind, and 503 behavior. Commit: `fix(web): initialize and safely tear down BT web state`.

Run the relevant tests after each sub-phase. Phase 2 is accepted only after both pass the combined acceptance checks. Implement authentication first so no newly reachable handler is exposed without authorization.

4. Q: TODO 2.7 introduces the `AUTH ROTATE` console command as a MUST-have feature, but the test list
   in 2.8 has no explicit test for rotation itself (rotation success, old-token-invalidated-after-rotate).
   Should an explicit test for this be added to Phase 2?

   A: Yes. Add explicit token-rotation tests to Phase 2A. At minimum, test:

1. Successful rotation creates a new valid 64-character hexadecimal token and persists it before publication.
2. The old token is rejected immediately after successful publication.
3. The new token is accepted immediately and after auth-module reinitialization/reload.
4. NVS open, write, commit, or required read-back failure leaves the old token active and valid.
5. Failed rotation does not print or expose the uncommitted candidate token.
6. Successful rotation emits the bootstrap token exactly once and then `AUTH|TOKEN_ROTATED`.
7. `AUTH ROTATE` is handled locally and is never forwarded to the WROOM32.
8. No unauthenticated HTTP token-rotation route exists.
9. Concurrent authorization checks see either the old committed token before publication or the new committed token afterward, never a partial token or unauthenticated gap.

These are required Phase 2A acceptance tests.

5. Q: TODO Phase 5 covers two conceptually distinct concerns (station persistence/CRC/migration, and
   network SSRF/URL policy) under one phase header with one "Phase acceptance" gate, but the recommended
   commit sequence splits them into separate `fix(stations)` and `fix(url)` commits. Should Phase 5 be
   split into two sub-phases/gates to match, or kept as a single phase with one acceptance gate covering
   both commits?

   A: Split Phase 5 into two explicit sub-phases with separate gates and commits:

- **Phase 5A — station persistence integrity:** CRC-32, blob construction/validation, load-result classification, V1 migration, non-destructive corruption handling, persist-before-publish mutations, and persistence tests. Commit: `fix(stations): repair CRC validation and non-destructive recovery`.
- **Phase 5B — URL and destination security:** strict URL parsing, literal-IP policy, DNS-result validation, redirect validation, private/link-local/loopback rejection, playback revalidation, and SSRF tests. Commit: `fix(url): enforce stream destination policy`.

Phase 5A may use a pure host-testable URL syntax validator for stored blobs; it must not depend on DNS or live networking. Phase 5B adds runtime destination checks and must revalidate every initial URL, playlist-resolved URL, redirect target, and reconnect target. Keep later phase numbers unchanged.

6. Q: Task 9.3 (persist control candidates before publish) explicitly leaves two unresolved strategies
   open — "serialize the complete update with a dedicated update mutex" **or** "add a generation counter
   and compare/retry" — without stating a preference. Which should the implementation use, or should the
   implementer choose and document the rationale?

   A: Use the dedicated update-mutex design. Do not use generation-counter compare/retry for FIX3.

Add `s_update_mtx` and require every persistent control mutation, including `ctrl_set_sink()` and `ctrl_note_station()`, to use this order:

1. Acquire `s_update_mtx`.
2. Acquire `s_mtx` briefly and copy `s_cfg` to a local candidate.
3. Release `s_mtx`.
4. Validate and modify the local candidate.
5. Persist and commit the candidate, including read-back validation if required by the final schema.
6. Acquire `s_mtx` briefly and publish the complete candidate.
7. Release `s_mtx`.
8. Release `s_update_mtx`.

Never hold `s_mtx` during NVS operations. All code must use the same lock order—`s_update_mtx` before `s_mtx`. Create both mutexes during `ctrl_init()` using local candidates and unwind both if initialization fails. Do not delete them until all control/helper tasks acknowledge exit. Add failure-injection and concurrent-setter tests proving persistence failure preserves the old runtime configuration and two setters cannot lose updates.

7. Q: Task 9.4 (repair old control migration) explicitly leaves two unresolved designs open — "move V0
   migration into a coordinator that has the migrated station store" **or** "load V0 into a temporary
   structure with `migration_pending=true`, then finalize after stations initialize" — without stating a
   preference. This is the highest-stakes open choice since it affects cross-component data-migration
   correctness. Which design should be used?

   A: Use the coordinator design. Do not introduce a long-lived `migration_pending` control configuration.

Because stations initialize before control, station initialization must expose an authoritative legacy-index mapping for the current boot. Add a read-only API such as:

```c
esp_err_t stations_resolve_legacy_index(int16_t legacy_index,
                                        uint32_t *out_station_id);
```

Required behavior:

- A negative legacy index maps to `CTRL_LAST_STATION_NONE`.
- For a valid V1 station migration, return the stable ID assigned to that exact legacy index.
- For a valid V2 store, resolve the current index to its stored stable ID.
- Index `0` therefore maps to stable ID `1` for a normal V1 migration.
- Out-of-range indices return a specific migration error.
- If stations are unavailable, corrupt, or only default-seeded while a legacy control blob exists, do not pretend those defaults are the legacy list. Fail migration visibly and leave the legacy control blob untouched.

Refactor `ctrl_cfg_load()` to distinguish current-format config, raw V0 config, absent config, corrupt config, and NVS failure. It must return raw V0 fields without casting the legacy index to an ID. `ctrl_init()` or a small boot coordinator called after `stations_init()` then resolves the index, builds a complete current-format candidate, persists/commits and validates it, and only then publishes it. On mapping or persistence failure, preserve V0 NVS data, report a visible migration/degraded error, and use safe runtime defaults without claiming success.

8. Q: Task 1.1 (regenerate the frontend lockfile) requires `npm ci`/`npm install` from a clean `web/`,
   which needs npm registry network access. Does this environment/session have that access, or does this
   step need to happen somewhere else before implementation starts here?

   A: Do not assume the Claude Code environment has npm-registry access. Attempt the documented commands there first and capture exact Node/npm versions and error output. Do not hand-edit `package-lock.json`.

If registry access or the required cache is unavailable:

1. Mark only Task 1.1 as `BLOCKED_NETWORK`.
2. Leave `package.json` and `package-lock.json` unchanged.
3. Continue the independent firmware phases.
4. Regenerate and verify the lockfile later in a network-enabled environment before the frontend phase and final FIX3 verification.
5. Do not claim Phase 1 or FIX3 complete until `npm install`, `npm ci`, `npm run build`, and `npm test` pass from a clean `web/` directory.

A network restriction is a documented verification blocker, not a reason to stall all firmware implementation.

9. Q: SPEC §15.4 (15 hardware-gate items) and §15.5 (a 2-hour endurance test) require the physical
   WROOM32+S3 pair and can't be verified by an agent alone. How much of this hardware-gated acceptance do
   you want attempted per-phase on real hardware vs. deferred to one final hardware session?

   A: Use targeted hardware smoke tests after phases whose behavior can only be validated on the S3/WROOM32 pair, when hardware is available:

- Phase 2: unauthenticated/authenticated routes and `/api/bt` with WROOM32 absent.
- Phase 3: boot without clocks, clock application, and clock removal/reapplication.
- Phase 4: WROOM32 absence, link stop/restart, and request cancellation behavior.
- Phase 5A: controlled station-corruption injection and preservation of the original blob.
- Phase 7: first-boot AP identity/credentials and Wi-Fi failure reporting.
- Phase 8: real playback, playlist handling, network interruption/backoff, and visible decoder fault.
- Phase 9: scan/resume and truthful restoration diagnostics.

Do not run the full two-hour endurance test after every phase. Run the complete 15-item hardware gate and one uninterrupted two-hour endurance campaign after all software phases are integrated.

When hardware is unavailable to Claude Code, it must prepare/update exact test commands or scripts and expected diagnostic markers, mark checks `PENDING_HARDWARE`, and continue. A phase may be software-complete with pending hardware evidence, but FIX3 as a whole is not complete until the final hardware gate and endurance logs are captured.

10. Q: SPEC §10.7 / TODO 8.1 require resetting the reconnect-backoff counter after a "stable" connection,
    but never quantify "stable" (no duration/byte-count threshold given anywhere in the document). Should
    I pick a default (e.g. 10 seconds of continuous streaming) and document it, or do you want to specify
    the exact threshold?

   A: Use an exact stable-connection threshold.

A connection becomes stable only after **both** conditions are true for the same connection attempt:

1. At least **10,000 ms** have elapsed since the first validated audio-payload byte was received.
2. At least **32 KiB** of validated audio payload—not HTTP headers, playlist text, or discarded bytes—has been accepted into the compressed-audio ring.

During that interval there must be no transport error, rejected HTTP status, URL-policy failure, redirect failure, playlist-resolution failure, reconnect, session fault, or decoder-requested stop. Once both conditions are met, reset the reconnect-attempt/backoff counter exactly once for that connection. An HTTP 2xx response, socket connection, worker STARTED event, or first byte alone is insufficient. If the connection fails before stability, retain/increment the retry progression.

Add host tests with a fake clock and byte counter proving: 9,999 ms is insufficient; fewer than 32 KiB is insufficient; both thresholds reset the counter; a pre-threshold failure does not reset it; and a prior STARTED/READY bit cannot collapse the next backoff wait.

---

Fill in the `A:` line under each question above, then share this file back (or paste the answers) when
ready — implementation will not begin until then.
