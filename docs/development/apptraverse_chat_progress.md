Status: active
Plan: apptraverse_chat_plan.md
Iterate prompt: apptraverse_chat_iterate_prompt.md
Stop signal: APPTRAVERSE_CHAT_BASELINE_COMPLETE

# Current ready slice

Slice:
ACT-S023

Status:
ready

Goal:
thin MCP wrapper over the canonical runner and background job controller.
Must not independently implement build logic.

# Status vocabulary

- ready
- in_progress
- blocked
- done
- optional

# Slice table

| Slice ID | Description | Status | Notes |
| --- | --- | --- | --- |
| ACT-S001 | initial Windows presentation replay | blocked | blocked until tooling baseline is usable; previous environment/worktree failure, not a product-code failure |
| ACT-S010 | canonical plan/progress/iterate trio | done | documentation-only |
| ACT-S020 | checked-in Ninja CMake presets | done | this session; x86_64 desktop presets; no configure/build |
| ACT-S021 | canonical staged build runner | done | tooling-level: unit tests, VS preflight, configure, classified compile_failed |
| ACT-S022A | bounded artifacts and compact JSON results | done | this session |
| ACT-S022B | background jobs start/status/cancel | done | this session; must not implement MCP |
| ACT-S022 | JSON result/artifact/timeout contract | done | S022A + S022B |
| ACT-S023 | thin MCP wrapper over canonical runner | ready | wraps job controller; must not reimplement build |
| ACT-S025 | structured runtime JSONL logging | blocked | documentation only |
| ACT-S026 | GoogleTest multi-instance harness | blocked | documentation only |
| ACT-S027 | debugger inspection adapter | blocked | documentation only |
| ACT-S030 | read-only architecture audit | blocked | blocked by tooling baseline |
| ACT-S040 | transport simplification slices | blocked | blocked by audit and user decisions |

## ACT-S001 details

ACT-S001 remains blocked until the tooling baseline is usable. Previous attempt had:

- accidental MinGW configure
- compiler switch inside same build directory
- broken/missing worktree
- staged unrelated files in another checkout
- no commit produced

Do not treat this as a product-code failure.

## ACT-S020 details

Evidence:

- `CMakePresets.json` created
- canonical x86_64 desktop Ninja profiles defined:
  `win64-ninja-msvc-debug`, `linux-x64-ninja-clang-debug`,
  `macos-x64-ninja-appleclang-debug`
- five-platform x86_64 target recorded; ARM64 out of current baseline
- Android remains Gradle + Ninja native ABI=x86_64; iOS remains xcodebuild
- no build/configure performed

## ACT-S021 details

Done at the tooling level. Evidence:

- runner unit tests passed
- Windows preflight passed
- configure executed successfully
- build invoked through the canonical profile
- compile failure correctly classified as `compile_failed`
- no clean/rebuild/fallback

Product compile success is not required to prove runner orchestration.

## ACT-B001

- kind: `transitive_dependency_drift`
- status: blocked
- pinned Æther SHA `7294f92a` includes `aether-miscpp/reflect/domain_visitor.h`
- Æther fetches aether-miscpp using floating `GIT_TAG main`
- current header path: `aether-miscpp/domain_visitor/domain_visitor.h`
- not fixed in this slice; not a runner defect

## ACT-S022A details

Windows runner path: `tools/runners/run_apptraverse_build.py`.

`--json` emits schema `apptraverse.build_result/1`. Artifacts under
`.artifacts/apptraverse-build/<run-id>/` with public `artifact_id`
`apptraverse-build/<run-id>`.

Real validation: known-failing VS narrow build captured as compact JSON
`status=failed failure_kind=compile_failed` with C1083 in `first_error`.
Full MSBuild output stayed in artifacts.

## ACT-S022B details

Windows job controller: `tools/runners/run_apptraverse_job.py`.

Schema `apptraverse.build_job/1`. Job artifacts under
`.artifacts/apptraverse-jobs/<job-id>/` with public `artifact_id`
`apptraverse-jobs/<job-id>`. Operations: `start`, `status`, `cancel`,
`_worker`. Job states: starting, running, completed, cancelled, failed,
not_found.

Windows detach: `cmd.exe /c start "" /b` without redirected stdio; worker
writes PID first; `start` polls up to 2s. Windows liveness uses
`OpenProcess`/`GetExitCodeProcess` (not `os.kill(pid, 0)`). Cancel uses
`taskkill.exe /PID /T /F` with `shell=False`.

