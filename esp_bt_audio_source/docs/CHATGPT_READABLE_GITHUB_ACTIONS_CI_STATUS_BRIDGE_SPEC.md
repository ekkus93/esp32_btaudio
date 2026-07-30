# ChatGPT-Readable GitHub Actions CI Status Bridge

## Reusable project request and implementation specification

**Repository:** `ekkus93/esp32_btaudio`  
**Default branch:** `master`  
**Publisher:** `.github/workflows/publish-ci-status.yml`  
**Monitored workflows:**

- `CI — host tests (optimized)`
- `CI — device build (compile only)`

**Monitored branches and authoritative status issues:**

- `master` → issue `#3`, `CI Status: Host + Device — master`
- `feature/esp-bt-audio-duplex` → issue `#4`, `CI Status: Host + Device — feature/esp-bt-audio-duplex`

---

## 1. Purpose

Use this document when adding the same CI-status bridge to another GitHub repository.

ChatGPT can inspect repository files, issues, commits, workflow jobs, job logs, and artifacts through the GitHub connector. However, ordinary push-triggered GitHub Actions runs are not always easy to discover without already knowing the workflow run ID. ChatGPT also does not have an interactive local `gh` session.

The bridge solves that discovery problem by publishing the latest applicable CI state into a known, persistent GitHub issue. The issue contains the exact run IDs, job IDs, step results, commit SHA, branch, failure information, timing, and artifact identifiers that ChatGPT needs to continue a Ralph loop without guessing.

The status issue is an index and current-state mirror. It does not replace GitHub Actions, check runs, commit statuses, job logs, artifacts, branch protection, or hardware testing.

---

## 2. Standard request for a new repository

Give ChatGPT this file and the following instruction:

> Implement the ChatGPT-readable GitHub Actions CI status bridge described in this document for this repository.
>
> Inspect the current GitHub Actions workflows and identify the authoritative quality gates. Create persistent status issues for the workflow and branch combinations that matter to current development. Add a publisher workflow on the repository's default branch that listens for `workflow_run` events of type `requested`, `in_progress`, and `completed`.
>
> Publish exact run, commit, branch, job, step, failure, timing, and artifact metadata in concise Markdown plus parseable JSON. Reject stale runs and unrelated branches. Preserve all existing CI semantics. Do not weaken tests, suppress failures, convert failures into warnings, or change the production build merely to obtain green CI.
>
> Use only the minimum permissions required. Never check out or execute code from the triggering workflow's branch. Never publish raw logs, secrets, environment variables, or other sensitive data into the issue.
>
> Validate the bridge with a real CI run and report the exact issue numbers, workflow files, monitored workflow names, monitored branches, implementation SHA, run IDs, job IDs, and validation result.
>
> Do not create a branch or pull request unless I explicitly request one.

---

## 3. Desired feedback loop

Without the bridge:

```text
commit change
→ push-triggered CI starts
→ ChatGPT cannot reliably discover the run
→ ChatGPT waits, polls indirectly, or guesses
→ run ID eventually becomes available
→ ChatGPT can finally inspect the failed job
```

With the bridge:

```text
commit change
→ CI starts
→ publisher updates a known issue
→ ChatGPT reads the issue
→ issue supplies exact run and job IDs
→ ChatGPT fetches only the relevant job log or artifact
→ ChatGPT patches the defect
→ repeat until all required gates pass on the same SHA
```

---

## 4. Required architecture

### 4.1 Existing CI workflows remain authoritative

The normal CI workflows continue to perform the real work, such as:

- compilation;
- unit and integration tests;
- sanitizer runs;
- lint and static analysis;
- firmware compile-only builds;
- package generation;
- artifact upload.

The status publisher must not alter the meaning of these gates.

### 4.2 Publisher workflow

The publisher must be a separate workflow on the default branch:

```yaml
on:
  workflow_run:
    workflows:
      - "<EXACT WORKFLOW NAME>"
    types:
      - requested
      - in_progress
      - completed
```

The workflow name must exactly match the monitored workflow's top-level `name:` value.

GitHub evaluates `workflow_run` publishers from the default branch. Adding a publisher only to a feature branch is not sufficient.

### 4.3 Persistent status issues

Use one persistent automation-owned issue for each independent branch status that must remain visible. A project may combine related gates into one branch-specific issue when the gates form one release candidate, as this repository does for host and device compile-only CI.

Each issue body must begin with a stable ownership marker:

```html
<!-- maintained by publish-ci-status.yml -->
```

The publisher must verify this marker before overwriting the issue.

---

## 5. Security requirements

Use the minimum permissions:

```yaml
permissions:
  actions: read
  contents: read
  issues: write
```

The publisher must not:

