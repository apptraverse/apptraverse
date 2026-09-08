Status: implemented, verified. Not accepted.

# MCP worktree-aware — progress

## Problem

User-level MCP (`user-apptraverse`) launches `tools/mcp/apptraverse_mcp.py` from the
checkout that contains that file. `repo_root()` was `Path(__file__).resolve().parents[2]`.
Build/test tools therefore always used that original tree, even when Cursor was
opened on another git worktree.

Observed against this worktree, from the still-running original MCP process:

- job `20260908-195143-ed5b76` / artifact `apptraverse-jobs/20260908-195143-ed5b76`
- nested `apptraverse-build/20260908-195145-6ee81a`
- `ninja: error: unknown target 'apptraverse_main_window_headless_check'`
- response had no `source_dir` (stale server schema)

Cursor MCP config was not rewritten. No per-worktree MCP server was registered.

## API

Optional `source_dir` on START (and on excerpt/log query, which read artifacts).
STATUS/CANCEL/STOP take `job_id` / `process_id` only; the server remembers the
canonical checkout in `.artifacts/mcp-source-index/` under the MCP server tree
and in `job.json` / `request.json` / `process.json` of the selected checkout.

Omitted `source_dir` keeps the previous default: the checkout that launched the
server. Never cwd.

Invalid / missing / non-App-Traverse paths return `failure_kind=invalid_source_dir`
and `state=failed` (no Python exception). Relative paths are rejected.

Tools with `source_dir`:

- `apptraverse_build_start` / `status` / `cancel` / `failure_excerpt`
- `apptraverse_platform_start` / `status` / `cancel` / `failure_excerpt`
- `apptraverse_process_start` / `status` / `stop`
- `apptraverse_chat_headless_test_start`
- `apptraverse_chat_p2p_headless_test_start`
- `apptraverse_runtime_log_query`

## Commit SHA

Recorded after the implementation commit.

## Unit tests

`python -m unittest tools.mcp.test_apptraverse_mcp tools.runners.test_run_apptraverse_job tools.runners.test_run_apptraverse_platform_job`

- omitted `source_dir` → `start_job(repo_root(), …)`
- explicit fixture `source_dir` → runner receives that path, not MCP `repo_root()`
- two roots: status/excerpt do not mix jobs or same-run-id artifacts
- missing path / non-App-Traverse dir / relative path → `invalid_source_dir`
- existing MCP tests remain green

## Real MCP jobs against this worktree

`source_dir=C:\Users\nickc\Projects\apptraverse-prep-deps-assert`
profile `win64-ninja-msvc-debug`

The attached Cursor `user-apptraverse` process is still the original checkout
binary and ignores `source_dir`. Proof of the new server used this worktree's
`apptraverse_mcp.py` over MCP stdio (cwd `C:\Temp`) and the same tool functions.

| target | job ID | artifact ID | nested build artifact | status |
| --- | --- | --- | --- | --- |
| `apptraverse_main_window_headless_check` | `20260908-195713-0fef9b` | `apptraverse-jobs/20260908-195713-0fef9b` | `apptraverse-build/20260908-195714-45078a` | ok |
| `apptraverse_main_window_lifecycle_test` | `20260908-195737-53326d` | `apptraverse-jobs/20260908-195737-53326d` | `apptraverse-build/20260908-195739-d8ba7b` | ok |
| `apptraverse_main_window_win32_smoke_check` | `20260908-195739-1c8eb3` | `apptraverse-jobs/20260908-195739-1c8eb3` | `apptraverse-build/20260908-195741-ce8814` | ok |
| stdio preflight (cwd Temp) | `20260908-195928-053c6d` | `apptraverse-jobs/20260908-195928-053c6d` | — | ok |

Canonical `source_dir` in job metadata and public payloads:
`C:\Users\nickc\Projects\apptraverse-prep-deps-assert`

The headless-check target exists only in this worktree; the stale original MCP
job failed with unknown target. After `source_dir`, ninja found the target here.

Assert/crash path was not regressed: these jobs completed `ok` through the
existing noninteractive worker (no assert dialog).

# Main-window skeleton — progress

## Identity

- Base SHA: `24daa80dfb39edfcc1261cd15df2d62d1ce32fa9`
- Branch: `prep/deps-objects-assert-mcp-v1`
- Worktree: `C:\Users\nickc\Projects\apptraverse-prep-deps-assert`

## Commits

1. `e9bca9adfeba9b0c111486276646b1265a97452e` — Fix PublicationChannel TakePublished so a consumer buffer cannot be cleared.
2. `6a58e2074a108ca85599b6cd191ef9982f8011ea` — Add serialized initial publication without GUI-thread model graph copies.
3. `0f2540bc1d55e8853a0851376ba31e34fb7c9c20` — Add Loading/main-window example with a dedicated model thread.
4. `57f4fc30db7b75354d82085e81924f3ef3b520fc` — Add headless lifecycle and Win32 smoke tests for the main window.
5. `1a078f9f9a6b1f907749d1a8d5f6fbca7da3c0da` — Record the main-window skeleton plan and progress.
6. `75a1af396b86237bf6c6b770a8db50e4ef2a159f` — Name the documentation commit in Progress.md.
7. `4135ca5afa46b7f731afa3ba295ff1baf63aab27` — Remove premature DPI state from main window skeleton.