Real validation: `start` returned in 0.545s. Job
`20260818-042808-d03598` completed; wrapped build `compile_failed` C1083
`domain_visitor.h`. Product ACT-B001 remains blocked.

# Acceptance registry

| Acceptance ID | Status | Evidence / notes |
| --- | --- | --- |
| ACT-A001 | done | three canonical files created and linked |
| ACT-A002 | done | progress identifies exactly one ready slice: ACT-S023 |
| ACT-A003 | done at documentation and preset level | Ninja preferred; explicit `win64-vs2022-msvc-debug` fallback |
| ACT-A004 | done at runner-contract level | staged execution + JSON/artifacts; product build remains ACT-B001 |
| ACT-A005 | blocked | requires ACT-S023 MCP wrapper |
| ACT-A006 | blocked | Windows/Android chat functional baseline not yet executed |
| ACT-A007 | blocked | requires ACT-S030 read-only architecture audit |

# Evidence log

Date: Monday Aug 17, 2026

Session ACT-S010:

- clean clone from v3 SHA `4786ac6bf7e384c530a1f6994c4f3e91ce35bb04`
- documentation-only
- no configure/build/test
- no source changes
- no v4/v5 cherry-picks

Session ACT-S020:

- clean checkout of `review/chat-methodology-v1` at `23f63b058a35e601b5d9e6ed102a14ddba80b10a`
- configuration/documentation-only
- `CMakePresets.json` added; plan/progress/iterate updated
- `cmake --list-presets` used only to parse/list presets
- no configure/build/rebuild/CTest/Gradle/xcodebuild
- no source code or product-feature work
- no v4/v5 cherry-picks
- ARM64 not added to the baseline

Session ACT-S021:

- clean checkout of `review/chat-tooling-v1` at `7a0c1e4fe7c876be81ab92449d6d9d1c5f473c8b`
- Windows runner and unit tests added
- `python -m unittest tools.runners.test_run_apptraverse_build -q` PASS
- preflight BLOCKED: `ninja_missing`
- configure not_run
- incremental build not_run
- no clean/rebuild/directory deletion
- no compiler/generator fallback
- no Android/Linux/macOS/Gradle/xcodebuild/CTest/MCP/JSON artifacts
- no product source changes
- ACT-S022 not marked ready

Session ACT-S021 (runner-v2):

- clean checkout of `review/chat-runner-v1` at `21b9861cf4d6403c98c5c197db75583459c5ac5f`
- added explicit `win64-vs2022-msvc-debug` preset and runner support
- Ninja preferred, not required; no implicit generator fallback
- Python unit tests PASS (17)
- VS preflight PASS
- VS configure `configured` (~347s)
- VS incremental build `apptraverse_chat_component_test` FAIL `compile_failed` (~251s)
- timeout 15 minutes; not hit
- no Ninja retry; no clean/rebuild; no C++ product fix
- documented Python-first orchestration, GoogleTest policy, ChatTransport, console/Emscripten, `ae::Uid` audit
- ACT-S022 not marked ready

Session ACT-S022A:

- checkout `review/chat-runner-v2` at `03bc24478948c690f7ddf985f1bab2f177e6effd`
- ACT-S021 reclassified done at tooling level
- ACT-B001 recorded as `transitive_dependency_drift` (not fixed)
- `--json` compact result + `.artifacts/apptraverse-build/` logs
- unit tests PASS (29)
- real VS narrow build once: `status=failed failure_kind=compile_failed`
- `first_error` contains C1083 and `aether-miscpp/reflect/domain_visitor.h`
- `artifact_id=apptraverse-build/20260818-033329-e7c5bd`
- duration_ms=121425; timeout 15 minutes not hit
- full MSBuild not printed to terminal
- no clean/rebuild/product/dependency fix; no MCP; no background jobs

Session ACT-S022B:

- checkout `review/chat-runner-jobs-v1` at `d7689a76a88b35373dc9b0e4c49ad498f089c911`
- background job controller wrapping `run_apptraverse_build.py`
- unit tests PASS (47: 29 build + 18 job)
- Windows `start` detached in 0.545s (`job_id=20260818-042808-d03598`)
- status running then completed; build `status=failed failure_kind=compile_failed`
- `first_error` contains C1083 and `aether-miscpp/reflect/domain_visitor.h`
- job `artifact_id=apptraverse-jobs/20260818-042808-d03598`
- build `artifact_id=apptraverse-build/20260818-042808-d914e8`
- job `duration_ms=152619`; full MSBuild not printed
- no clean/rebuild/configure/product/dependency fix; no CTest; no MCP
- ACT-B001 remains blocked
- next ready slice: ACT-S023 only