- use `write-all`;
- check out the triggering commit;
- execute scripts from the triggering branch;
- import Python modules from the triggering branch;
- source shell files from the triggering branch;
- execute downloaded artifacts;
- publish raw logs;
- publish secrets or environment variables;
- modify source code, branches, tags, releases, or pull requests;
- update an issue that lacks the expected ownership marker.

`workflow_run` can execute with privileges unavailable to the original pull-request workflow. Treat all triggering-run names and metadata as untrusted text. Sanitize control characters and Markdown-facing strings.

---

## 6. Branch isolation and stale-event protection

A run on one branch must not overwrite another branch's status issue.

Map each monitored branch explicitly to its issue number. Unrecognized branches must exit successfully without modifying any issue.

Before publishing, query the latest applicable run for the same workflow and branch. Ignore older `requested`, `in_progress`, or `completed` events when a newer run exists.

The publisher should accept only intended events, normally:

```text
push
workflow_dispatch
```

Do not allow an unrelated pull-request run to displace the branch status used for release-candidate work.

Use branch-specific concurrency so one branch cannot cancel another branch's publisher:

```yaml
concurrency:
  group: publish-ci-status-${{ github.event.workflow_run.head_branch }}
  cancel-in-progress: true
```

---

## 7. Required status contents

The human-readable section must show at least:

- monitored branch;
- overall state;
- whether related gates tested the same SHA;
- each workflow's status and conclusion;
- exact run ID and run link;
- exact head SHA;
- job counts;
- abnormal job or step names;
- job IDs for failures;
- artifact count;
- publication time.

The machine-readable JSON must contain at least:

```json
{
  "schema_version": 2,
  "publisher": {
    "workflow_file": ".github/workflows/publish-ci-status.yml",
    "issue_number": 0,
    "monitored_branch": "master"
  },
  "trigger": {
    "workflow": "CI",
    "run_id": 0,
    "run_attempt": 1,
    "run_url": "https://github.com/OWNER/REPO/actions/runs/0",
    "status": "completed",
    "conclusion": "success",
    "event": "push",
    "head_branch": "master",
    "head_sha": "FULL_SHA",
    "created_at": "ISO-8601",
    "updated_at": "ISO-8601"
  },
  "overall": "success",
  "same_sha": true,
  "published_at": "ISO-8601",
  "details_compacted": false,
  "host": {},
  "device": {}
}
```

Each workflow entry must include:

- workflow name and ID;
- run ID, number, attempt, and URL;
- event, branch, and SHA;
- status and conclusion;
- timestamps;
- every job ID and result;
- every available step name and result;
- abnormal steps;
- artifact IDs, names, sizes, expiration state, and timestamps.

Treat at least these conclusions as abnormal:

```text
action_required
cancelled
failure
stale
startup_failure
timed_out
```

A skipped step is not automatically a defect, but it must remain visible in the job's step list when details are not compacted.

---

## 8. Pagination and issue-size behavior

Request up to 100 jobs and 100 artifacts.

If GitHub reports more entries than were retrieved, the publisher must fail explicitly rather than publish a plausible-looking incomplete payload. Extend pagination before accepting workflows that exceed that limit.

Do not silently truncate JSON.

When the issue body approaches its size limit:

1. Preserve the human summary.
2. Preserve workflow and SHA metadata.
3. Preserve every job ID and conclusion.
4. Preserve all abnormal jobs and steps.
5. Compact successful step details first.
6. Set:

```json
{
  "details_compacted": true,
  "compaction_reason": "issue_body_size_limit"
}
```

7. Fail instead of publishing if the body remains too large after safe compaction.

---

## 9. Artifact handling

After or during a workflow run, query artifact metadata through the Actions API.

Publish only metadata:

```json
{
  "id": 0,
  "name": "artifact-name",
  "size_in_bytes": 0,
  "expired": false,
  "created_at": "ISO-8601",
  "expires_at": "ISO-8601",
  "updated_at": "ISO-8601"
}
```

Do not copy artifact contents into the issue. Do not download and execute artifacts in the publisher.

ChatGPT can later use the run and artifact IDs to inspect or download the specific evidence it needs.

---

## 10. Validation procedure

The bridge is not complete merely because the YAML parses.

### Static validation

Confirm:

- the publisher is on the default branch;
- monitored workflow names are exact;
- configured issues exist and contain the ownership marker;
- token permissions are minimal;
- no triggering-branch checkout exists;
- no artifact execution exists;
- shell blocks use `set -euo pipefail`;
- malformed API payloads cause explicit failure;
- JSON compaction cannot produce invalid JSON.

Run `actionlint` when available.

### Runtime validation

Trigger a real monitored CI run and verify:

1. The correct branch-specific issue updates.
2. An unrelated branch issue does not change.
3. The issue SHA matches the tested commit.
4. The exact run ID is present.
5. Job IDs and step states match GitHub Actions.
6. Artifact metadata appears when artifacts exist.
7. The JSON block parses.
8. The issue remains open.
9. The issue updates through queued/in-progress/completed states when GitHub emits those events.

### Failure-path validation

A safe failure-path test is recommended. Verify that:

- `overall` becomes `failure`;
- the exact failed job and step are published;
- the job ID can be used to fetch its log;
- a newer run supersedes the old failure;
- a late event from the old run cannot overwrite the newer status.

Do not leave an intentionally failing production gate in place.

---

## 11. ChatGPT operating procedure

During a Ralph loop:

1. Record the candidate SHA.
2. Read the status issue for the candidate branch.
3. Compare the issue's workflow SHAs with the candidate SHA.
4. Ignore status for a different SHA.
5. Use the published run and job IDs to fetch the exact failed log.
6. Diagnose the first meaningful failure instead of guessing.
7. Fetch artifacts only when relevant.
8. Patch and repeat.
9. Require all relevant gates to pass on the same SHA before declaring software CI green.
10. Preserve the distinction between host tests, compile-only builds, physical-device tests, and manual acceptance testing.

The issue is the discovery index. The workflow job log remains the detailed source of failure output.

---

## 12. Optional commit-status bridge

The issue bridge may coexist with commit statuses such as:

```text
ci/host-tests-optimized
ci/device-build-compile-only
```

Commit statuses provide concise same-SHA proof and integrate well with branch protection. The persistent issue provides better run discovery and detailed job/step indexing.

Recommended combined design:

```text
native GitHub check run
+ concise commit status
+ persistent branch-specific status issue
```

Do not remove existing check runs or commit statuses merely because the issue bridge exists.

---

## 13. Required completion report

At completion, report:

```text
REPOSITORY=
DEFAULT_BRANCH=

PUBLISHER_FILE=
IMPLEMENTATION_COMMIT_SHA=

MONITORED_WORKFLOW_1=
MONITORED_WORKFLOW_2=

MONITORED_BRANCH_1=
STATUS_ISSUE_1=

MONITORED_BRANCH_2=
STATUS_ISSUE_2=

VALIDATION_HEAD_SHA=
HOST_RUN_ID=
HOST_JOB_IDS=
DEVICE_RUN_ID=
DEVICE_JOB_IDS=
VALIDATION_RESULT=

STALE_EVENT_PROTECTION=
BRANCH_ISOLATION=
ARTIFACT_METADATA=
MACHINE_JSON_VALIDATED=
RAW_LOGS_PUBLISHED=
TRIGGERING_BRANCH_CODE_EXECUTED=
EXISTING_CI_SEMANTICS_CHANGED=
REMAINING_LIMITATIONS=
```

---

## 14. Acceptance criteria

- [ ] Publisher workflow exists on the default branch.
- [ ] `requested`, `in_progress`, and `completed` are handled.
- [ ] Only intended workflow events are accepted.
- [ ] Monitored branches map to separate issues.
- [ ] Unrelated branches cannot overwrite a monitored issue.
- [ ] Older runs cannot overwrite newer runs.
- [ ] Exact SHA and run IDs are published.
- [ ] Exact job IDs are published.
- [ ] Job and step states are published.
- [ ] Abnormal jobs and steps are summarized.
- [ ] Artifact metadata is published.
- [ ] Machine-readable JSON parses.
- [ ] Pagination overflow fails explicitly.
- [ ] Safe compaction preserves valid JSON and failure details.
- [ ] Minimum permissions are used.
- [ ] Triggering-branch code is not checked out or executed.
- [ ] Raw logs and secrets are not copied into issues.
- [ ] Existing CI semantics remain unchanged.
- [ ] A real CI run updates the correct issue.
- [ ] The issue's SHA matches the validation candidate.
- [ ] ChatGPT can use the job ID to fetch a detailed log.
- [ ] No unused support script is added.

---

## 15. Current repository implementation

This repository deliberately combines the host and device compile-only gates into one status issue per branch because both gates must pass on the same SHA before an ESP32 firmware candidate is considered software-stable.

The publisher:

- runs from `master`;
- listens to the two authoritative workflow names;
- accepts `push` and `workflow_dispatch` runs;
- maps only `master` and `feature/esp-bt-audio-duplex` to issues;
- ignores other branches;
- verifies stale-run ordering independently for the triggering workflow;
- fetches the latest applicable host and device runs for the same branch;
- reports whether both gates tested the same SHA;
- publishes all available jobs, steps, failures, and artifact metadata;
- never checks out repository code;
- never publishes raw job logs;
- does not change the host or device workflow's test semantics.

Future branches should receive a new dedicated issue and an explicit branch-to-issue mapping. Do not make the publisher write every branch into a shared issue.