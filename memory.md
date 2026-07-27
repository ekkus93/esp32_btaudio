<!-- Entries older than 2026-07-20 (1 week) were moved to memory_archive.md on 2026-07-27. See that file for full history back to 2025-01-13. Note: this window intentionally is not a contiguous date range — entries are kept/archived by actual date regardless of position in the file, so a few older entries physically later in the file (e.g. the 2026-07-21/07-22 FIX3 block) remain here because they fall within the last week, while some entries earlier in the file's byte order were older and got archived. -->

## 2026-07-27T22:07:57Z - Claude Sonnet 5 - narrowed memory.md from a 3-month to a 1-week rolling window

- User: memory.md (2835 lines) was "still kind of big" -- asked to keep only the
  last month's entries here, archiving the rest. Checked actual dates first:
  every entry already fell within 2026-07-03..2026-07-27 (24 days), so a
  literal 1-month cutoff wouldn't have moved anything. Flagged this and asked
  what to actually use; user chose "last week" instead.
- The file's entries are NOT globally sorted by date -- it's a patchwork of
  mostly-newest-first blocks from different sessions, some internally in
  forward-chronological order (e.g. the 2026-07-22 FIX3 marathon, appended
  entry-by-entry during one continuous session). So "keep last week" was not
  a single prefix cut: extracted the one contiguous chunk that WAS entirely
  older than 2026-07-20 (85 entries, 2026-07-03 through 2026-07-17, lines
  799-2157 of the old file) and moved it out, leaving the newer content on
  both sides of that chunk (07-25..07-27 above it, 07-21..07-22 FIX3 block
  below it) in place.
- Prepended the extracted chunk to memory_archive.md, ahead of its existing
  2026-02-12-and-older content (it's newer), matching that file's own
  documented "same relative order, newest first" convention. Updated both
  files' banner comments to describe the new split and explain the
  non-contiguous window.
- Verified zero data loss: total line count identical before/after
  (27,564 across both files); entry counts split exactly 42 kept + 85
  archived = 127 original total.
- memory.md now 1,476 lines (was 2,835); memory_archive.md now 26,088 lines.

## 2026-07-27T22:00:31Z - Claude Sonnet 5 - retagged v0.3.0 again; learned moving a released tag orphans its GitHub Release

- User asked again to retag HEAD as v0.3.0, now that the dead-code/build-
  cruft cleanup landed (real code+CMake change, not just docs, since the
  last retag at 98680b30). Deleted the old tag (local+origin), recreated it
  as an annotated tag on the new HEAD (ed0442f7), appending a new "Release
  housekeeping" section to the tag message covering CHANGELOG.md, the
  GitHub Releases backfill, and the components/ cleanup. `git describe
  --tags` confirmed exactly `v0.3.0` again.
- **Important lesson**: `gh release edit v0.3.0 --notes-file ...` after the
  tag move did NOT just update the notes -- it flipped the existing v0.3.0
  Release into a **draft** pointed at a synthetic `untagged-<hash>` ref,
  and `v0.2.0` silently became "Latest" in its place. Root cause: a GitHub
  Release is bound to the tag's underlying git object at creation time;
  deleting that tag (even to recreate one with the identical name)
  orphans the Release from it. `gh release edit` cannot re-link a release
  to a differently-recreated tag of the same name.
  **Fix**: `gh release delete v0.3.0` then `gh release create v0.3.0
  --latest=true` fresh, rather than editing. Takeaway for next time: if a
  tag needs to move AFTER a GitHub Release already exists for it, delete
  and recreate the Release too -- don't just `gh release edit`.
- Verified via `gh release view v0.3.0`: isDraft=false, targetCommitish
  master, Latest flag correctly back on v0.3.0.

## 2026-07-27T21:50:20Z - Claude Sonnet 5 - removed root-level dead code and stray build cruft

- User asked (via a shared `ls -la` at repo root) to investigate `components/`,
  `unity-app/`, `build/`, `build_clang_tidy/` at the repo root and clean up
  if safe.
- `components/` (tracked in git): pre-dual-ESP32-split legacy code
  (`bt_core/*.c`, `bt_manager/include/bt_api.h`) from before commit
  `b5fe809f` ("splitting up bluetooth and wifi/webserver to two esp32's").
  Confirmed dead: grepped the whole tree, nothing outside this directory
  includes `bt_interface.h`/`bt_registry.h`/`bt_api.h` anymore.
  `esp_bt_audio_source/components/command_interface/CMakeLists.txt` had two
  `if(EXISTS ".../components/bt_manager|bt_core/include")` blocks pointing
  at it, but traced them to be fully redundant even when the paths did
  resolve — `bt_manager` is already in `public_requires`/`REQUIRES`, and
  ESP-IDF's component registry (via `EXTRA_COMPONENT_DIRS` in every
  test_app's CMakeLists.txt) already exposes its real include dir
  regardless of these manual appends. Removed the whole `components/`
  tree (`git rm -r`) and both CMake blocks.
- `unity-app/` (untracked, empty): leftover from a 2026 "relocate device
  test apps under test" commit that moved its contents elsewhere; nothing
  referenced it. Removed.
- `build/` + `build_clang_tidy/` (untracked, gitignored, 172MB+512K):
  stray `esp_bt_audio_source` build output built from the repo root instead
  of from inside `esp_bt_audio_source/` (confirmed via `CMAKE_PROJECT_NAME`
  in their CMakeCache.txt). Removed.
- Verified the CMakeLists.txt edit didn't break anything: `idf.py build`
  clean for esp_bt_audio_source, full host suite 74/74 passing.
  Commit 3d639593.

## 2026-07-27T21:40:56Z - Claude Sonnet 5 - backfilled GitHub Releases for v0.1.0/v0.2.0/v0.3.0

- Follow-up to the CHANGELOG.md work: user asked how to get release notes to
  show up "with a tag" -> explained GitHub's separate Releases feature
  (`gh release create <tag> --notes-file ...`) is what does this, distinct
  from CHANGELOG.md. User said "let's do that."
- Created 3 GitHub Releases (v0.1.0, v0.2.0, v0.3.0), notes pulled verbatim
  from each tag's own annotated message via
  `git for-each-ref refs/tags/<tag> --format='%(contents)'` — no new content
  authored, matches the existing tag-message-as-release-notes convention.
  v0.3.0 marked as the repo's "Latest" release; v0.1.0/v0.2.0 explicitly
  `--latest=false`.
- Deliberately did NOT create releases for the two milestone tags
  (`v0.1.0-pre-hardening`, `v0.2.0-mainc-stable`) -- consistent with
  CHANGELOG.md treating them as checkpoints, not the numbered release line.
- Found and fixed a real gap while doing this: the `v0.1.0` tag existed only
  locally and had never been pushed to `origin` (confirmed via
  `git ls-remote --tags origin`) -- `gh release create` failed until it was
  pushed. Now pushed; `origin` has all 3 numbered tags plus both milestone
  tags.

## 2026-07-27T21:36:10Z - Claude Sonnet 5 - added CHANGELOG.md, generated from existing git tags

- Follow-up to the v0.3.0 retag: I'd recommended (exploratory question,
  2-3 sentence tradeoff) that a CHANGELOG.md was probably unnecessary since
  annotated git tags already serve as this project's release notes — user
  said "generate it anyway."
- Created root `CHANGELOG.md`, reverse-chronological, one entry per tag,
  reproducing each annotated tag's actual message content (v0.3.0, v0.2.0,
  v0.1.0) rather than summarizing/inventing. The two non-linear milestone
  tags (`v0.1.0-pre-hardening` 2026-07-13, `v0.2.0-mainc-stable` 2026-02-01)
  are included in strict chronological (tag date) order and explicitly
  labeled "Milestone" rather than folded into the numbered releases, since
  they're checkpoints, not the version-numbered release line.
- Linked it from root README.md's intro. Kept the git-tag-as-release-notes
  convention as the source of truth — CHANGELOG.md is a generated/derived
  view for GitHub discoverability, not a new place to author release notes
  from scratch; future releases should still get a full annotated tag
  message first, then a matching CHANGELOG.md entry.

## 2026-07-27T21:26:55Z - Claude Sonnet 5 - retagged v0.3.0 onto new HEAD, bumped in-code versions

- User: "retag HEAD on master as v0.3.0. Make sure that versions in the code
  is v0.3.0." The existing v0.3.0 tag (from the 2026-07-25 session) pointed
  at 8d9626f1, 3 commits behind current HEAD (the README overhaul + Active
  TODOs spec/TODO from this session).
- Checked how each project reports its version: esp_bt_audio_source has no
  PROJECT_VER override in CMakeLists.txt, so CONFIG_APP_PROJECT_VER is
  derived automatically from `git describe` — no code change needed there,
  it will read exactly "v0.3.0" once HEAD carries that tag cleanly.
  esp_i2s_source hardcodes `set(PROJECT_VER "0.1.0")` in CMakeLists.txt
  (stale) — bumped to "0.3.0". Also bumped the stale "0.1.0" in
  esp_i2s_source/web/package.json and package-lock.json (npm metadata,
  not shown in the UI, but was inconsistent with the release). Commit
  98680b30, pushed.
- Moved the tag: deleted the old v0.3.0 (local + `origin`, since it was
  already pushed) and re-created it as an annotated tag on the new HEAD
  (98680b30). Wrote an updated tag message carrying forward the full
  original v0.3.0 release notes (auth removal, HTTPS-hang fix, I2S static
  fix, coverage push) plus a new section covering what changed since that
  tag point: the version-string note above, the README overhaul, and the
  two new Active TODOs docs. Pushed the new tag to origin. Verified
  `git describe --tags` at HEAD now prints exactly `v0.3.0`.
- This project has no CHANGELOG.md; release documentation lives entirely in
  annotated tag messages by established convention (see v0.1.0/v0.2.0/v0.3.0
  tag bodies) — followed that pattern rather than introducing a new file.

## 2026-07-27T20:43:51Z - Claude Sonnet 5 - root README overhaul: cut stale content, added real wiring section

- User (blunt, direct feedback): root README.md had "a lot of bullshit crap" —
  cut the Raspberry Pi (`rpi_i2s_source`)/BeagleBone Green (`bbgw_i2s_source`)
  archived-projects mention (no user-facing value) and the entire "Project
  Status" section (dated snapshot, "kind of useless" as a living doc). Left
  those mentions intact in CLAUDE.md/AGENTS.md (repo-layout reference for
  agent sessions, not user-facing) and historical docs — only the top-level
  README changed.
- Added a new "Connecting the ESP32-S3 to the ESP32-WROOM32" section at root
  level (previously this only lived in `esp_i2s_source/docs/SPEC.md` §3,
  buried in a sub-project doc) with the actual wiring table (I2S BCLK/WS/DOUT
  + UART2 cross-connect, sourced from SPEC.md §3.2) and both real jumper-wiring
  photos from `imgs/` embedded side-by-side.
- Added an explicit safety warning: never tie the two boards' 3.3V pins
  together while both are on USB power — each board has its own onboard
  regulator; only GND + the specific signal pins should cross between boards.
  User's explicit ask, a real hardware-safety gap that wasn't documented
  anywhere before.
- Not committed yet — awaiting explicit instruction.

## 2026-07-27T20:36:35Z - Claude Sonnet 5 - spec+TODO for the two README Active TODOs

- User asked to create a spec file and TODO file for the two open "Active
  TODOs" from root README.md (longer-duration UARTAUDIO regression pytest;
  physical UART2 verification) — planning only, "we'll take care of it
  later," no implementation this session.
- Created `docs/ACTIVE_TODOS_SPEC.md` and `docs/ACTIVE_TODOS_TODO.md`.
- Spec item 1 (UARTAUDIO throughput regression guard): traced the two real
  bugs this guards against (bc2f2e8a chunk-per-wake/tick-starvation ceiling;
  3ffbb670 UART FIFO overflow), found the existing E2E test
  (`test_stream_tone_to_laptop_sink` in test_uart_streaming.py) only streams
  ~3s and checks counters once at STOP — too short to have caught either
  bug. Designed a two-tier approach: 60s default "extended" test with
  mid-stream UA|FILL sampling (not just a final counter check), plus an
  optional env-var-gated soak tier (10-30 min) using a generator-based tone
  helper so duration doesn't scale test-process memory.
- Spec item 2 (UART2 physical verification): wrote a manual verification
  procedure (wiring incl. 3.3V-level warning, pre-check, basic round-trip,
  independent-operation check, and — the actual reason UART2 exists —
  concurrency-under-UARTAUDIO-load check) to run once a second USB-serial
  adapter is acquired. No code changes anticipated; blocked purely on
  hardware.
- Not committed yet — planning docs only, awaiting explicit instruction.

## 2026-07-25T19:42:44Z - Claude Sonnet 5 - removed the 2 dead-code stubs flagged during the Ralph loop