## DPI cleanup

DPI was removed from the new skeleton because `plan.md` defers DPI/screen system events to a later stage. This slice only needs Loading → model thread → serialized mirror → Main → shutdown.

Removed from the example and its tests:

- `MainWindow::dpi`
- reflection / Load / Save of DPI
- `kDefaultDpi`
- `model_window_dpi`
- DPI assertions

`MainWindow` now has only `x`, `y`, `width`, `height`. Schema version is 2; v0 and v1 Load throw (no conversion). Tests use clean temp state directories. `plan.md` later-stage DPI/screen architecture is unchanged.

## App Traverse library changes

- `PublicationChannel::TakePublished` now CAS-marks `in_ui_` before clearing `published_`, so `AcquireProducer` cannot wipe the buffer the consumer is reading. Added `TakePublishedCopy()`.
- `SerializeInitialPublication` / `LoadInitialPublication`: root ObjId + existing graph-fragment bytes. GUI Domain creates shells from the buffer; it does not read model objects.
- `EnableNoninteractiveCrt()` hooked from `EnsureObjectRegistration()` so Debug asserts abort to stderr instead of a modal dialog.
- `apptraverse` PUBLIC-links `aether::objects` + `aether::miscpp` only. Full `aether` is linked by chat/presence/model-ui targets that include `aether/clock.h` / `aether/all.h`. This slice does not compile the network client.

## Example-only changes

`examples/main_window_runtime_demo/`:

- `Application` → `MainWindow` (Node) with `x, y, width, height`
- `ModelSession::Run` on the model thread
- Win32 `WinApp`: Loading HWND, notify HWND, presenter, empty Main HWND

## Model lifecycle

Fresh state (model thread only):

1. create storage + Domain + graph
2. distill (`FinalizeDistilledGraph` + `SaveDistilledRoot`)
3. drop Application + Domain + storage
4. new storage + Domain in the same thread
5. `LoadApplication`
6. serialize into `PublicationChannel<3>` and notify GUI

Existing state: skip 1–3; load and publish.

Stop (`RequestStop` + `cv.notify_all`) is checked at each stage, including mid-serialize before `PublishProducer`. Cleanup always runs on the model thread: Application, Domain, storage, then `SetEvent(done_event)`.

## Initial publication format

`uint32 root_id` followed by `SerializeObjectGraphToBuffer` (layer count, per-layer obj/class/version/bytes, then Node generation table). GUI `LoadInitialPublication` injects layers into RAM storage, creates shells using a registered factory (skips `ae::Obj` base layers), loads, and `FinalizeUiNodeState` (clears `base` and `journal`).

## Domain ownership

- Model Domain + `DirectoryDomainStorage`: model thread only. Never passed to GUI.
- GUI Domain + `RamDomainStorage`: GUI thread (or the test consumer thread) after `TakePublishedCopy`.
- Same ObjIds, different C++ addresses, different Domains. Observation atomics store integer addresses for tests; GUI must not dereference them.

## MCP / artifacts

User-level `user-apptraverse` MCP is bound to the original App Traverse checkout, not this worktree.

- MCP job `20260908-193409-2f3650` / artifact `apptraverse-jobs/20260908-193409-2f3650` → failed: `unknown target 'apptraverse_main_window_headless_check'`
- Nested MCP build artifact `apptraverse-build/20260908-193411-af0768`

Local runner in this worktree (authoritative for this slice):

- DPI-cleanup build `apptraverse-build/20260908-193412-68f37e` status=ok

## Headless tests

Profile `win64-ninja-msvc-debug`.

- `apptraverse_main_window_lifecycle_test` — OK after DPI removal
  - fresh: distill + destroy Domain + reload + initial publication
  - existing: no second distill
  - initial mirror: same ObjIds, different addresses/Domains, same MainWindow bounds, UI `base` invalid and journal empty
  - thread ownership: create/destroy on model thread; GUI copy on consumer thread
  - stop while Distilling: join completes, `published` stays false
  - stop after Ready: join completes
- Synchronization is `condition_variable` waits, not `sleep()` as proof.

## Windows smoke

`apptraverse_main_window_win32_smoke_test` — OK after DPI removal (`main_window_win32_smoke_test OK`, exit 0)

- in-process: Loading appears, Main appears, Loading gone, close Main, thread exits
- in-process close during Loading (`hold_stage=Distilling`): join, no Main
- child `win32_main_window_runtime_demo`: same Loading → Main → close → process exit 0
- child close during Loading (`--hold-stage 2`): process exit 0

Search after cleanup: `dpi`, `DPI`, `WM_DPICHANGED`, `GetDpiForWindow` are absent from `examples/main_window_runtime_demo` and `tests/main_window_*.cpp`.

## Shutdown

- During Loading: GUI `WM_CLOSE` → `RequestStop` + wake; message loop until `done_event`; join; destroy Loading/notify/GUI Domain on GUI thread.
- After Ready: same stop path; presenter/Main HWND destroyed on GUI thread after join.
- No `TerminateThread`. Model objects are not freed on the GUI thread.

## git status --short

Recorded after the MCP worktree-aware work. Untracked: `.artifacts/`, `build/`, `__pycache__/`. `.venv-apptraverse-mcp/` is gitignored.