# Session log

Completion packet:

Slice: ACT-S010
Acceptance IDs: ACT-A001, ACT-A002, ACT-A003 documentation level
Artifacts:
- apptraverse_chat_plan.md
- apptraverse_chat_progress.md
- apptraverse_chat_iterate_prompt.md
Build identity: n/a
Build proof: n/a
Runtime proof: n/a
Known limits:
- Ninja presets not implemented
- runner not implemented
- MCP not implemented
- ACT-S001 Windows render fix still blocked
Next ready slice: ACT-S020

Completion packet:

Slice: ACT-S020
Acceptance IDs: ACT-A003 preset-level; ACT-A004 still blocked
Artifacts:
- CMakePresets.json
- apptraverse_chat_plan.md
- apptraverse_chat_progress.md
- apptraverse_chat_iterate_prompt.md
Build identity: n/a
Build proof: cmake --list-presets only
Runtime proof: n/a
Known limits:
- runner not implemented
- MCP not implemented
- Linux/macOS/Android/iOS adapters not implemented
- ACT-S001 Windows render fix still blocked
Next ready slice: ACT-S021

Completion packet:

Slice: ACT-S021
Acceptance IDs: ACT-A004 still blocked
Artifacts:
- tools/runners/run_apptraverse_build.py
- tools/runners/test_run_apptraverse_build.py
- tools/runners/__init__.py
- apptraverse_chat_progress.md
- apptraverse_chat_plan.md (runner path only)
Build identity: n/a
Build proof: unit tests PASS; runtime preflight blocked ninja_missing
Runtime proof: n/a
Typed blockers: ninja_missing
Known limits:
- ninja not on PATH in this environment
- configure/build not executed
- MCP not implemented
- JSON/artifact contract not implemented
- ACT-S001 Windows render fix still blocked
Next ready slice: none (ACT-S021 blocked; ACT-S022 not ready)

Completion packet:

Slice: ACT-S021 (runner-v2)
Acceptance IDs: ACT-A004 still blocked
Artifacts:
- CMakePresets.json
- tools/runners/run_apptraverse_build.py
- tools/runners/test_run_apptraverse_build.py
- apptraverse_chat_plan.md
- apptraverse_chat_progress.md
- apptraverse_chat_iterate_prompt.md
Build identity: win64-vs2022-msvc-debug
Build proof: unit tests PASS; VS preflight PASS; configure configured; build compile_failed
Runtime proof: n/a
Typed blockers: compile_failed
Known limits:
- Æther include `aether-miscpp/reflect/domain_visitor.h` missing under VS 2022 generator
- Ninja profile still unused on this host
- MCP/JSON not implemented
- ACT-S001 still blocked
Next ready slice: none (ACT-S021 blocked; ACT-S022 not ready)

Completion packet:

Slice: ACT-S022A
Acceptance IDs: ACT-A004 runner-contract level
Artifacts:
- tools/runners/run_apptraverse_build.py
- tools/runners/test_run_apptraverse_build.py
- .gitignore
- apptraverse_chat_plan.md
- apptraverse_chat_progress.md
- apptraverse_chat_iterate_prompt.md
Build identity: win64-vs2022-msvc-debug
Build proof: unit tests PASS; JSON failed result compile_failed; artifacts written
Runtime proof: n/a
Typed blockers: ACT-B001 transitive_dependency_drift (product)
Known limits:
- product still does not compile
- background jobs not implemented
- MCP not implemented
Next ready slice: ACT-S022B

Completion packet:

Slice: ACT-S022B
Acceptance IDs: ACT-A004 runner-contract level (jobs); ACT-A005 still blocked
Artifacts:
- tools/runners/run_apptraverse_job.py
- tools/runners/test_run_apptraverse_job.py
- apptraverse_chat_plan.md
- apptraverse_chat_progress.md
- apptraverse_chat_iterate_prompt.md
Build identity: win64-vs2022-msvc-debug
Build proof: unit tests PASS (47); start 0.545s; job completed; build compile_failed
Runtime proof: n/a
Typed blockers: ACT-B001 transitive_dependency_drift (product)
Known limits:
- product still does not compile
- MCP not implemented
- S025/S026/S027 documented, not implemented
Next ready slice: ACT-S023