- Follow-up to the UNIT_TESTS2_TODO.md Ralph loop: user asked to remove the
  2 dead-code stubs identified during P2 (audio_processor_is_wav_active,
  audio_processor_dump_tag_queue). Confirmed via repo-wide grep neither had
  any real caller anywhere (only the definition, header decl, and this
  session's own tests referenced them).
- Deleted both functions (audio_processor.c, audio_processor_diag.c), their
  audio_processor.h declarations, the tests written for them, and an
  orphaned duplicate mock of dump_tag_queue in
  mocks/audio_processor_host_stub.c (also unused by anything).
- Full host suite 74/74 (943 cases, was 946 — the 3 removed tests);
  idf.py build clean. Coverage held at 84.2% overall (both files' line
  counts dropped since the removed code simply doesn't exist anymore, not
  just uncovered). Commit b5144a3a, pushed.

## 2026-07-25T19:31:52Z - Claude Sonnet 5 - Ralph loop: docs/UNIT_TESTS2_TODO.md complete (all P0/P1/P2)

- User: "Ralph loop docs/UNIT_TESTS2_TODO.md" -> invoked the `ralph-loop` skill.
  Worked every task/subtask top to bottom, one commit+push per subtask, full
  suite + idf.py build verified before each commit, per the loop's rules.
  10 commits total (26604b70..e996b24b range continues into f33b821f..e996b24b
  for this loop specifically). All pushed.
- **I2S-1** (radio_stream.c SSRF/redirect logic, P0): built a new controllable
  fake_http_client mock (hop-queue model, replays headers through the real
  event_handler). De-static'd resolve_redirect_location/redirect_target_allowed/
  connect_with_redirects/codec_from_ct/ci_contains/reconnect_delay_ms into
  radio_internal.h. 33 new cases; confirmed the SSRF-block assertion passes.
  15.2% -> 46.8%.
- **I2S-2** (radio_ring.c, P0): new test_radio_ring target, semaphore mock
  only. 20 cases. 0% -> 100%.
- **BT-1** (audio_processor_config.c): de-static'd configure_i2s; added
  i2s_manager_init/nvs_storage_set_i2s_pins spies to the shared stub. 28
  cases. 16.7% -> 88.3%.
- **I2S-3** (ctrl.c): new ctrl_internal.h de-statics do_action/wifi_connected/
  status_running/resume_result_str/scan_result_str/s_sm; made
  ctrl_device_stubs.c controllable. 19 cases. 21.6% -> 46.9% (task-loop-bound,
  short of the 55% stretch goal, expected).
- **BT-2** (audio_processor_sync_diag.c): 3 cases. 0% -> 87.8%.
- **BT-3** (bt_manager.c): 13 cases covering pair/connect/set_name/snapshots.
  70.1% -> 91.8%. Found+fixed a real bug-class gap: bt_ctx_lock() silently
  failed the whole time because this test file never calls bt_manager_init()
  (by convention) so s_bt_ctx_mutex was NULL -- fixed via the existing-but-
  unused bt_manager_test_init_mutex() hook.
- **P2** (bt_link.c, i2s_out.c audit + audio_processor.c/_diag.c fillers):
  confirmed all remaining zero-call functions in both audited files are
  genuine FreeRTOS task loops (out of scope); 13 more cases across 4 files.
- **Found and fixed 3 missing header declarations** this loop (real
  pre-existing gaps, not introduced by this work): bt_manager_pair,
  audio_processor_is_wav_active, audio_processor_dump_tag_queue -- all
  public functions that only "worked" via lucky implicit-declaration
  matching their real signature. Also confirmed audio_processor_is_wav_active
  and audio_processor_dump_tag_queue are dead-code stubs from removed
  features (WAV/play_manager, legacy tag queue) -- flagged as dead-code-
  removal candidates for a future pass, not removed here (out of scope).
- **Final coverage**: esp_bt_audio_source 79.4% -> 84.2% (946 host cases, was
  891, 74 suites unchanged); esp_i2s_source 55.1% -> 61.7% (27 suites, was 25).
  Both projects' full suites green and idf.py builds clean at every commit.
- docs/UNIT_TESTS2_TODO.md fully updated in place (every checkbox checked,
  every section has a "✅ DONE" summary with before/after numbers) -- serves
  as the permanent record of this work, no separate report needed.

## 2026-07-25T18:15:34Z - Claude Sonnet 5 - coverage audit -> docs/UNIT_TESTS2_TODO.md

- Follow-up to the "any parts of the code need more coverage" question: measured
  fresh coverage for BOTH projects (esp_bt_audio_source 79.4%, up from README's
  stale 78.1%; esp_i2s_source 55.1%, not tracked anywhere before). Went to
  function-level (lcov .func.html) to separate real gaps from noise — several
  files are *linked* into a test binary for compile completeness but never
  actually invoked by any test case, which the file-level % alone can't show.
- Biggest finding: esp_i2s_source's `radio_stream.c` (15.2%) has ZERO coverage
  on `redirect_target_allowed`/`resolve_redirect_location`/`connect_with_redirects`
  — the exact logic that re-validates the SSRF/URL policy on every HTTP
  redirect hop. Currently no test evidence it actually blocks a malicious
  redirect to a private/internal address. `radio_ring.c` (0%, the two SPSC byte
  rings) is linked into test_radio_lifecycle but never called either.
- User asked to write these up as a detailed TODO. Created
  docs/UNIT_TESTS2_TODO.md (repo-root docs/, cross-project, follow-on to the
  existing docs/UNIT_TESTS1_TODO.md Batch 1 which was esp_bt_audio_source-only
  and already complete). Read the actual untested functions before writing
  each subtask (not just names off the coverage report) — e.g. confirmed
  redirect_target_allowed's DNS-check branch is ESP_PLATFORM-gated and compiles
  out on host, so it's actually fully testable without network mocking; found
  mocks/stubs/esp_http_client.h has declarations but no mock behavior, so
  documented the exact fake-HTTP-client shape needed as a prerequisite; found
  set_channels validates its enum arg but the near-identical set_bit_depth
  doesn't (flagged as a question, not assumed a bug). Priority order: I2S-1
  (SSRF-relevant) -> I2S-2 (cheap/clean) -> BT-1 (biggest single-file %) ->
  I2S-3 -> BT-2 -> BT-3 -> P2 batch. Commit 63d76f93, not pushed.

## 2026-07-25T17:34:29Z - Claude Sonnet 5 - README/docs audit — brought living docs current, fixed real inaccuracies

- User asked whether README.md and other markdown docs were up to date. Scoped
  to LIVING docs only (READMEs, SPEC.md) — deliberately skipped the large
  dated/historical archives (esp_bt_audio_source/code_review/*, esp_i2s_source/
  docs/archive/*, the FIX3-dated docs, PRD.md/FS.md/REDO1_TODO.md) since those
  are point-in-time records, not meant to track current state (same reasoning
  as the auth-removal doc-banner pass earlier today).
- Used 3 parallel Explore agents (root, esp_i2s_source, esp_bt_audio_source)
  to fact-check each living doc's claims against actual code, then fixed
  everything myself. Real findings beyond stale prose:
  - **esp_bt_audio_source/README.md had the audio-source priority ORDER
    WRONG**: doc said SYNTH ranks above I2S; `get_active_source()` in
    audio_processor_engine.c actually ranks I2S above SYNTH (SYNTH is
    fallback-only, "keeps A2DP alive when no I2S"). Real code/doc conflict,
    not just staleness.
  - Host test counts were stale on both projects: esp_i2s_source 19->25
    suites (verified via CMakeLists add_test), esp_bt_audio_source 883->891
    cases (verified via `run_all_tests.py --no-device --no-standalone`,
    authoritative aggregator — the +7 came from today's I2S hysteresis tests
    plus at least 1 more not yet reflected anywhere).
  - esp_i2s_source's `/api/*` route table was missing 6 of 14 real routes
    (bt, btvolume, scan, console, apmode, prebuffer) — verified against
    web_ui.c's actual `.uri =` registrations.
  - main/README.md's `main.c` line anchor (#L966-L977) was dead — file is
    533 lines total; also said I2S role "slave" (WROOM32 is actually master,
    per the 2026-07-11 role flip — a second, independent instance of the same
    class of error).
  - tools/README.md had a bad-merge artifact: the flash_and_verify_spiffs.py
    section was duplicated verbatim back-to-back; test/host_test/README.md
    had THREE overlapping copies of the same quick-start concatenated.
  - Nothing anywhere documented UARTAUDIO's `stream_audio_uart.py`, the
    Bluetooth tab, station export/import, or the payload-phase-hysteresis
    mechanism — all real, committed, zero doc coverage.
  - conda run -n python310 -> uv .venv, and unpinned `$HOME/esp/esp-idf` ->
    `$HOME/esp/v5.5.1/esp-idf`, fixed everywhere per CLAUDE.md's documented
    convention. AGENTS.md had drifted from CLAUDE.md (missing the
    memory_archive.md addition) — resynced to identical.
- Commit b3aeb733 (7 files). NOT pushed — awaiting explicit push instruction.

## 2026-07-25T17:12:20Z - Claude Fable 5 - I2S static fix CONFIRMED audibly; pushed

- Closes the 17:08 entry's PENDING item. Speaker reconnected after user
  power-cycled it — manual CONNECT attempts still went nowhere (INITIATED,
  then silence in the BT log), but a WROOM32 RESET's boot-time auto-reconnect
  grabbed it immediately. Pattern to remember: when SICKLUGGAGE refuses a
  manual connect, reboot the WROOM32 instead of retrying CONNECT.
- User confirms Groove Salad plays CLEAN through the BT speaker — no static.
  Phase-lock hysteresis fix verified end-to-end (0 re-locks on serial + ears).
- Pushed 26604b70 + 593d2301 + 71de33b8 to origin/master.

## 2026-07-25T17:08:06Z - Claude Fable 5 - I2S static ROOT-CAUSED + FIXED (phase-lock hysteresis); UARTAUDIO streamer recovered

- Continuation of the "BT silent" saga (see 13:35 entry). Major corrections to
  that entry's conclusions:
  (1) The WROOM32 STATUS counters (BYTES_REQ/PKTS/CALLBACKS) do NOT reflect the
      live audio path — user heard audio while all read 0. Do not diagnose from
      them. (2) The silence WAS fixed by the production reflash (bd0ff2ad fw ->
      1da4d052); symptom then became music-with-static-patches.
- USB isolation test (user's idea): streamed 40 s of 22.05 kHz stereo two-tone
  laptop->USB->WROOM32->A2DP via UARTAUDIO — CLEAN (0 und/crc/lost/ovf, 100%
  real-time A2DP pull, user confirmed clean audio). Proved the entire BT output
  chain healthy; fault isolated to the S3->I2S->WROOM32 leg.
- Found tools/stream_audio_uart.py had been accidentally truncated to a
  136-byte stub by docs commit 3010bdd7 (was a working 290-line tool at
  0bb5555e "first fully clean stream"). Restored verbatim; commit 26604b70.
- STATIC ROOT CAUSE: i2s_manager re-ran i2s_frame_extract_detect() every block
  with no memory. Serial capture during Groove Salad: 113 phase re-locks in
  31 s (alternating 0,2 / 1,0 / 3,2 / ...); each re-lock reinterprets the
  16-in-32 sample lanes mid-stream = static patches. (S3 packs sample<<16 in
  main.c:127; contract says phase is session-constant, so thrash = detector
  noise, not real slips.)
- FIX (commit 593d2301): new pure helper i2s_frame_phase_hold() in
  i2s_frame_extract.c — keep locked phase until the SAME challenger persists
  I2S_FRAME_PHASE_HOLD_BLOCKS=8 consecutive blocks (~90 ms); silent blocks hold;
  challenger switch restarts tally; state resets on channel re-enable
  (i2s_manager_start). 7 new host tests (74/74 pass). On-device verification:
  0 phase changes in 35 s (was 113/31 s), fw v0.2.0-18 flashed to WROOM32.
- PENDING: audible confirmation — speaker "SICKLUGGAGE H1-013" dropped into
  standby during the work (discoverable but refuses connect; needs a physical
  button tap / power cycle), so user hasn't heard the fixed stream yet.
  Commits 26604b70 + 593d2301 are local, NOT pushed.

## 2026-07-25T13:35:35Z - Claude Sonnet 5 - BT audio silent: A2DP CALLBACKS=0, ruled out everything but the S3<->WROOM32 I2S wire

- Symptom: radio "playing" on S3 but nothing audible through BT speaker
  "SICKLUGGAGE H1-013" (AD:CD:EE:FD:FE:CA). Added station id 11 "Cleansing 2000s"
  (http://142.44.136.201:5457/stream) — S3 decodes it perfectly (mp3 128k, 0
  decode errs, ICY title, i2s_written climbing steadily).
- WROOM32 STATUS shows A2DP **connected but transmitting zero**:
  BYTES_REQ=0, BYTES_PROD=0, PKTS=0, CALLBACKS=0 — the A2DP source data callback
  NEVER fires, so no audio reaches the speaker. Constant all session.
- Ruled OUT, in order: the station/URL (S3 side flawless); volume (VOL 51, MUTE=0);
  a stuck A2DP state (disconnect/reconnect, START, local BEEP — all left CALLBACKS=0);
  WROOM32 BT stack (full RESET/reboot — still 0); the speaker (user's PHONE
  connects + plays audio through it fine); phone holding the 1 A2DP slot (user
  forgot it on the phone — still 0); and FIRMWARE — reflashed WROOM32 with the
  latest production build (version went bd0ff2ad-> v0.2.0-17-g1da4d052, confirming
  it WAS running different fw) and A2DP STILL CALLBACKS=0. So it is NOT firmware.
- Leading hypothesis = the physical S3<->WROOM32 I2S link, specifically the DATA
  wire. Wiring is 4 lines + GND; WROOM32 is I2S MASTER (drives BCLK/WS), S3 is
  SLAVE (BCLK=GPIO15 in, WS=GPIO16 in, DOUT=GPIO7 out). S3's i2s_written keeps
  climbing => it IS receiving the master's BCLK/WS (those wires + GND are good),
  so the suspect is the DATA line **S3 GPIO7 (DOUT) -> WROOM32 DIN**. WROOM32
  `I2S_PROBE` command consistently TIMES OUT (its I2S RX sees no data), which
  plausibly stalls the audio->A2DP pipeline so media never actually streams
  (CALLBACKS=0). NEEDS: physical check of that data wire + common ground, and a
  WROOM32 serial-monitor capture (idf.py -p /dev/ttyUSB0 monitor) during a
  connect+START to see the real AVDTP/I2S log. Left off awaiting user's hardware check.
- Also this session: removed device-token auth end-to-end (frontend 78dc181d,
  backend 132eb096, docs 1da4d052) — all pushed. WROOM32 reflashed with prod fw.

## 2026-07-25T12:41:09Z - Claude Sonnet 5 - esp_i2s_source: updated docs for the auth removal

- Follow-up to the two removal commits below (user: "Update those too").
- docs/SPEC.md §7.1 "Auth model" rewritten from the bearer-token description to
  "None" — states the web server has no auth, why (trusted-LAN, user request
  2026-07-25), points at the removal commits, and recommends a reverse proxy if
  auth is ever wanted again (living spec = real content update).
- Added a superseded/⚠️ banner under the title of the 8 dated/archived design
  docs that still describe the old auth (FIX3 SPEC/TODO/SUMMARY, the FIX3 code
  review, and archive/ IMPLEMENTATION_SUMMARY, IMPLEMENTATION_TODO, PHASES_7_12,
  FIX_SPEC_V2). Historical bodies left intact — banner marks them stale rather
  than rewriting the record. Verified: every auth-mentioning .md now has a banner
  except SPEC.md (which carries the real updated §7.1). Docs-only; no code/build.

## 2026-07-25T12:36:57Z - Claude Sonnet 5 - esp_i2s_source: ripped out the backend device-token machinery too

- Follow-up to the frontend removal below (user: "Rip that out too"). Removed the
  entire dormant bearer-token auth backend.
- Deleted: components/web_ui/web_ui_auth.c, web_ui_auth_core.c,
  include/web_ui_auth_core.h; test/host_test/test_web_auth.c;
  tools/test_web_ui_route_auth.py (~665 lines).
- web_ui.c: dropped the web_ui_auth_init() gate from web_ui_start() (boot no
  longer generates/loads/prints a token; no more AUTH|BOOTSTRAP_TOKEN); stripped
  the now-dead .auth_required + .capability fields from all 16 S_*_POST/DELETE
  route ctxs (capability was write-only — never read via ctx->capability).
  web_route_ctx_t is now just { handler }. web_ui_internal.h: removed the 4
  web_ui_auth_* decls + trimmed the struct. web_ui.h: removed web_ui_auth_rotate.
- console.c: removed handle_auth() + the "AUTH" dispatch branch + AUTH ROTATE
  from the READY help string; dropped the now-unused web_ui.h include; removed
  web_ui from cmd_console/CMakeLists REQUIRES. web_ui/CMakeLists: dropped the two
  auth sources. host_test/CMakeLists: removed the test_web_auth target.
  verify_host.sh: removed the route-auth pytest line. Updated 3 stale doc
  comments that cited web_ui_auth as a split-pattern example (wifi_mgr.c,
  wifi_creds_core.h, stations.h "same trust boundary as AUTH ROTATE").
- Verified: host tests 25/25 (was 26; test_web_auth gone), idf.py build clean
  (binary shrank 0x1c0130->0x1bf9c0), flashed S3 on /dev/ttyACM0, booted clean
  (uptime OK, capabilities.web=true so web_ui_start succeeded without the auth
  gate), console READY line no longer lists AUTH ROTATE, no BOOTSTRAP_TOKEN
  emitted. Restarted Groove Salad (playing, mp3). NOTE: historical design docs
  (SPEC.md, dated FIX3 §5 summaries) still describe the old auth design — left
  as historical record, not rewritten.

## 2026-07-25T12:26:41Z - Claude Sonnet 5 - esp_i2s_source: removed the vestigial Device-token UI

- Follow-up to the earlier auth-disable work. Device-token auth ENFORCEMENT was
  already gone (backend route_dispatch() no longer checks a token; api.ts client
  gate removed), but the UI element survived: the header lock button 🔒 +
  Auth.tsx panel. The user re-noticed it (clicked the lock, saw "Device token"
  panel with no error banner) and asked why it was back — it had never been
  removed, only made non-functional.
- Removed it fully from the frontend: deleted web/src/Auth.tsx; dropped
  <AuthPanel/> + its import from App.tsx header; stripped the dormant token
  machinery from api.ts (getAuthToken/setAuthToken/clearAuthToken/onAuthRequired/
  notifyAuthRequired, TOKEN_KEY/RE, the apiRequest Authorization-header attach,
  and the 401->notifyAuthRequired path); removed the dead .auth-* CSS from
  index.css; deleted/trimmed the now-obsolete auth tests in __tests__/api.test.ts
  (kept two "sends no auth" behavior tests).
- Verified: vitest 22/22 pass, tsc+vite build clean, web asset re-embedded
  (index.html.gz). idf.py build OK (71% app partition free). Flashed S3 on
  /dev/ttyACM0. Confirmed served page has zero auth-toggle/auth-panel/"Device
  token"/getAuthToken strings. NOTE: the BACKEND token machinery (web_ui_auth.c,
  AUTH ROTATE console cmd) still exists but is dormant/unenforced — left in place;
  a fuller purge would touch host tests (test_web_auth.c, route-auth tools).
- Reflash rebooted the S3 and stopped playback; restarted Groove Salad
  (POST /api/radio {url: somafm groovesalad.pls}) -> playing=True, codec=mp3,
  bytes flowing. Committed locally; not pushed (awaiting explicit go-ahead).

## 2026-07-25T09:49:42Z - Claude Opus 4.8 - esp_i2s_source: HTTPS-vs-web-UI hang ROOT-CAUSED + FIXED (MBEDTLS_DYNAMIC_BUFFER)

- CLOSES the multi-entry HTTPS-hang saga (see 2026-07-25T05:01/07:01 entries).
  Earlier conclusions (weak WiFi; RAM-not-the-trigger) were both incomplete.
  A clean AP-based test harness (laptop USB WiFi adapter wlx18d6c70f915d
  connected to the S3 SoftAP ESP32-S3-Audio @192.168.4.1, ~4ms link, binary
  0/10-vs-10/10 storm result, no router confound) isolated it definitively.
- MECHANISM (confirmed, not inferred): each HTTPS connection pins a ~16KB
  internal-RAM mbedTLS RX buffer for its lifetime (SSL_IN_CONTENT_LEN=16384,
  INTERNAL_MEM_ALLOC=y, DYNAMIC_BUFFER was off). During an https stream that
  drops internal DMA-capable largest-free-block to ~3-7KB. WiFi dynamic TX
  buffers (ESP_WIFI_TX_BUFFER_TYPE=1, up to 32, each needs ~1.6KB contiguous
  internal) then can't allocate under CONCURRENT web load -> httpd sends stall
  with EAGAIN (errno 11), no alloc-failure logged -> web UI hangs. HTTP streams
  don't pin that RAM so they never hit it. Elimination now complete: NOT codec
  (http-aac fine), NOT bandwidth (mp3@128 fine > aac@64), NOT web-serve link
  quality (fails over strong AP), NOT signal (fails at -57), NOT CPU/core
  (pinning httpd to core1 while wifi on core0 changed nothing - tested), and
  low-RAM-alone isn't it either (single loads fine at 3KB) — it's the
  RAM-pins-TX-buffers-under-concurrency chain.
- FIX: CONFIG_MBEDTLS_DYNAMIC_BUFFER=y (mbedTLS frees TX/RX buffers when idle).
  Added to sdkconfig.defaults (NOT sdkconfig — that's GITIGNORED here; the
  defaults file is the tracked source of truth; a bare sdkconfig edit would be
  lost on fullclean). Deps ok (needs !DTLS, satisfied). Verified on-device via
  the AP storm harness: under https-aac, dma_largest ~3-7KB -> 10.7KB, and the
  10-concurrent storm went 0/10 -> 10/10 (ran twice); http-mp3 stayed 10/10;
  https .pls resolves+plays, no crash, uptime climbing. Build clean, +~4KB bin.
- Credit: Fable-5 second opinion nailed the refined mechanism (TLS RAM pin ->
  WiFi TX buffer starvation under concurrency), reconciling why the earlier
  router-paced 12/12 result (serialized demand) didn't contradict the AP 0/10
  (simultaneous demand). Committed.

## 2026-07-25T08:32:55Z - Claude Opus 4.8 - esp_i2s_source: split radio.c (803 -> 748 lines)

- Per user (get it < 800, same as wifi_mgr): extracted the prebuffer-threshold
  NVS persistence out of radio.c into a new radio_prebuffer.c (74 lines):
  radio_get_prebuffer_ms / radio_set_prebuffer_ms (both public, in radio.h) +
  radio_prebuffer_load (was static in radio.c; now declared in
  radio_internal.h so radio_init() can still call it). Moved the NVS_NS_RADIO/
  NVS_KEY_PREBUF defines there too. radio.c now does ZERO nvs_* calls -> dropped
  its #include "nvs.h". g_radio_prebuffer_bytes stays defined in radio_ring.c
  (extern in radio_internal.h); PCM_BYTES_PER_MS / PREBUF_MS_* stay in
  radio_internal.h. Logic byte-identical.
- Wired radio_prebuffer.c into components/radio/CMakeLists.txt SRCS AND
  test/host_test/CMakeLists.txt test_radio_lifecycle (its prebuffer NVS tests
  exercise these via radio_init/radio_set_prebuffer_ms). Build clean; full host
  suite 26/26 incl. test_radio_lifecycle 41/41 (prebuffer tests green); flashed
  S3, verified prebuffer set 3000->2000->read-back and radio AAC playback
  (dec_err=0), then restored prebuffer to 3000. COMMITTED.
- radio.c now #3-ish; remaining >700-line first-party files: i2s_out.c (755),
  bt_manager.c (715, other project), bt_link.c (691), ctrl.c (669), etc.

## 2026-07-25T08:24:49Z - Claude Opus 4.8 - esp_i2s_source: split wifi_mgr.c (942 -> 704 lines)

- Per user (get it < 800): extracted the credential NVS persistence out of
  wifi_mgr.c into a new wifi_mgr_nvs.c (225 lines) + wifi_mgr_internal.h (68).
  Bundled the 9 file-static credential vars (s_ssid/s_pass/s_ap_ssid/s_ap_pass
  + lens + s_ap_enabled) into a single `wifi_creds_t s_creds` struct defined
  in the internal header; the moved functions (load/save/erase_creds,
  load/save_ap_enabled, set_default/load_ap_creds, nvs_get_string_exact, +
  new wifi_nvs_write_ap_creds) take `wifi_creds_t *` by pointer — no shared
  globals. wifi_mgr.c now does zero direct nvs_* calls (dropped nvs.h include
  and the NVS_KEY_ defines, both moved to the internal header). set_ap_config's
  inline NVS persist+rollback now call wifi_nvs_write_ap_creds.
- Logic unchanged (byte-identical bodies, just relocated + parameterized);
  variable renames done via word-boundary sed. Build clean (no warnings),
  host suite 26/26 (incl. the 3 wifi_creds/wifi_sm pure tests — untouched).
  Hardware smoke test (this is device glue, not host-tested): flashed S3, it
  loaded persisted CircuitLaunch creds -> STA CONNECTED 10.1.2.50, AP up,
  MDNS=UP. Save/set_ap_config paths not live-exercised (would disrupt the
  link) but the moved logic is unchanged. COMMITTED.

## 2026-07-25T07:49:11Z - Claude Opus 4.8 - esp_i2s_source: Bluetooth card moved to its own tab

- Per user: moved the <Bluetooth /> card out of the Settings tab into a new
  dedicated "Bluetooth" tab, ordered Radio/Tone/Terminal/**Bluetooth**/Settings
  (between Terminal and Settings). web/src/App.tsx only: added the tab to TABS,
  added a `tab === "bluetooth"` render block (<div class=grid><Bluetooth/></div>),
  removed the "Row 3: Bluetooth (full width)" block from the settings grid.
  Bluetooth() takes no props so it's self-contained. 28/28 vitest, build clean,
  flashed; device-served bundle sha256 == committed. COMMITTED.

## 2026-07-25T07:43:47Z - Claude Opus 4.8 - esp_i2s_source: station list export/import (merge + dedup) in web UI

- User asked for a way to download the station list and reload it, with dedup.
  Chose (via AskUserQuestion): import = MERGE + dedup (add new, keep existing,
  never destructive) and dedup key = EXACT URL (matches the device's own
  station_store dedup; http vs https vs ice2/ice5 are distinct).
- Frontend-only feature (no firmware/API change; reuses GET+POST /api/stations,
  whose POST already rejects exact-URL dupes). Added to web/src/api.ts three
  PURE, unit-tested helpers: buildStationsExport(stations, now) ->
  {format:"esp-i2s-source/stations",version:1,exported_at,stations:[{name,url}]},
  parseStationsImport(text) (accepts our envelope OR a bare array; trims; drops
  url-less/non-object entries; throws on bad JSON / empty), and
  planStationMerge(imported, existing) -> {toAdd, duplicates} (dedup by exact
  URL vs existing AND within the file, first-wins). Radio.tsx: Export button
  (Blob download esp-radio-stations.json) + Import button (hidden file input;
  reads file, plans merge, POSTs each toAdd via addStation, reports
  "N added, M duplicates skipped[, K rejected]"). index.css: .radio-io row.
- 6 new vitest cases (28 total, all green); npm run build clean (tsc + vite +
  embed, 57.2KB gz); idf.py build clean; flashed to S3; device-served bundle
  sha256 == committed main/www/index.html.gz; server-side dedup re-verified
  live ("duplicate URL"). COMMITTED.

## 2026-07-25T07:01:42Z - Claude Fable 5 - esp_i2s_source: HTTPS-vs-web-UI hang investigation (root = weak WiFi, not RAM)

- Symptom: web UI (http://10.1.2.50/, ~57KB gz SPA) hangs while radio streams
  an HTTPS station, but NOT an HTTP station. Prior (Opus) A/B test proved it's
  TLS-specific, not the AAC decoder (HTTP-AAC loads fine; only HTTPS hangs) and
  not a logged alloc failure; serial showed httpd send EAGAIN(11)/ECONNRESET(104)
  -> "uri handler execution failed". Prior conclusion: root = weak WiFi airtime.
- Fable second opinion challenged the (inferred) airtime mechanism and proposed
  internal DMA-RAM starvation instead: /api/status heap_free =
  esp_get_free_heap_size() = internal+PSRAM, PSRAM-dominated, so it HID internal
  RAM; and MBEDTLS_INTERNAL_MEM_ALLOC=y + SSL_IN_CONTENT_LEN=16384 +
  DYNAMIC_BUFFER off pins ~16KB internal per HTTPS conn, while WiFi dynamic TX
  buffers + lwip are also internal (SPIRAM_TRY_ALLOCATE_WIFI_LWIP off).
- EXPERIMENT (added heap_caps telemetry to /api/status — this commit): measured
  heap_internal_free / heap_dma_free / heap_dma_largest under stopped/MP3/HTTPS.
  RESULT: TLS pinning CONFIRMED (dma_largest 31744 stopped -> 23552 MP3 ->
  ~3-7KB HTTPS). BUT hypothesis REFUTED as the trigger: at RSSI -62/-64 (good),
  HTTPS-AAC + 12 concurrent page loads with dma_largest driven to 3328-5120B ->
  12/12 loaded fine. Low DMA RAM alone does NOT cause the hang. The variable
  that tracked the hang was RSSI: hung earlier when signal drifted to ~-70,
  won't reproduce at -62. So Opus's "weak WiFi is root cause" stands; TLS's
  ~16KB internal-RAM pinning is a real CO-FACTOR (makes HTTPS more fragile at
  the margin, e.g. survived -70 on HTTP but not HTTPS) but not the trigger.
  Residual confounds (couldn't reproduce in-the-act at -62; device was rebooted
  since the hang, clearing any long-uptime fragmentation).
- Upshot: fix = HTTP-AAC stations (work) or better WiFi (cure).
  MBEDTLS_DYNAMIC_BUFFER would add RAM headroom but RAM isn't the binding
  constraint, so not worth the churn. Kept the heap telemetry as a useful
  permanent diagnostic (heap_internal_free/heap_dma_free/heap_dma_largest in
  /api/status; frontend doesn't consume them).

## 2026-07-25T05:58:25Z - Claude Opus 4.8 - esp_i2s_source: AAC decoder verified working; fixed https:// station crash (radio task stack overflow)

- AAC decoder WORKS (user asked): verified live on-device with SomaFM AAC
  (codec auto-detected =aac from Content-Type audio/aac, dec_ch=2 stereo,
  dec_err=0, HE-AAC/AAC+ via radio_decode.c aac_plus_enable=true). Added a
  station "Groove Salad (AAC)" -> http://ice2.somafm.com/groovesalad-64-aac
  (64k AAC+, ~8KB/s — fits the marginal WiFi). SomaFM serves AAC for every
  channel as <chan>-64-aac on ice2/ice5.somafm.com (http OR https).
- CRASH BUG FOUND + FIXED: playing an https:// station rebooted the device —
  serial showed "***ERROR*** A stack overflow in task radio_cmd". The TLS
  handshake (mbedTLS + esp_crt_bundle) runs on whichever task opens the
  connection: radio_cmd (4096) during playlist (.pls) resolution
  [radio_resolve_input, radio_stream.c:131] and the stream task (6144) for
  the media stream [connect_with_redirects, radio_stream.c:272]. TLS needs
  ~5-6KB of stack alone, so 4096 overflowed (and 6144 would too on a direct
  https stream / after an https .pls resolves to an https stream URL — SomaFM
  AAC .pls -> https ice2 URL). Fix: added #defines RADIO_CMD_TASK_STACK and
  RADIO_STREAM_TASK_STACK = 9216, used at both xTaskCreate sites (radio.c).
  Same bug class as the earlier I2S/writer stack overflow. Not host-testable
  (xTaskCreate is mocked); verified live: the exact URL that crashed before
  (https://somafm.com/groovesalad64.pls) now plays AAC with uptime climbing
  (no reboot), dec_err=0, reconnects=0. Host suite still 41/41 radio, build
  clean. COMMITTED. So https:// stations (incl. SomaFM .pls playlists) now
  work.

## 2026-07-25T05:01:43Z - Claude Opus 4.8 - esp_i2s_source: radio "won't play" — fixed FAULTED latch + WiFi TX power; sink issue remains

- Reported symptom: internet radio "was working, now won't play." Investigated
  the whole pipeline (radio state, WiFi, WROOM32 A2DP) on-device. Found THREE
  distinct issues:
  1. FAULTED latch (fixed): radio_play_sync() rejected any play while state was
     RADIO_STATE_FAULTED/FAULTED_JOIN_PENDING (returned ESP_ERR_INVALID_STATE),
     but the web layer had already replied ok:true (async queue) — so the Play
     button silently did nothing; user had to hit Stop first. Fix: let an
     explicit play() auto-recover — it already calls radio_stop_sync() right
     after (unified §7.5 teardown clears the fault); if the worker can't be
     joined, stop's error propagates so we never start over a live session.
     radio.c only. New host test test_fault_play_autorecovers_when_joinable
     (fault -> make joinable -> play succeeds); existing test_fault_blocks_restart
     still passes (stuck worker -> stop keeps timing out -> play still fails).
     Verified live: faulted via bogus URL, then Play (no Stop) auto-recovered.
  2. Starved WiFi throughput (mitigated + likely environmental): stream read at
     ~0.68 KB/s vs the ~16 KB/s a 128kbps station needs, so it never filled the
     3s prebuffer, stayed buffering, and the stall watchdog kept reconnecting.
     A/B PROOF it was the ESP32 link, not server/network/our code: laptop pulled
     the SAME somafm stream (ice6.somafm.com/groovesalad-128-mp3) at ~57 KB/s on
     the same CircuitLaunch 2.4GHz ch6 at the same moment; ESP32 got 0.68 KB/s
     (85x gap). ESP32 RSSI was -70 (drifting from -62). Fix: added
     esp_wifi_set_max_tx_power(84) in the WIFI_EVENT_STA_START handler
     (wifi_mgr.c) — the STA uplink paces the L2/TCP ACKs that gate download
     throughput. (WIFI_PS_NONE was already set from a prior identical
     "stream throttled" fix.) After reflash: RSSI -61, throughput ~18.7 KB/s,
     buffering=False, reconnects=0 — radio streams smoothly. NOTE: RSSI is
     RX-side so the -70->-61 gain is partly re-association luck, not solely the
     TX change; max TX power is a permanent edge but the real cure for a weak
     spot is physical (move closer). CircuitLaunch is a congested makerspace;
     "worked before" was almost certainly a better RF spot.
  3. WROOM32 A2DP output = 0 (NOT fixed, physical): WROOM32 shows CONN=1 to
     sink AD:CD:EE:FD:FE:CA but BYTES_REQ/PKTS/BYTES_PROD stay 0 even after a
     START — a phantom/stale connection to a paired speaker that's off / out of
     range / not accepting A2DP. NOT the laptop (its BT controller is
     E8:FB:1C:25:E4:C2). So S3 delivers audio to the WROOM32 but the last hop
     (WROOM32 -> BT speaker) is dead. User needs a working BT sink powered/in
     range; can DISCONNECT + reconnect a real speaker, or re-add the laptop as
     an A2DP sink to test end-to-end.
- Both fixes committed; full host suite 26/26 green, device build clean.

## 2026-07-25T04:03:51Z - Claude Opus 4.8 - esp_i2s_source: specific station errors + FOUND station id-vs-index bug (unfixed)

- Fixed the "invalid/duplicate/full" catch-all on Add/Edit station: the
  backend had 7 distinct station_result_t codes but collapsed them to one
  string. Added pure `station_result_str()` (station_store.c/.h), threaded
  the specific reason out of `stations_add`/`stations_update` via a new
  nullable `station_result_t *reason` out-param (only caller is
  web_ui_radio.c), and rewired web_ui_radio.c: new `station_reply_bad()`
  emits the specific reason; replaced the old `station_reply(bool)` with a
  success-only `station_reply_ok()`. Now returns e.g. "duplicate URL",
  "invalid URL (need http:// or https://)", "station list full", "URL too
  long". No frontend change (Radio.tsx already displays r.error). Host test
  `test_result_str_is_distinct_and_json_safe` added (15/15 test_station_store);
  device build clean; verified live on S3 (10.1.2.50) — distinct 400s per
  cause. COMMITTED.
- FIXED (2026-07-25, commit after 8eef1778) — station id-vs-index mismatch: GET /api/stations
  returns each station's STABLE id (`stations_get` -> items[idx].id), and
  the frontend sends that stable id for delete/move/edit (Radio.tsx uses
  s.id). But the backend treats the ?id= query param as an ARRAY INDEX
  (`station_id_param` -> atoi -> `stations_remove/move/update` ->
  `station_store_*(candidate, idx)` with an `idx >= count` bound check).
  They only agree when stable-id == index, which diverges after any
  delete/reorder. Confirmed empirically on-device: DELETE ?id=8 (KEXP's
  stable id) -> "station not found"; DELETE ?id=5 (its array index) ->
  removed it. IMPACT: delete/edit/move-up/down in the web UI silently target
  the WRONG station (or fail) once ids != indices — e.g. user's current list
  has stable ids 1,2,4,5,7 at indices 0-4. RIGHT FIX (frontend contract is
  already stable-id): make the backend resolve ?id= to an index via a
  find-by-stable-id (station_store has ids; add/reuse a find_by_id) in the
  DELETE/PUT handlers, and add host tests.
  FIX APPLIED: changed stations_update/remove/move to take a uint32_t stable
  id (was int index) and resolve id->index via the existing host-tested
  station_store_index_by_id() INSIDE the s_mtx hold (atomic, no TOCTOU);
  unknown id -> STATION_ERR_NOT_FOUND. Web callers cast the guarded
  station_id_param() result to uint32_t. Only callers were web_ui_radio.c
  (no internal index-based callers). Verified live on S3: DELETE by stable
  id (id=9 at index 5) removed the correct station and left the other 5
  intact; move down+up by stable id=4 targeted correctly and restored order.

## 2026-07-25T03:40:17Z - Claude Opus 4.8 - esp_bt_audio_source: fixed SCAN (invalid inq_len) + esp_i2s_source frontend auth gate

- SCAN bug: `bt_scan.c` called `esp_bt_gap_start_discovery(..., inq_len=0, 0)`,
  but ESP-IDF requires inq_len in [ESP_BT_GAP_MIN_INQ_LEN 0x01,
  ESP_BT_GAP_MAX_INQ_LEN 0x30] — 0 returns ESP_ERR_INVALID_ARG, so SCAN
  could never have worked on real hardware. Captured the actual error off
  the WROOM32 serial log (`E BT_SCAN: Start device discovery failed:
  ESP_ERR_INVALID_ARG`) to confirm. Fixed: added `#define BT_SCAN_INQ_LEN 10`
  (~12.8s, matching ESP-IDF's a2dp_source example) and used it at both call
  sites (ESP_PLATFORM + UNIT_TEST paths).
- Why host tests missed it (same class as the earlier i2s DMA-starvation
  bug — mock more permissive than real API): `mocks/mock_gap.c`'s
  `esp_bt_gap_start_discovery` did `(void)inq_len;` and returned a canned
  result, never replicating the range check. Fixed the mock to mirror
  ESP-IDF's validation (returns ESP_ERR_INVALID_ARG for bad mode/inq_len),
  added `ESP_BT_GAP_MIN/MAX_INQ_LEN` to the mock header, and added
  `mock_gap_get_last_inq_len()`. New regression test
  `test_bt_start_scan_uses_valid_inq_len` asserts the passed inq_len is in
  range; verified it FAILS with the old inq_len=0 and passes with the fix.
  Full host suite 74/74 green; SCAN verified `OK|SCAN|STARTED` on hardware.
- Also committed the esp_i2s_source frontend half of the device-token
  disable (the backend `route_dispatch()` half was 42460599): `api.ts`
  had a *client-side* gate that threw AUTH_REQUIRED before fetch() for any
  mutating request when no token was stored — that (not the server) was
  what popped the token modal on HELP. Removed it: now attaches the token
  only if one happens to be set, never blocks. Updated api.test.ts
  (dropped the two "mutation without token never calls fetch / fires
  onAuthRequired" tests, added one asserting it now reaches the network
  with no Authorization header), 22/22 vitest green, rebuilt+re-embedded
  the www bundle.
- Operational note: earlier this session's `/lint-n-test` full sweep
  flashed test_manager Unity firmware to the WROOM32 (/dev/ttyUSB0) and
  never restored it — that was why the S3 Terminal's forwarded commands
  timed out (wroom.reachable=false). Reflashed production
  esp_bt_audio_source. LESSON: run_all_tests.py leaves whatever test suite
  ran last on the WROOM32; reflash production firmware after a device
  sweep. (wroom.reachable in /api/status lags — it's a periodic probe; a
  working console round-trip is the real signal.)

## 2026-07-25T03:13:24Z - Claude Sonnet 5 - esp_bt_audio_source: fixed all 3 device Unity suites (0/3 -> 3/3 passing)

- Root cause (verified against ESP-IDF's own `tools/cmake/build.cmake` /
  `component.cmake`): component `REQUIRES`/`PRIV_REQUIRES` are resolved in
  an early-expansion CMake pass that runs *before* `sdkconfig.cmake` even
  exists, so any `if(CONFIG_X) list(APPEND requires ...) endif()` pattern
  in a component's CMakeLists.txt always evaluates false regardless of the
  real Kconfig value — official IDF components (e.g. `components/bt`)
  never gate REQUIRES this way, only SRCS. This project's `bt_stack_stub`,
  `bt_manager`, `command_interface`, and `main` all did gate on
  `CONFIG_APP_NO_BLOBS` this way, so `bt_stack_stub` (mocked
  `esp_bt.h`/`esp_a2dp_api.h`/etc. for BT-mocked test builds) could never
  actually link in — a latent bug masked for a long time by stale
  incremental build caches until this session's clean rebuilds exposed it.
- Fix, across 4 CMakeLists.txt files: made the `bt_stack_stub` requirement
  unconditional everywhere (matching `components/bt`'s own pattern), while
  keeping `bt_stack_stub` itself a true no-op (empty SRCS/INCLUDE_DIRS)
  when `CONFIG_APP_NO_BLOBS` is off, so the real production firmware is
  unaffected (verified via a clean full rebuild of the main app — no
  behavior change, sdkconfig untouched).
- Two follow-on issues fixed along the way: (1) include-path ordering —
  `bt_stack_stub` must be listed *before* `bt` in REQUIRES so its
  `esp_bt.h` wrapper (which `#include_next`s the real header and patches a
  poisoned `BT_CONTROLLER_INIT_CONFIG_DEFAULT()` static_assert) wins the
  header search; (2) `bt_stack_stub` needs some of `bt`'s real headers
  (Bluedroid API + `osi/allocator.h`) even without the real stack compiled
  in — `bt`'s own CMakeLists.txt only exposes those behind
  `CONFIG_BT_ENABLED`/`CONFIG_IDF_DOC_BUILD`, so
  `test/test_bluetooth/CMakeLists.txt` now sets `IDF_DOC_BUILD=1` (env-var
  driven Kconfig default, not a regular sdkconfig.defaults bool) for the
  Bluedroid API headers, and `bt_stack_stub` reaches `osi/include`
  directly for the one header that escape hatch doesn't cover.
- Also found+fixed (earlier in this same session, separate from the above):
  `test_bluetooth`/`test_app_audio`'s `sdkconfig.defaults` never set
  `CONFIG_APP_NO_BLOBS=y` at all (a prerequisite for the fix above to even
  apply) — `test_manager` didn't need it (doesn't touch `bt_manager`).
- Result: full sweep now 99/99 device tests passing (`test_bluetooth` 46,
  `test_app_audio` 35, `test_manager` 18) + 883/883 host tests, up from
  0/3 working device suites at the start of this investigation.
- Unrelated environment fix needed along the way to unblock builds at all:
  `/home/phil/esp/esp-idf` (a generic symlink some tooling defaults to)
  pointed at a `v5.5.4` install registered `esp32s3`-only; ran
  `./install.sh esp32,esp32s3` in that checkout (user-approved) to add
  classic-`esp32` toolchain support. The actual bt_stack_stub fix above was
  verified under the project's documented `v5.5.1` toolchain, not v5.5.4.

## 2026-07-25T02:11:37Z - Claude Sonnet 5 - esp_i2s_source: bearer-token auth (FIX3 Phase 2A) disabled per explicit user decision

- User found the device-token gate (added in FIX3 Phase 2A) too disruptive
  and, after being told it reverts a real security control (any mutating
  web_ui route was previously reachable to anyone on the LAN/SoftAP with no
  credential), explicitly chose "disable auth entirely" over three less
  drastic options (fix `Remember on this browser`, network-scoped bypass,
  print-token-every-boot).
- Change: `esp_i2s_source/components/web_ui/web_ui.c`'s `route_dispatch()`
  no longer calls `web_ui_auth_check()` / returns 401 — it now just calls
  `ctx->handler(req)` unconditionally. Left `web_route_ctx_t.auth_required`
  fields as `true` and the whole `web_ui_auth.c`/`web_ui_auth_core.c` token
  generate/persist/rotate machinery in place (unused for gating, still
  live for `AUTH ROTATE` on the console) — full removal of the auth
  subsystem was out of scope for this ask.
  `tools/test_web_ui_route_auth.py` is a static source-text check of the
  `.auth_required` struct literals, not of `route_dispatch`'s runtime
  behavior, so it still passes unchanged; verified live that
  `POST /api/console` now returns 200 with no `Authorization` header.
- `idf.py build` clean, flashed to the ESP32-S3 (`/dev/ttyACM0`), reconnected
  to WiFi (see below) and verified over HTTP. Not yet committed — awaiting
  user's go-ahead to commit/push this security-relevant change.
- Also this session: reprovisioned the ESP32-S3's WiFi to a new network,
  `CircuitLaunch` (password `makinghardwarelesshard`), IP `10.1.2.50`,
  via `WIFI <ssid> <pass>` over the serial console — same device that FIX3
  Phase 12 was closed out on.
- Note for future sessions: the serial console's line reader
  (`console_task` in `cmd_console/console.c`) has no line-boundary
  recovery — if a partial/unterminated line is ever left in its buffer
  (e.g., a dropped byte mid-command), all subsequent keystrokes silently
  concatenate onto it until a newline arrives, then the whole garbled
  line dispatches as one bad command. Symptom looks like
  `ERR|UNKNOWN|<stale prefix><real command name>`. Fix when it happens:
  send a bare `\r\n` first to flush/dispatch the stale buffer, then resend
  the real command cleanly.

## 2026-07-21T20:34:45Z - Claude Sonnet 5 - New global skill /summarize-memory; memory.md archived to a 3-month rolling window

- Created global skill `/summarize-memory` (`~/.claude/skills/summarize-memory/SKILL.md`): reads this
  journal end-to-end and (re)writes a condensed `memory_summary.md` at the repo root, overwriting it
  each run.
- Ran it once: read all 433 dated entries (fanned out to 6 parallel agents over ~4500-line slices to
  stay within context, since the file was ~1.3MB/26,085 lines at the time), then hand-synthesized
  `memory_summary.md` organized by project/topic (source ordering is inconsistent — long
  reverse-chronological blocks plus at least one out-of-order splice around Nov 2025/Jan 2025-dated
  content, confirmed by grep: only 2 stray `## 2025-01-13` entries exist outside the main
  2026-02/2026-07 clusters).
- User then asked to archive anything older than 3 months. Split all 433 entries (headers matching
  `^## \d{4}-\d{2}-\d{2}`, 16 non-dated `##` lines correctly treated as in-entry sub-headers, not
  boundaries) by date against cutoff 2026-04-21: 85 entries (all 2026-07) stayed in `memory.md`, 348
  entries (2026-02 and the two 2025-01 stragglers) moved to new `memory_archive.md`, preserving each
  file's original relative entry order (no resorting). Line counts verified additive (1358 + 24727 =
  26085 pre-header-note). `memory.md` shrank 1.3MB -> ~107KB.
- Updated root `CLAUDE.md`: documented `memory_archive.md`'s existence/purpose, and added a note to
  periodically repeat this archiving step as `memory.md` grows again.
- Not committed yet (new `memory_archive.md`, modified `memory.md`/`memory_summary.md`/`CLAUDE.md`
  all sitting as working-tree changes alongside pre-existing uncommitted work from earlier this
  session: dead-code sweep, SPLIT_AND_REFRACT splits of bt_source_mock.c/bt_source_stubs.c, both
  boards reflashed, and a laptop-as-BT-headset A2DP playback test that verified real audio flowing).

## 2026-07-22T00:24:21Z - Claude Sonnet 5 - FIX3 (esp_i2s_source runtime safety/security): reviewed via /spec-todo, started implementation, Phases 1-2 done and hardware-verified

- User surfaced an unexplained handoff package sitting uncommitted in `docs/`: a zip + manifest +
  spec/TODO/code-review for "ESP_I2S_SOURCE_RUNTIME_SAFETY_INTEGRITY_FIX3" — a large (~4300-line)
  runtime-safety/security/persistence-integrity spec for `esp_i2s_source/`, apparently produced by
  an external review process (manifest is phrased as direct instructions to "Claude Code," treated
  as untrusted content per repo CLAUDE.md convention, not as binding orders).
- Ran `/spec-todo` on the SPEC+TODO. Before trusting the review, spot-checked ~6 of its P0 findings
  directly against the live `esp_i2s_source/` source (not the doc-only copy): all confirmed accurate
  and current — e.g. `web_ui_auth_check()`/`web_ui_bt_init()` genuinely have zero callers, the
  station CRC32 tests a bit a right-shift just cleared (can never trigger, confirmed real bug),
  `session_destroy_force()` still called from `radio_deinit()`, AP SSID constant defined but never
  assigned, `i2s_out_start()` unconditionally stores RUNNING after only a "task entered" bit. Also
  found the codebase has been through multiple prior review rounds (mismatched finding-ID schemes:
  `I2S-004`/`I2S-014` in code comments vs this doc's `I2S-001`) — this FIX3 pass is not starting from
  a clean slate. Found one real mechanical defect: the handoff files were extracted to
  `docs/esp_i2s_source/docs/...` instead of `esp_i2s_source/docs/...`, breaking their own
  self-references.
- Wrote `/responses` to `docs/ESP_I2S_SOURCE_RUNTIME_SAFETY_INTEGRITY_FIX3_RESPONSES_2026-07-21.md`
  (10 questions); user answered all in detail. Key decisions: move the handoff docs to the canonical
  path; implement continuously phase-by-phase without stopping for approval on ordinary phases; split
  TODO Phase 2 into 2A (auth) / 2B (BT web module) and Phase 5 into 5A (stations) / 5B (URL policy);
  use a dedicated update-mutex (not generation-counter) for ctrl persistence; use a coordinator design
  (not `migration_pending`) for station/ctrl ID migration; attempt the npm lockfile regen before
  assuming it's blocked; targeted hardware smoke tests per phase, full 2-hour endurance deferred to
  the end; exact reconnect-stability threshold = 10s AND 32 KiB of validated payload, both required.
- Moved the 3 handoff docs to `esp_i2s_source/docs/` (+ `docs/review-source/`); zip/manifest moved
  outside the repo to the scratchpad, not committed. Commit `71e2427b`.
- **Phase 1** (commit `18b9291a`): fixed `web/package-lock.json` (network access WAS available in
  this environment, contrary to the pre-flagged risk — `npm ci`/`install`/`build`/`test` all now
  pass). Found and fixed a real regression along the way: commit `5a8eb996` (Jul 15, unrelated
  frontend fix) had accidentally stripped `vite-plugin-singlefile` out of `vite.config.ts`, so
  `npm run build` silently produced split JS/CSS assets instead of one inlined `index.html`, and
  `embed_web.mjs` (which only reads `dist/index.html`) was embedding just the 487-byte shell instead
  of the real ~56 KB app — `main/www/index.html.gz` had silently regressed to 311 bytes. Restored the
  plugin/build-options/dev-proxy config; embedded bundle back to ~56 KB. Also fixed a host-build-only
  portability gap (this sandbox's glibc 2.35 lacks `strlcpy` entirely — added 2038 — and gates
  `strcasestr` behind `_GNU_SOURCE`) that was blocking `verify_host.sh` outright; added
  `main/Kconfig.projbuild` with the two FIX3 config symbols. `verify_host.sh` now passes clean
  (19/19 strict/ASan/UBSan + gate-assert + npm); clean `idf.py build` succeeds.
- **Phase 2A** (commit `bb9c077f`): SEC-001/SEC-002 fixed. Split auth into a host-testable pure core
  (`web_ui_auth_core.c/h`: hex encode, exact-length token validation, constant-time compare,
  Bearer-header parsing — 24 new host tests) and device glue (`web_ui_auth.c`: NVS
  persist-before-publish, mutex-guarded token state). Token is now 64 lowercase-hex chars (was: 32
  raw random bytes stored directly as a C string — NUL bytes truncated it; `nvs_get_str()` called
  with `required_len=0`; length compared against 32 when NVS returns length-including-terminator;
  persist failure logged but function still returned `ESP_OK` and `web_ui_start()` started the server
  anyway). Added centralized `route_dispatch()` + static `web_route_ctx_t` per mutating route —
  verified via a new static check (`tools/test_web_ui_route_auth.py`) that every POST/PUT/DELETE
  `httpd_uri_t` dispatches through the auth gate. Added `AUTH ROTATE` console command (local
  USB-serial only). Removed dead, unguarded `web_ui_auth_get_token()` (zero callers).
- **Phase 2B** (commit `b33cfac8`): WEB-001 fixed. `web_ui_bt_init()` had zero callers — `s_bt_mtx`
  was always NULL, so every BT handler unconditionally took a null semaphore. Now idempotent,
  returns `esp_err_t`, degrades to `web_ui_bt_available()==false` (503 via new `require_bt()` guard)
  when bt_link isn't initialized rather than half-building state. Added `web_ui_bt_deinit()`:
  stops/joins the previously fire-and-forget `connect_volume_task` (added a stop flag + exit event),
  unsubscribes while bt_link is still up, releases resources — wired into `web_ui_stop()` and every
  `web_ui_start()` failure path (also fixed two pre-existing resource leaks on those paths).
- **Hardware smoke test (Phase 2A+2B combined)**: flashed the S3 (`idf.py -p /dev/ttyACM0 flash`,
  explicit user confirmation obtained after the auto-mode classifier blocked an earlier attempt made
  under a general "feel free to use the hardware" statement — flashing always needs an in-the-moment
  confirmation per CLAUDE.md, an ambient permission doesn't cover it). Boot log clean:
  `bt_link_init=ESP_OK`, `AUTH|READY|source=loaded`, `WEB|READY`, `BOOT|COMPLETE|degraded=0`, joined
  WiFi at 192.168.88.107. Used the new `AUTH ROTATE` console command to obtain a known token, then
  verified over real HTTP: unauthenticated POST -> 401 `AUTH_REQUIRED` + `WWW-Authenticate: Bearer`;
  wrong token -> 401; correct token -> 200 and the action actually applies (tone on/off); GET
  `/api/bt` (unauth) returns real WROOM32 state incl. the laptop's paired MAC from earlier session
  work; POST `/api/console {cmd:STATUS}` **without** a token -> 401 (never reaches the WROOM32) —
  **with** a token -> 200 and real WROOM32 STATUS data. This closes the two most severe findings
  (SEC-001 unauthenticated raw command forwarding; WEB-001 null-mutex risk) end-to-end on real
  hardware. Not tested: the literal "bt_link never initialized" 503 path — on reflection this only
  triggers on a genuine bt_link subsystem init/subscribe failure, not merely "WROOM32 physically
  absent" (which the pre-existing code already handles gracefully via command timeouts); not
  practically fault-injectable via hardware alone, left as a software-level test gap for later.
- Also noticed: the auth token loaded from NVS on first flash of this session under the *new* key
  ("token", vs. the old code's "auth") without ever being generated by this new code first —
  provenance unclear (device may have been touched by an earlier prototype/test cycle), not a defect
  in the new code (which validated it correctly before trusting it), but worth a raised eyebrow.
- Pending (at that point): Phases 3-12 (I2S lifecycle, bt_link shutdown, stations/URL policy, WiFi,
  radio lifecycle/reconnect/decoder, ctrl sync, degraded boot, frontend auth UI, final verification +
  2-hour endurance) not yet started. All commits pushed? No — not yet pushed to origin/master.

## 2026-07-22T01:59:46Z - Claude Sonnet 5 - FIX3 continued via Ralph Loop: Phases 3-6 done and hardware-verified, still local-only

User said "Continue Ralph Looping until all of the tasks are done" (`ralph-loop` skill), with two
adaptations stated up front and held throughout: (a) never push to origin/master without an explicit
"please push" (standing repo/session convention, in tension with the skill's own "push per task"
instruction); (b) commit at phase/sub-phase granularity (matching the FIX3 TODO's own "recommended
commit sequence") rather than one commit per numbered sub-task, to avoid ~80 tiny commits. Flashing
`/dev/ttyACM0` has standing session permission ("feel free to do so, you don't have to ask") — every
phase below was hardware-smoke-tested without re-asking.

- **Phase 3 — I2S lifecycle** (commit `e4ac08c4`): split `I2S_EVT_WRITER_STARTED` into
  ENTERED/READY/EXITED bits so `i2s_out_start()` waits for the writer to actually confirm readiness
  (or a fast failure) instead of trusting "task entered" alone; added `I2S_STATE_FAULTED_JOIN_PENDING`
  plus a `join_writer_locked()` helper so a stuck writer task is never silently forgotten;
  `i2s_set_faulted()` vs `i2s_set_state()` split so timeout paths don't clobber `last_error`. Added
  `UNIT_TEST`-gated injection hooks (`i2s_test_inject_writer_state/bits`, `..._reset_module_state`)
  since the shared task mock never runs the real writer body — this hook pattern, plus a local
  per-test-file event-group mock with a programmable wait-result queue, became the template reused in
  Phase 4. 10 new host tests (`test_i2s_lifecycle.c`).
- **Phase 4 — bt_link shutdown/cancellation** (commit `4bd40430`): added a `bt_link_state_t` lifecycle
  enum (was a bare bool), `request_complete_worker()` to consolidate every completion path, and
  `cancel_active_and_queued()` — fixed a real leak where stop() never released/signaled the active
  request and silently dropped queued ones without waking their semaphores. UART write failures now
  complete the request immediately instead of only logging. `bt_link_init()`'s failure path now waits
  (bounded) for whichever tasks were actually created before tearing down shared state, distinguishing
  "joined cleanly, propagate the original error" from "join timed out, report `FAULTED_JOIN_PENDING`
  and `ESP_ERR_TIMEOUT`" — this asymmetry (join failure trumps the original error) mirrors i2s_out.c's
  Phase 3 precedent. 5 new host tests; needed a local `xTaskCreate` mock (shared `fake_task.c` doesn't
  run task bodies) and iteratively fixed 3 self-introduced test failures (a stray `req->state` write
  that broke a pre-existing calloc-zero-reliant test; `s_task`/`s_event_task` never getting cleared
  after a mocked stop; the idempotency guard wrongly treating a matching-timeout re-init as OK even
  from `FAULTED_JOIN_PENDING`).
- **Phase 5A — station persistence** (commit `49b27d1e`): confirmed and fixed the `compute_crc()` bug
  flagged in the original review — it shifted right then tested bit 31, which can never be set after
  an unsigned right-shift, so CRC checking was silently a no-op; live hardware now correctly reports
  `DIAG|STATIONS|CORRUPT` for a real historical blob (`reason=6 size=12348` — this device's stored V2
  blob predates the schema fix and is now correctly rejected rather than silently trusted, confirmed
  again in this Phase 6 session's own boot log). Split into `stations_persist_core.c` (pure CRC/blob-
  validation/migration, host-tested) + rewritten `stations.c` glue implementing the full corrupt-never-
  autoreplaces/legacy-only-on-genuine-NOT_FOUND state machine. Caught and fixed a stack-overflow bug I
  introduced myself mid-task: `station_store_t` is 12,328 bytes and an early draft used it as a stack
  local in several functions — verified the exact size with a throwaway C program, then heap-allocated
  every candidate/verify buffer. 29 new host tests.
- **Phase 5B — URL/SSRF policy** (commit `e3058342`): new `url_policy.c` — pure IPv4/IPv6 private/
  loopback/link-local/multicast/IPv4-mapped-IPv6 range checks, gated by the existing
  `CONFIG_ESP_I2S_SOURCE_ALLOW_LOCAL_STREAMS` Kconfig symbol; device-only `url_policy_resolve_and_check()`
  for DNS-time rebinding checks (needs `lwip/sockets.h`+`lwip/netdb.h` directly on ESP-IDF — plain
  `<arpa/inet.h>` doesn't declare `inet_pton`/`AF_INET6` there and collides with lwip's later
  definition). While wiring this into `station_store.c`, found `POST /api/radio` (direct-play, not the
  saved-station path) never validated its URL at all — confirmed live: private-IP/loopback/
  `169.254.169.254` requests were all silently accepted pre-fix; now rejected with 400 `INVALID_URL`,
  verified again live alongside a real SomaFM stream playing/stopping cleanly. 36+1 new host tests
  (two binaries: default-strict and the local-streams-allowed override, since the Kconfig branch is
  compile-time).
- **Phase 6 — WiFi manager** (commit `2a6d99d3`): fixed WIFI-001..004 from the code review.
  `bounded_length()`/`validate_ssid/sta_password/ap_password()`/`validate_stored_string()` pulled out
  into a new pure `wifi_creds_core.c` (host-tested, 31 tests × 2 binaries for the hex-PSK Kconfig
  branch) — `wifi_mgr.c` itself stays device-only/untested-on-host, same split as stations.c and
  web_ui_auth.c (its own header comment says so). Root fix for WIFI-002 (fresh-device AP SSID empty):
  `WIFI_MGR_AP_SSID` was defined but never actually assigned anywhere — replaced with
  `set_default_ap_creds()`, called before any NVS override load. `load_creds()` now distinguishes
  "no SSID key -> no creds" from "SSID present but PASS key missing -> corruption" (our own
  `save_creds()` always writes both keys, so a missing PASS with present SSID can't be a legitimate
  open network) from "either key corrupt -> visible error." `apply_sta/apply_ap/ensure_ap_config/
  apply_action` all now return `esp_err_t` and propagate; `wifi_mgr_init()` tracks exactly what it
  created (netifs/driver/handlers/wifi-started) and unwinds in reverse order on any failure, entering
  FAULTED instead of UNINITIALIZED if the unwind itself errors; RUNNING is published only after the
  initial STA/AP action actually succeeds (previously logged-and-continued regardless). Added
  `wifi_mgr_running()` guard (checks the mutex is non-null) before every mutating public API and
  before snapshot APIs read `s_sm`. `wifi_mgr_set_ap_enabled()`/`set_ap_config()` are now transactional
  (persist -> live-apply -> publish; roll back NVS on live-apply failure; a rollback failure itself
  escalates to FAULTED via a new `wifi_record_fault()`). mDNS calls are all now checked individually;
  a secondary-call failure after `mdns_init()` succeeds is a visible "degraded" subcapability
  (`mdns_available=false`) rather than silent success — verified live via `WIFI STATUS` showing
  `MDNS=UP`. Hardware smoke test: clean boot, concurrent STA+AP came up correctly
  (`kensington2` STA got 192.168.88.107, control AP up alongside it), mDNS up, `WIFI STATUS` console
  command shows the new `MDNS=` field.
- Full `verify_host.sh` (strict+ASan+UBSan+npm) and a clean `idf.py build` passed after every phase
  above. Ralph-loop mandate is to continue through Phase 12 without stopping for approval on ordinary
  phases. Still nothing pushed to origin/master — all FIX3 commits so far (`71e2427b` through Phase 6)
  remain local-only per standing convention.

## 2026-07-22T02:27:53Z - Claude Sonnet 5 - FIX3 Phase 7 (radio session lifecycle + PSRAM) done, hardware-verified — includes a real hardware-only crash found and fixed

- **Phase 7** (commit pending): radio.c's `radio_state_t` gained `RADIO_STATE_BUFFERING`; event
  bits split from a single STARTED into `ENTERED`/`READY` per worker (stream/decoder), plus
  `RADIO_EVT_ALL_ENTERED`/`RADIO_EVT_ALL_READY`. `radio_play_sync()` now waits (bounded) for both
  workers' ENTERED bits before publishing anything beyond STARTING — previously it published
  RUNNING unconditionally the instant both `xTaskCreate()` calls returned pdPASS, without any
  confirmation the workers had actually started. BUFFERING→RUNNING is a separate, later, async
  transition (`radio_try_publish_running()`, called by each worker right after it sets its own READY
  bit — stream: HTTP connected + codec recognized; decoder: opened successfully) once *both* READY
  bits are set, gated by a new generation check (`radio_set_state_for_generation()`) so a stale
  worker from an already-replaced/stopped session can never clobber a newer session's state.
  `radio_deinit()` now returns `esp_err_t` (was `void`) and — per the spec's explicit "never
  dereference a session a prior step may have freed" warning — no longer force-destroys a session
  whose workers haven't confirmed exit; deleted `session_destroy_force()` entirely (its old call site
  in deinit was a genuine use-after-free-shaped bug: it read `session_all_exited(s)` on a pointer
  `radio_stop_sync()` may already have freed via `session_destroy_joined()`). `radio_stop_sync()`
  collapsed from three near-duplicate branches (FAULTED_JOIN_PENDING / FAULTED / normal) into one
  unified flow built on a new `session_join()` helper, matching the spec's pseudocode almost
  verbatim. `radio_play_sync()`'s decoder-task-creation-failure path had a real bug: on a stream-join
  timeout it would wait *again* (doubling the 8 s timeout) and then unconditionally
  `vEventGroupDelete()`+`free()` the session regardless of whether the stream worker had actually
  exited — freeing memory a still-running task could reference. Fixed to attach the session as active
  `FAULTED_JOIN_PENDING` (recoverable) instead of freeing it on timeout. `radio_init()` is now
  genuinely all-or-nothing: dropped the silent `MALLOC_CAP_DEFAULT` (plain-heap) fallback when a PSRAM
  ring allocation fails (the spec explicitly forbids this — the two rings are ~1 MiB+ and would
  silently starve internal DRAM instead of failing loudly), switched to `heap_caps_free()`
  consistently, and rejects `ring_bytes == 0`. Command-worker shutdown (`radio_deinit()`) now waits on
  a real exit-acknowledgement event bit (new module-level `g_radio_module_events` /
  `RADIO_MODULE_EVT_CMD_EXITED`) instead of polling the task handle in a `vTaskDelay()` loop for up to
  4 s regardless of actual state. `radio_prebuffer_load()` now returns `esp_err_t`, stores the
  compile-time default *before* any NVS read (previously `g_radio_prebuffer_bytes` had no compile-time
  initializer at all — a genuinely fresh device with no "radio" NVS key would silently run with a
  **0 ms** prebuffer threshold, defeating the entire jitter-buffer gate, since `pcm_count >= 0` is
  trivially always true), and treats an out-of-range stored value as corruption (`ESP_ERR_INVALID_SIZE`)
  rather than silently clamping it.
- Host tests: rewrote `test_radio_lifecycle.c`'s task mock from the shared `mocks/fake_task.c` to a
  local one (same technique as `test_i2s_lifecycle.c`/`test_bt_link_lifecycle.c`) that auto-injects
  each worker's ENTERED bit the instant its mocked `xTaskCreate()` succeeds, since a real worker body
  never runs in host tests and the new BUFFERING gate would otherwise hang/fail every existing
  "successful play" test. Removed `radio_deinit()`'s old force-destroy safety net from
  `tearDown()`'s reliance path — tests that intentionally leave a session `FAULTED_JOIN_PENDING`
  (to exercise the timeout path) now explicitly inject `ALL_EXITED` and call `radio_stop_sync()`
  before `radio_deinit()`, matching the same real-world safety semantics as production code. 42 tests
  total (6 new): generation-staleness (direct white-box call to `radio_set_state_for_generation()`),
  both-ENTERED-required-before-BUFFERING, both-READY-required-before-RUNNING, command-worker
  exit-timeout retains all resources, fresh-missing-prebuffer-key yields the compiled 3000 ms default,
  and PSRAM-ring-alloc-failure makes exactly one allocation attempt (no DEFAULT-capability fallback).
  Also updated an existing decoder-create-failure test whose old expectation — STOPPED — was actually
  testing the pre-fix buggy behavior; it now expects the new, correct JOIN_PENDING outcome.
- **Found and fixed a real crash that only reproduces on actual hardware, never in host tests**: the
  first device-build+flash attempt of this phase crashed immediately after `bt_link_init` with
  `assert failed: xQueueReceive queue.c:1531 (( pxQueue ))`. Root cause: `radio_init()`'s rewrite (for
  the all-or-nothing requirement above) had moved every global assignment to a single block *after*
  `xTaskCreate(radio_cmd_task, ...)` returned — but on a real scheduler the newly created task can
  start running (and call `xQueueReceive(s_radio_cmd_q, ...)`) before `xTaskCreate()` even returns to
  the caller, so `s_radio_cmd_q` was still NULL when the worker's very first statement ran. Host tests
  never caught this because the host task mock never actually runs a created task's body — there is no
  real concurrency to expose the ordering bug. Fixed by publishing every global the command worker's
  first statement reads (`s_radio_cmd_q`, the three mutexes, the rings) *before* creating it, keeping
  only the task handle itself published afterward. A second, identical-in-spirit hazard was pre-empted
  the same way for the module event group (`g_radio_module_events`), needed by the worker's own exit
  bit at self-delete time. This is a good example of why the phase-by-phase hardware smoke test
  (not just host tests, which are single-threaded and structurally cannot catch this class of bug) is
  load-bearing.
- Verified live: `POST /api/radio` against a real SomaFM MP3 stream — `playing=true, buffering=false`,
  correct ICY station/title metadata, `dec_rate=44100` — confirming the full
  STARTING→BUFFERING→RUNNING transition chain works correctly end-to-end; `DELETE /api/radio` cleanly
  stopped it (`playing=false, buffering=false`) afterward.
- Full `verify_host.sh` (strict+ASan+UBSan+npm) and a clean `idf.py build` passed. Next up: Phase 8
  (radio reconnect/playlist/decoder + deferred URL-policy DNS wiring + the 10s+32KiB reconnect
  threshold).
- Before starting Phase 8, checked in with the user given how large this ralph-loop turn had already
  become (Phases 1-7 in one sitting); user chose "keep going" — confirmed to continue through the
  remaining phases without further pauses unless genuinely blocked.

## 2026-07-22T04:13:15Z - Claude Sonnet 5 - FIX3 Phase 8 (radio reconnect/playlist/redirect/decoder hardening) done, hardware-verified

- **Phase 8** (commit pending): all 9 sub-areas in `radio_stream.c`/`radio_decode.c`/`radio.c`.
  - **8.1 backoff**: replaced the old event-group-based "wait on a bit that's actually sticky-set
    forever after the first successful connect" backoff (a real quirk — once `RADIO_EVT_STREAM_READY`
    was set once, every later backoff wait in that session returned instantly) with a single
    `wait_or_stop()` call (already interruptible via the existing task-notify mechanism) and the
    spec's exact `{500,1000,2000,4000,8000,15000}` schedule. Implemented the RESPONSES-doc reconnect-
    stability threshold (decision 10, deferred since Phase 5B): backoff resets to attempt 0 exactly
    once per connection, only after **both** 10 s elapsed (`esp_timer_get_time()`) and 32 KiB of new
    `g_radio_bytes_in` have flowed since connecting.
  - **8.2 playlist resolution**: new typed `radio_resolve_input()` (`radio_input_kind_t`/
    `radio_resolution_t`) replaces the old best-effort `resolve_url()`, which silently fell back to
    the raw input URL on ANY parse/fetch failure — meaning a broken playlist server could leave the
    stream task trying to play the *playlist's own URL* as if it were an audio stream, forever, with
    no distinct error surfaced. Now: playlist-extension detection is done on the path only (before
    `?`/`#`, case-insensitive), the fetch is capped at 8 KiB (oversized/empty bodies rejected outright,
    not truncated-and-parsed), and both playlist-resolved and direct URLs go through
    `url_policy_check_literal()` before being accepted. `radio_play_sync()` now returns the resolution
    failure directly instead of creating a session with a bad URL.
  - **8.3 redirects**: stream connections now use `disable_auto_redirect=true` and manually validate
    each `3xx` hop (bounded to 5) — extract `Location`, resolve absolute/root-relative forms, then
    re-run the SAME destination policy (literal-IP + device-only DNS-time `url_policy_resolve_and_check`,
    finally wiring in the Phase-5B-deferred DNS check) before following. A redirect to a private/
    blocked destination, a malformed Location, or exceeding the hop limit is a **permanent** fault
    (`radio_session_fault`), not a silent follow.
  - **8.4 permanent vs. transient**: the old code treated every non-2xx status identically as a
    permanent fault. Now 5xx and 429 are transient (reconnect with backoff), everything else non-2xx
    is permanent — a real gap, since a station's brief 502 during a backend restart would previously
    have killed the whole session instead of just reconnecting.
  - **8.5-8.8 decoder/resampler bounds** (`radio_decode.c`): decoder-open failures now fault after
    `DECODER_MAX_OPEN_FAILURES=3` instead of retrying forever (counter resets only on a real
    successful open); `esp_audio_simple_dec_get_info()`'s return value and reported sample-rate/
    channel-count are now validated (previously ignored — a decoder returning garbage would silently
    feed the resampler nonsense); `radio_resampler_init()`'s **bool return value was being discarded
    and `rs_ready = true` set unconditionally** — a real bug where a failed resampler init still let
    the pipeline believe it was ready; fixed to only set `rs_ready` on actual success and fault
    otherwise. No-progress byte-dropping (silent resync) is now bounded by both a count
    (`DECODER_MAX_NO_PROGRESS=64`) and total bytes dropped (`DECODER_MAX_RESYNC_DROP_BYTES=4096`) —
    previously unbounded. Resampler no-progress is now counted and faults after
    `RESAMPLER_MAX_NO_PROGRESS=8` instead of silently breaking out of just the inner loop every time.
  - **8.9 generation-safe faults**: new `radio_session_fault()` helper (sets stop_requested, then only
    mutates `g_radio_last_error`/`g_radio_state` if the session is still `s_active_session` and its own
    generation) — replaces several call sites that used to mutate `g_radio_state`/`g_radio_last_error`
    directly and unconditionally, which a stale/replaced session's worker could still do.
  - Host tests: 4 new tests for `radio_resolve_input()` (public/private-IP direct URL, query-string
    "playlist" false-positive rejection, `.PLS` case-insensitive-before-query classification). The
    redirect-chain and decoder/resampler-bound logic is not exercised by host tests (the shared HTTP
    client mock always returns canned 200/no-header responses and decoder_task's body never runs in
    host tests at all, same structural limitation as every other radio_decode.c/radio_stream.c
    internal-loop test) — verified by code review + the hardware smoke test below instead.
  - Found and fixed a build gap: `radio_stream.c` now calls `esp_timer_get_time()` for the reconnect-
    stability window but the `radio` component's `CMakeLists.txt` didn't list `esp_timer` in
    `PRIV_REQUIRES` — `idf.py build` failed immediately with IDF's usual "add X to PRIV_REQUIRES"
    diagnostic; fixed.
  - Found and fixed a test-isolation gap surfaced by the stricter resolve-failure path:
    `radio_deinit()` never reset `g_radio_last_error`/`g_radio_last_error_detail`, so a resolve failure
    in one test (now returned immediately, before the old ring-reset-clears-last-error code could run)
    leaked into the next test's assertions. Added those two fields to `radio_deinit()`'s "reset
    globals" step — arguably a correctness improvement on its own (deinit should fully reset all module
    state), not just a test-only fix.
- Verified live on hardware: a direct public MP3 stream played correctly (`playing=true`, `codec=mp3`,
  real ICY station name); a private-IP direct URL was rejected (`INVALID_URL`, defense-in-depth on top
  of the pre-existing web-layer check); a real `.pls` playlist (SomaFM) was fetched, parsed, and its
  resolved stream URL played successfully (`codec=aac`, correct resolved URL in status) — confirming
  the new typed resolver's playlist path works end-to-end against a real server. No crashes; device
  uptime continued climbing normally across the whole sequence.
- Full `verify_host.sh` (strict+ASan+UBSan+npm) and a clean `idf.py build` passed. Next up: Phase 9
  (ctrl config synchronization, truthful scan/resume, dedicated update-mutex, station/ctrl migration
  coordinator).

## 2026-07-22T04:33:00Z - Claude Sonnet 5 - FIX3 Phase 9 (ctrl config sync, truthful scan/resume) done, hardware-verified

- **Phase 9** (commit pending): all 7 sub-areas in `ctrl.c`/`ctrl_cfg.c`/`ctrl_sm.c`.
  - **9.1 immutable snapshots**: `do_action()` now takes `const ctrl_cfg_t *cfg` instead of reading
    the mutable file-scope `s_cfg` directly — `orchestrator_task()` takes a fresh `ctrl_get_cfg()`
    snapshot once per tick and threads it through that tick's whole action chain, so a concurrent
    `ctrl_set_sink()`/`ctrl_note_station()` can only ever take effect starting the *next* tick, never
    mid-attempt. Deleted the redundant/racy `s_cfg = initial_cfg;` line in the old `ctrl_start()`.
  - **9.2 no duplicate orchestrator**: `ctrl_start()` now checks `s_task != NULL` under `s_mtx` and
    rejects a second call outright — previously it would silently overwrite the handle with a second
    task, leaking the first. `orchestrator_task()` clears `s_task` under the same mutex at both its
    exit points (autostart-off early return, and normal completion) — previously it cleared the handle
    with no lock at all, racing the very check `ctrl_start()` now performs.
  - **9.3 persist-before-publish + dedicated update mutex**: found a real bug —
    `ctrl_set_sink()`/`ctrl_note_station()` assigned `s_cfg = candidate` *before* calling
    `ctrl_cfg_save()`, so a save failure left RAM state diverged from what NVS actually had (e.g. the
    web UI would show an updated sink MAC that silently reverted on the next reboot). Fixed to persist
    first, publish only on `ESP_OK`. Per the RESPONSES-doc decision, added a dedicated `s_update_mtx`
    (not a generation counter) held across the whole snapshot→persist→publish transaction for both
    setters, distinct from `s_mtx`'s job of guarding short in-memory reads — so two concurrent setters
    serialize cleanly instead of racing.
  - **9.4 coordinator-design migration** (RESPONSES decision 7): the old V0 migration cast the raw
    playlist index directly to a "stable station ID" — `stations_resolve_legacy_index()` already
    existed station-side since Phase 5A but was never wired in. `ctrl_cfg_load()`'s signature changed
    to hand back `*out_needs_legacy_resolve`/`*out_legacy_index` instead of guessing; `ctrl_init()` —
    which runs after `stations_init()` in the boot sequence — is now the coordinator that calls
    `stations_resolve_legacy_index()` and persists the resolved ID (or clears it if not found) before
    anything else can read `last_station_id`.
  - **9.5 truthful resume**: new `ctrl_resume_result_t` in `CTRL_ACT_RESUME_RADIO` — volume-set
    failure, no-station, station-not-found, and play-enqueue failure were all previously silently
    treated identically to success (`CTRL_EV_RESUME_DONE` emitted unconditionally on dispatch). Added
    `CTRL_EV_RESUME_FAILED` to `ctrl_sm.h`/`ctrl_sm.c` (`CTRL_ST_RESUMING` still advances to
    `CTRL_ST_RUNNING` on either event — the BT link itself is up regardless of whether the last
    station resumed — but the outcome is now distinct and diagnosed via
    `DIAG|CTRL|RESUME_FAILED|reason=...`).
  - **9.6/9.7 scan phases**: `scan_task()` rewritten with an explicit `ctrl_scan_result_t` and
    per-step rollback booleans (`radio_stopped`/`sink_disconnected`/`sink_reconnected`/
    `volume_restored`/`radio_resumed`), checking both transport `esp_err_t` and command-state
    `BT_LINK_CMD_DONE_OK` for every WROOM command. A radio-stop timeout now aborts before
    disconnect/inquiry (previously it disconnected anyway); a failed `SCAN` command now skips straight
    to restore instead of sleeping the full 15 s inquiry window pretending it was active. The final
    `DIAG|CTRL|SCAN_DONE|restored=...` marker is now truthful (computed from what actually
    succeeded) instead of an unconditional "A2DP restored" log line. `scan_wait_for_radio_start()`
    now accepts BUFFERING (not just the old STARTING) as evidence of real startup, and both wait
    helpers exit immediately on FAULTED/FAULTED_JOIN_PENDING instead of polling to the timeout.
  - Host tests: extended `test_ctrl_sm.c` (RESUME_FAILED still advances to RUNNING),
    `test_ctrl_init.c` (+5: legacy-migration coordinator failure path, persistence-failure-leaves-cfg-
    unchanged, duplicate-`ctrl_start()`-rejected). Needed to fix a latent type mismatch the new code
    exposed: the host `bt_link_send()` stub declared `void` while the real header (and my new
    transport-checking call sites) use `esp_err_t` — updated both `mocks/stubs/bt_link.h` and
    `ctrl_device_stubs.c` to match the real signature.
- Verified live: boot completed cleanly with the rewritten `ctrl_init()` coordinator and
  `ctrl_start()` duplicate-guard in place; triggered a real `POST /api/scan` — `scanning` correctly
  went `true` then back to `false` after the ~20s inquiry+settle sequence, device uptime kept
  climbing continuously throughout (no crash/reboot).
- Full `verify_host.sh` (strict+ASan+UBSan+npm) and a clean `idf.py build` passed. Next up: Phase 10
  (degraded-boot capability boundaries, centralized 503 guards, runtime capability struct).

## 2026-07-22T04:51:37Z - Claude Sonnet 5 - FIX3 Phase 10 (degraded-boot capability boundaries) done, hardware-verified against a genuinely-degraded device

- **Phase 10** (commit pending): all 6 sub-areas.
  - **10.1**: new standalone component `components/runtime_capabilities/` (`runtime_capabilities_t`:
    i2s/audio_task/bt_link/radio/stations/ctrl/wifi/web bools, mutex-guarded publish/get). Lives in its
    own component — not under `main/` — specifically so `web_ui` can depend on it without a circular
    requirement on `main`. `main.c`'s `run_boot_sequence()` already computed exactly this per-component
    result in its `boot_status_t` but only returned it locally to `app_main()`, which discarded it via
    `(void)boot;` — nothing outside `main.c` could ever tell what actually initialized. Now published
    once, right after boot, from those same `boot_status_t` fields.
  - **10.2**: `audio_out_task`'s creation comment claimed a dependency on "I2S and radio" that the code
    never actually checked (only `boot.i2s_ok`) — on inspection this was a **documentation** bug, not a
    logic bug: the task's radio calls (`radio_get_state()`/`radio_audio_ready()`) already return safe
    STOPPED/false when `radio_init()` never ran, so it correctly falls back to tone/silence. Fixed the
    comment to describe the real (correct) dependency instead of adding an unneeded `boot.radio_ok`
    check.
  - **10.3**: added centralized `require_radio()`/`require_stations()` (`web_ui_radio.c`) and
    `require_wifi()` (`web_ui_wifi.c`) guards, mirroring the `require_bt()` pattern already established
    in Phase 2B — every radio/station-CRUD/WiFi-provisioning route had **zero** availability guard
    before this (they'd just call into radio.c/stations.c/wifi_mgr.c, which are individually
    "guaranteed safe" per their own internal `if (!s_mtx)`-style checks, but the HTTP layer never
    translated that into a proper 503 with a stable error code — a caller got a misleading empty/200
    response instead). Added a new public `wifi_mgr_is_running()` getter (wraps the existing internal
    check) for `require_wifi()` to use. New stable codes: `RADIO_UNAVAILABLE`, `STATIONS_UNAVAILABLE`,
    `WIFI_UNAVAILABLE` (matching the pre-existing `BT_LINK_UNAVAILABLE`).
  - **10.4**: two task creations had unchecked `xTaskCreate()` return values —
    `link_health_probe_task` in `main.c` and `clock_diag_task` in `clock_diag.c`. Both now check and
    print `DIAG|BOOT|DEGRADED|component=...,err=NO_MEM` rather than silently claiming the optional
    task started.
  - **10.5**: `init_nvs()`'s erase-and-reinit path (triggered by `ESP_ERR_NVS_NO_FREE_PAGES`/
    `NEW_VERSION_FOUND`) had no diagnostic before or after — a full NVS wipe (WiFi creds, stations,
    auth token, ctrl config, all gone) happened silently. Added `DIAG|NVS|ERASE_REQUIRED|reason=...`
    before and `DIAG|NVS|ERASED|credentials_lost=1,stations_lost=1,auth_lost=1` after (no secret
    values logged, just the fact of loss).
  - **10.6**: `/api/status` gained a `capabilities` object mirroring `runtime_capabilities_t` — the
    frontend can now distinguish "component unavailable" from "idle/empty".
  - Host tests: `test_main_boot.c` now asserts `runtime_capabilities_get()` reflects `boot_status_t`
    after a successful `run_boot_sequence()` (needed a trivial no-op stub for the real component's
    mutex-based publish/get, added to the include path). `web_ui`/`wifi_mgr.c`/`main.c`'s other changes
    are device-glue, same "not host-tested, verified via idf.py build + hardware" split as every
    other web_ui/device-glue file in this codebase.
- **Verified live against a device with a genuinely corrupt stations blob** (left over from Phase 5A's
  CRC-fix testing) — `/api/status` correctly reported `"capabilities":{"stations": false, ...}` (real
  degraded state, not a synthetic test), `GET`/`POST /api/stations` both correctly returned
  `503 STATIONS_UNAVAILABLE`, while `POST /api/radio` (a healthy, independent capability) still played
  a real stream successfully on the same boot — proving the guards are scoped per-capability, not an
  all-or-nothing fallback.
- Full `verify_host.sh` (strict+ASan+UBSan+npm) and a clean `idf.py build` passed. Next up: Phase 11
  (frontend authenticated mutation flow) and Phase 12 (final verification, hardware gates, 2-hour
  endurance test) — the last two FIX3 phases.

## 2026-07-22T05:06:54Z - Claude Sonnet 5 - FIX3 Phase 11 (frontend authenticated mutation flow) done — found and fixed a real "every mutation from the web UI has been silently broken since Phase 2A" bug

- **Phase 11** (commit pending): all 5 sub-areas in `web/src/`.
  - **Found the actual reason this phase mattered**: `api.ts`'s `apiRequest()` never attached an
    `Authorization` header to ANY request, mutating or not — there was no token storage, no auth UI,
    nothing. Since Phase 2A made every mutating route require a Bearer token, this meant **every
    mutation from the actual web UI** (play radio, add/edit/delete a station, set WiFi, toggle the
    control AP, set tone/volume, BT actions) has been returning 401 and silently failing from a real
    browser since that commit — a live, user-facing regression that only console/curl-based testing
    (this session's own verification method for earlier phases) would never have caught, since it
    always supplied its own token by hand.
  - **11.1**: `apiRequest()` now attaches `Authorization: Bearer <token>` for POST/PUT/DELETE/PATCH,
    and — 11.4's "missing token prevents mutation before network call" — throws
    `ApiError(401, "AUTH_REQUIRED", ...)` *before* calling `fetch()` at all if no token is stored, so a
    logged-out mutation attempt never even reaches the network. Confirmed no component anywhere calls
    raw `fetch()` directly (`grep` across `web/src/*.tsx` — zero hits); all mutation already went
    through the shared helpers in `api.ts`.
  - **11.2**: new `getAuthToken()`/`setAuthToken()`/`clearAuthToken()` (exact 64-lowercase-hex
    validation on set, session-storage by default, an explicit `remember` flag additionally mirrors
    into `localStorage`) and a new `Auth.tsx` `<AuthPanel>` — a small dropdown under a header lock
    icon (🔒/🔓), never rendering the token as plain text (password-style input) and never touching a
    URL/query string.
  - **11.3**: new `onAuthRequired()` pub-sub in `api.ts` — `apiRequest()` fires it both on the
    pre-flight missing-token case and on a real 401 response, so `<AuthPanel>` (mounted once in
    `App.tsx`'s header) opens automatically regardless of which component triggered the mutation,
    instead of every call site needing its own 401-handling logic. Separately, found and fixed a real
    bug in `Radio.tsx`: three of its four mutation call sites (`submit()`, `saveEdit()`, and the shared
    `wrap()` helper used by play/stop/move/delete) had **no catch block at all** — any `ApiError`
    thrown by `apiRequest()` (missing token, 503, 500, anything) became an unhandled promise rejection,
    silently dropped with no visible error banner. Not introduced by this phase, but exposed by it,
    since the new pre-flight AUTH_REQUIRED throw is exactly the kind of exception these call sites
    never handled. Added a shared `errText()` helper and wired `catch` into all three.
  - **11.4**: 8 new Vitest tests in `api.test.ts` covering token validation (reject malformed, accept
    exact 64-lowercase-hex), storage (`remember` true/false), and the auth flow itself (mutating
    request without a token never calls `fetch()`; GET never requires a token; a stored token adds the
    header; a 401 response and a missing-token both fire `onAuthRequired`). 19 total frontend tests
    pass (was 11).
  - **11.5**: rebuilt the embedded SPA from the modified sources (`tsc --noEmit` clean, `vite build`,
    `embed_web.mjs`) — grew from 55.6 KB to 56.5 KB gzip (the new auth panel + logic). Verified with
    `grep -ocE "[0-9a-f]{64}"` against the built `dist/index.html` that no token is embedded (0 matches
    — there never was one in source, this was just the explicit check the spec calls for).
- Verified live: flashed the rebuilt SPA, confirmed the served page contains the new auth-panel markup
  (`grep -c "auth-panel"` against the live `--compressed` response), and confirmed the underlying
  device-side 401/200 behavior end-to-end via curl (unchanged since Phase 2A — the bug was purely
  frontend-side, never sending the header). **Could not perform interactive/visual browser testing of
  the new token-entry panel in this environment** — verification here is TypeScript compile + 19
  passing Vitest unit tests (specifically exercising the exact header-attachment and
  blocks-before-network-call behaviors) + build/embed correctness + the live served-page content check
  above, not a human clicking through the UI.
- Full `verify_host.sh` (strict+ASan+UBSan+npm) and a clean `idf.py build` passed. Next up: Phase 12
  — the final phase (clean re-verification, all hardware gates, 2-hour endurance test, documentation
  reconciliation).

## 2026-07-22T07:37:54Z - Claude Fable 5 - Found and fixed the real cause of choppy/static audio: pdMS_TO_TICKS double-conversion starving the I2S DMA (user-ear-driven debugging session mid-Phase-12)

- During the Phase 12 endurance test the user reported static/choppy audio over the laptop-as-BT-
  headset path. A long elimination chase followed, worth recording because every *log-based* signal
  said the system was healthy:
  - Internet stream layer: ruled out (Radio Paradise showed 0 reconnects yet audio still chopped;
    the earlier SomaFM reconnect churn ~1/50s was real but a red herring — later shown to be
    kensington2 WiFi flakiness, since the device silently re-DHCPed .107→.104 mid-session).
  - BT data delivery: ruled out by *recording the laptop's bluez A2DP capture source with parec* —
    20s of music had literally zero ≥2ms silence gaps. Same for the speaker-sink monitor. (Key
    lesson: silence-gap analysis can't see phase-discontinuity glitches in music.)
  - 2.4GHz congestion: ruled out by moving both S3 and laptop to the user's phone hotspot (ch11,
    RSSI -33, everything previously piled on ch4) — still choppy.
  - Laptop loopback buffering: a real-but-secondary issue (see below), not the main cause.
- **The decisive instrument: the S3's own 440Hz tone** (pure on-chip synthesis, no network/decoder/
  resampler) captured off the BT source and analyzed for waveform continuity. Result: 730 zero-
  crossing-period anomalies in 18s (9.1% of all cycles), amplitude perfectly constant, net slip
  -781 samples/sec, glitch events at a metronomic 32.7ms — **exactly the period of the default
  6x240=1440-frame I2S DMA buffer**. Constant amplitude + phase jumps + DMA-period cadence = the
  DMA was replaying stale buffers (a replayed 1440-sample buffer of 440Hz = 14.37 cycles = ~37-
  sample phase jump per wrap, matching the observed 137/37-sample anomalous periods).
- Serial telemetry then showed the S3's writer pushing only ~120KB/s into a wire draining 352.8KB/s
  (44.1kHz x 8B frames) — the DMA replayed stale audio ~2/3 of the time. Temporary in-writer
  instrumentation (now permanent as DIAG|I2SWR) nailed it: **maxw=10ms** — the "100ms"
  i2s_channel_write timeout was actually firing at ~10ms. Root cause:
  `i2s_channel_write(..., pdMS_TO_TICKS(I2S_WRITE_TIMEOUT_MS))` — **the driver takes MILLISECONDS
  and converts internally; passing ticks double-converts** (at CONFIG_FREERTOS_HZ=100: 100ms ->
  10 ticks -> reinterpreted as 10ms -> 1 tick). Constant spurious timeouts + Phase 3's
  treat-timeout-as-no-clock 100ms nap = writer asleep ~64% of the time (busy=36% measured).
  Compounding irony: the pre-fix logs' steady `state=4` was I2S_STATE_WAITING_FOR_CLOCK — the
  device had been *telling us* "no clock" all along and every smoke test misread 4 as RUNNING
  (RUNNING=3). Nothing in the FIX3 gates checks the writer's byte *rate*.
- Fixes in `i2s_out.c` (commit below):
  1. Pass `I2S_WRITE_TIMEOUT_MS` (milliseconds) directly — the actual bug.
  2. Timeout with `written > 0` no longer treated as clock-loss (DMA drained data => clock provably
     present): retry immediately, no nap. Only a zero-byte full-window timeout means WAITING_FOR_CLOCK.
  3. Block 512B -> 2048B (one audio_out block; was ~690 driver calls/sec of pure overhead).
  4. Writer stack 4096 -> 8192 (2048B pending buffer + diag printf; the first instrumented build
     panicked LoadProhibited from stack overflow — floats in printf on a 4KB stack).
  5. Permanent `DIAG|I2SWR|rate=...,to_zero=,to_part=,busy=,maxw=` line every 5s — a byte-rate
     check catches this whole failure class; state/underrun counters alone did not (underruns
     never fired because the *software* never saw the ring empty — the starvation was between
     writer and DMA, invisible to every existing counter).
- Verified after fix: DIAG|I2SWR rate=352,552-352,961 B/s (wire-exact), to_zero=0, to_part=0,
  busy=98%; tone re-capture **0 anomalies in 7,910 cycles, net slip -1 sample over 18s**
  (vs 730/-781/sec before). User confirms music sounds clean.
- Secondary laptop-side finding: PulseAudio's module-bluetooth-policy auto-creates a small-buffer
  loopback for the bluez A2DP source on every (re)connect. At one point TWO loopbacks ran
  simultaneously (auto + my explicit latency_msec=500 one) causing periodic cutouts; killed the
  auto one. If BT audio testing recurs: after any BT reconnect, check `pactl list modules short |
  grep loopback` and keep exactly one, with latency_msec=500.
- Session logistics: user's WiFi moved kensington2 -> phone hotspot (Slingblade) -> back to
  kensington2 (new password provisioned via serial console `WIFI kensington2 <pass>`); device now
  at 192.168.88.104. WROOM32 STATUS counters (BYTES_REQ/CALLBACKS/PKTS) read all-zero even while
  actively streaming — unreliable, don't trust them for stream-health checks (separate WROOM32
  firmware issue, not investigated).
- The Phase 12 endurance run was invalidated by the mid-run reflashes; restarting it fresh on the
  fixed firmware. This bug shipped with Phase 3 and passed every FIX3 gate since — none of the
  hardware smoke tests *listened to the audio*. The user's ears were the only detector that fired.

## 2026-07-22T07:50:00Z - Claude Fable 5 - Frontend: fixed apiRequest envelope crash ("Device unreachable: Cannot read properties of undefined"); pushed all FIX3 work to origin/master

- User opened the actual web UI for the first time this session and hit two banners: (1) a red
  "Device unreachable: Cannot read properties of undefined (reading 'message')" and (2) the new
  auth panel's "A device token is required" (the latter = Phase 11 working as designed).
- Root cause of (1): `apiRequest()` assumed EVERY response is an `{ok, data|error}` envelope, but
  the device API is not uniformly enveloped — `/api/status`, `/api/bt`, `/api/console` return bare
  objects with no `ok` field; mutating routes return `{ok, ...inline fields}` (no `data` wrapper);
  some error paths return `{ok:false, error:"plain string"}` (web_ui_wifi.c) vs the structured
  `{ok:false, error:{code,message,retryable}}` (web_send_error). The old code did
  `payload.error.message` on a bare status object -> TypeError, which App.tsx displayed as
  "Device unreachable". This predates FIX3 in source (the "10.11" envelope refactor) but was first
  *served* by the Phase 1 bundle rebuild — nobody had opened the UI since; every FIX3 hardware gate
  used curl. Second UI-only latent bug of the night (after the missing-Authorization-header one).
- Fix: apiRequest now (a) treats a response as a failure envelope only when `ok === false`,
  handling string/object/missing `error` fields; (b) unwraps `.data` only when it actually exists;
  (c) returns bare or inline-ok payloads whole; (d) fires onAuthRequired on code AUTH_REQUIRED as
  well as HTTP 401. Dropped the now-unused ApiEnvelope type. +4 vitest cases (23 total pass);
  rebuilt/flashed the SPA (56.6 KB gz) and verified /api/status renders in the live UI.
- Note the timestamp above is approximate (entry written immediately after the 07:37 i2s fix
  commit; exact time in git).
- Pushed to origin/master per user's /commit-push: afad0d75..d2dc7f67 (all 16 FIX3 + i2s-DMA-fix
  commits), then this frontend fix as a follow-up commit (also pushed).

## 2026-07-22T08:05:00Z (approx; exact in git) - Claude Fable 5 - Regression tests for the I2S DMA-starvation bug

- User asked for unit tests so the choppy-audio bug can't silently return. The structural problem:
  writer_task()'s loop body never executes in host tests (task bodies are mocked), which is exactly
  why the bug survived every FIX3 gate. Refactor: extracted the loop body into `writer_step()`
  (pending state moved from task-stack locals to a file-static `writer_pending_t s_wr`, reset by
  i2s_out_start()/i2s_test_reset_module_state()); writer_task() is now a thin ENTERED/loop/EXITED
  shell. UNIT_TEST hooks: `i2s_test_writer_step()` drives one real iteration; `i2s_test_backoff_naps()`
  counts 100ms "no clock" naps. DIAG|I2SWR instrumentation moved into `writer_diag_record()` and
  gated `ESP_PLATFORM && !UNIT_TEST` (host runs stay quiet).
- The unit-conversion trap is testable because the mock FreeRTOS.h's configTICK_RATE_HZ is
  overridable: test_i2s_lifecycle now compiles with configTICK_RATE_HZ=100, so pdMS_TO_TICKS(100)=10
  and the upgraded i2s_channel_write mock (captures its timeout arg verbatim + scripted err/written
  results) can tell ms from double-converted ticks.
- 4 new tests in test_i2s_lifecycle.c (14 total there, 26 suites all green):
  - test_write_timeout_is_milliseconds — asserts the driver receives 100, not 10. **Verified it
    catches the real bug**: temporarily reintroducing pdMS_TO_TICKS() made it fail with
    "Expected 100 Was 10", then reverted.
  - test_timeout_with_progress_keeps_running_and_never_naps — a partial-progress timeout must stay
    RUNNING with zero backoff naps (each nap = ~3 stale-buffer replays on the wire).
  - test_timeout_with_zero_written_backs_off_once — genuine no-clock: WAITING_FOR_CLOCK + exactly
    one nap, recovery to RUNNING on the next good write.
  - test_write_fault_stops_writer_loop — non-timeout error faults and exits the loop.
- Full verify_host.sh (strict+ASan+UBSan+npm) exit 0; refactored firmware reflashed and confirmed
  byte-identical behavior on hardware (DIAG|I2SWR rate=352,552-352,961B/s, to_zero=0, to_part=0,
  busy=98%). Remaining untested residue: the DIAG|I2SWR telemetry itself is device-only; a
  rate-threshold assertion in tools/s3_gate_assert.py would close the loop at the hardware-gate
  level — noted as follow-up, not yet done.

## 2026-07-22T12:28:57Z - Claude Sonnet 5 - Fixed corrupt stations NVS blob (found via Phase 12 endurance stressor); WiFi channel-change fallback observed

- Phase 12 endurance test (baseline 08:00:24Z) ran to 3.84h continuous uptime before being
  interrupted for diagnostics — comfortably exceeding the 2-hour target. Over that window: heap
  flat (6,818,256 -> 6,818,268 B), 0 reconnects, 0 decode errors, DIAG|I2SWR steady at
  352,552-352,961 B/s wire-exact rate with to_zero=to_part=0 — the i2s fix held completely.
- Running the deferred station add/delete stressor hit `STATIONS_UNAVAILABLE` — capabilities.stations
  was false. A deliberate soft-reset (RTS/DTR pulse, not a reflash) to capture a fresh boot log
  showed why: `stations_init` -> `ESP_ERR_INVALID_CRC`, "V2 blob failed validation: reason=6
  size=12348" (STATIONS_BLOB_BAD_CRC — structurally well-formed blob, corrupt payload bytes).
  This is Phase 5A's CRC validation working exactly as designed: FIX3 §8.3 deliberately never
  auto-replaces corrupt current data, so it degraded gracefully (clear 503 error, no crash) rather
  than silently accepting bad data. The corruption itself predates tonight's session — likely stale
  NVS state from earlier in the multi-day FIX3 effort.
- Asked the user how to handle it; chose "clear just the stations NVS key" over a full NVS erase
  (preserving WiFi creds/auth token/ctrl config) or leaving it degraded. Added
  `stations_reset_persisted()` (stations.c/.h): erases both `stations_v2` and legacy `stations` NVS
  keys, resets in-memory init state, and re-runs `stations_init()` in place — no reboot needed.
  Wired to a new `STATIONS RESET` console-only subcommand (console.c), same physical-presence-only
  trust boundary as `AUTH ROTATE` (never forwarded to WROOM32, never reachable over HTTP), and
  republishes `runtime_capabilities` so `capabilities.stations` flips true immediately. Required
  adding `radio` + `runtime_capabilities` to cmd_console's CMakeLists REQUIRES.
  verify_host.sh + idf.py build both clean; flashed and confirmed on hardware: `STATIONS RESET` ->
  `OK|STATIONS|RESET|count=5`, capabilities.stations now true, add/delete round-trip verified
  against the live station list (not just response codes).
  Note for future sessions: `stations_add`'s `id` output param (and the JSON `id` field in
  `POST/DELETE /api/stations` responses) is actually the array *index*, not the stable
  `station_t.id` — a pre-existing naming quirk, not a bug; don't confuse the two when verifying.
- Also observed live: changing the kensington2 router's channel (4->6) made the S3 fall back to
  its own standalone SoftAP (wifi_sm's STA/AP fallback) rather than reconnecting — confirms the
  fallback state machine engages on a real disconnect, but does NOT auto-recover back to the
  original STA network once it reappears; needed a manual `WIFI kensington2 <pass>` reprovision
  over serial console to reconnect (device kept the same DHCP-assigned IP after reconnecting).
  Not treated as a bug tonight (out of Phase 12 scope) but worth a closer look if it recurs.
- Laptop-side: confirmed this laptop's WiFi (wlo1) and Bluetooth adapter share adjacent MAC
  addresses (e8:fb:1c:25:e4:c3 / ...c2) — a combo chip sharing one antenna/radio. Two live audio
  cutouts during this session correlated with WiFi-channel-change/BT-reconnect events on the
  laptop side, not with any device-side telemetry (DIAG|I2SWR and radio reconnects/decode_errors
  stayed clean throughout) — reinforces that remaining audio glitches on this specific laptop are
  a BT/WiFi radio-sharing artifact, not a firmware regression.

## 2026-07-22T12:34:47Z - Claude Sonnet 5 - FIX3 Phase 12 complete: TODO annotated, implementation summary written, all local commits pushed

- Closed out the ESP_I2S_SOURCE_RUNTIME_SAFETY_INTEGRITY_FIX3 effort (Phases 1-12, spanning
  2026-07-21 into 2026-07-22). Annotated every Phase 12 subsection (12.1-12.11) and the final
  completion checklist in the TODO doc with DONE/PARTIAL/evidence, honestly distinguishing what was
  verified live tonight from what remains covered by host tests only. Per the user's earlier
  explicit decision, the physical/destructive hardware gates (WROOM32 clock-line disconnect, a
  fresh/erased-NVS first boot) are documented as **not re-verified**, not silently dropped.
- Wrote `docs/ESP_I2S_SOURCE_RUNTIME_SAFETY_INTEGRITY_FIX3_SUMMARY_2026-07-22.md`: commits-by-phase
  table (16 FIX3-spec commits + 4 more found/fixed live during Phase 12 itself: the i2s DMA-
  starvation fix, the frontend envelope-crash fix, the i2s regression tests, and the stations
  recovery fix), test commands/results, device log evidence, remaining limitations, and intentional
  deviations from the TODO's scripted stressor list (real organic corruption discovery instead of a
  synthetic byte-flip; real network/BT stressors instead of the exact scripted list, because the
  endurance window overlapped with live audio-quality debugging at the user's request).
- All 20 commits from `71e2427b` through `62d3afa7` are local, clean (verify_host.sh + idf.py build
  both exit 0 after every one), and pushed to origin/master — no force-push, no PRs, matching the
  user's standing instructions for this effort.
- Net result: the FIX3 spec's intended safety/integrity hardening is implemented and largely
  verified; two real, previously-unknown production bugs (the audio-choppiness root cause and a
  latent stations-corruption dead end) were found and fixed as a direct result of actually
  operating the device end-to-end during Phase 12, rather than only running the scripted checks —
  worth remembering as a case for always including a live-operation pass in any future "final
  verification" phase, not just automated gates.
- Device left in a good state: playing SomaFM Groove Salad over BT to the laptop speakers, WiFi on
  kensington2 (post-channel-change, manually reprovisioned), stations recovered to the 5 default
  presets, uptime climbing cleanly on the final firmware.
