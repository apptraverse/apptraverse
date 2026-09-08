Status: implemented, verified. Not accepted.

# Main-window startup simplify and distill/load-only split — progress

## Identity

- Branch: `prep/deps-objects-assert-mcp-v1`
- Worktree: `C:\Users\nickc\Projects\apptraverse-prep-deps-assert`
- Status: implemented, verified. Not accepted.

## Removed

- `ModelStartupStage`, `EnterStage`, `hold_stage`, `SetHoldStage`, `--hold-stage`
- Cancelable startup and close-during-Loading (tests and production)
- Test instrumentation on `ModelSession`: `published`, `finished`,
  `distilled_this_run`, `stage`, thread ids, model object addresses/ids/geometry
- `ApplicationStateExists` helper; `accept_input_`; `stop_requested_`;
  `GuiPresenterClassId`; `DestroyGuiMirror` wrapper
- Duplicate registration / `EnableNoninteractiveCrt` inside `WinApp::Run` and
  the model thread

## ModelSession (kept)

Shared production model-thread path for Win32 and headless tests:

- `state_dir`, `PublicationChannel`, `mu`/`cv`, `stop`, `RequestStop`, `Run`
- `notify_hwnd` / `done_event` (WinApp vs headless that never sets them)

## Dev / load-only

- Compile definition: `APPTRAVERSE_ENABLE_DISTILLATION`
- `win32_main_window_runtime_demo` — distill-enabled; bootstrap if state missing
- `win32_main_window_runtime_demo_load_only` — no definition; load only; missing
  Application is fatal
- Lifecycle `.cpp` is compiled per target (not one STATIC lib), so the ifdef is
  real. Load-only lifecycle obj has `LoadApplication` and no
  `FinalizeDistilledGraph` / `SaveDistilledRoot` / `BuildMainWindowGraph`.

Registration: `main()` registers model + Win32 presenter once, then `WinApp::Run`.

## Tests / artifacts

Cursor `user-apptraverse` still has no `source_dir`. **BLOCKED**.
Worktree runner (incremental, cmake regenerated ninja; no clean/rebuild):

| target | run_id / artifact | status |
| --- | --- | --- |
| simplify startup | `apptraverse-build/20260908-213039-bcd573` | ok |
| distill + load-only + smokes | `apptraverse-build/20260908-213326-84d24d` | ok (`publication_channel_test`, `main_window_lifecycle_test`, `main_window_lifecycle_load_only_test`, `main_window_win32_smoke_test`) |

Missing-state fatal is the load-only child-process smoke (non-zero, no Main, no Application object).

## Commits / push

- `628174df8a5b006b9d783a331751a829034e671f` — Simplify main-window startup and remove test state machine
- `1e91b0e15c484ed4fa2584d694dcfc26a06ea415` — Split development distillation and load-only targets

Pushed to `origin/prep/deps-objects-assert-mcp-v1`. `main` not changed.

# Main-window lifecycle defensive-check cleanup — progress

## Identity

- Branch: `prep/deps-objects-assert-mcp-v1`
- Worktree: `C:\Users\nickc\Projects\apptraverse-prep-deps-assert`
- Status: implemented, verified. Not accepted.

## Removed checks (unreachable or already guaranteed)

- `Win32MainWindowPresenter::OnLoad`: no `hwnd != nullptr` early return, no
  `!window` check, no `assert(hwnd == nullptr)` / `assert(window)`, no
  bool-return recovery if `CreateWindowExW` fails.
- `DestroyNative` / destructor `DestroyWindow`: gone. Native teardown is
  `OnUnload` only, called from `UnloadPresenters` after successful GUI init.
  Model-side Win32 objects never run that pass, so the destructor does not
  guess whether an HWND exists.
- `presentation_initialized` and per-presenter "already initialized" / second
  `OnLoad` protection.
- `InitializePresenters` bool return and "OnLoad failed" recovery.
- `OnPublished`: empty-bytes return, stacked graph asserts, init-failure
  abort-to-Loading, `assert(loading_)` before destroying Loading.
- Startup recovery (`AbandonStartup`, `exit_code_`, `joinable()`, repeated
  `assert(notify_)` / `assert(done_event)` at shutdown).
- WndProc `assert(msg == WM_GETMINMAXINFO)` when `app == nullptr`.
- ModelSession duplicate `assert(buffer)` / `assert(presenter)` after the
  stage that already produced them.
- Tests: idempotent second `InitializePresenters`; `TestPresenterInitFailure`
  (recoverable OnLoad failure is not a supported state).

## Kept `if`s (real state-machine branches)

- `InitializePresenters` / `UnloadPresenters`: `dynamic_cast` skip of
  non-Presenter objects on the reachable walk.
- `OnPublished`: `stop_requested_` — publication can arrive after close during
  Loading; skip presentation init.
- `RequestStop`: already requested (Loading and Main can both send WM_CLOSE).
- `loading_ != nullptr` at shutdown: close during Loading vs after Main replaced
  it. Success path destroys Loading then sets `loading_ = nullptr` because
  that nullable handle is the two-stage machine.
- `ui_application_` before `UnloadPresenters`: graph exists only after a
  completed presentation pass; close during Loading never loaded it.
- WndProc: `WM_NCCREATE` binds userdata; `app == nullptr` is legal before that
  (`WM_GETMINMAXINFO`); after bind, dispatch to `Handle`.
- Message loop: `WAIT_OBJECT_0` vs queued input; `WM_QUIT`.
- ModelSession: `EnterStage` / `stop`; first launch vs existing state;
  `done_event` / notify HWND (WinApp vs headless tests that never set them).

## Fatal (unrecoverable) conditions

- `CreateEventW` for `done_event` returns null.
- `CreateWindowExW` for notify or Loading returns null.
- `CreateWindowExW` for Main returns null (`OnLoad`).
- `std::thread` construction throws (uncaught; never enters the loop).

No recovery, no keep-Loading, no presenter-without-HWND, no bool `OnLoad`.

## Tests

Headless: startup, existing-state, mirror identity, most-derived Test
presenter, `InitializePresenters` once then `UnloadPresenters` once,
model-side OnLoad/OnUnload not called, object destructor does not `OnUnload`,
thread ownership, stop during Loading, stop after Ready.

Win32 smoke: Loading then Main; GUI class `Win32MainWindowPresenter`; close
during Loading does not create Main; GUI teardown destroys Main HWND.

## MCP / artifacts

Cursor `user-apptraverse` still has no `source_dir`. **BLOCKED**.
Worktree runner (incremental, no clean/rebuild):

| target | run_id / artifact | status |
| --- | --- | --- |
| `apptraverse_main_window_headless_check` + `apptraverse_main_window_win32_smoke_check` | `apptraverse-build/20260908-211920-2b7853` | ok (`publication_channel_test OK`, `main_window_lifecycle_test OK`, `main_window_win32_smoke_test OK`) |

Did not rebuild `apptraverse_event_sourced_core_test`: it pulls the full Aether
client (sodium) and is outside this slice. `presenter.h` there is only a
class-id check.

## Commits / push

- `c0008de19a234eda79b63bb7c08e316f5bcf763b` — Remove defensive state checks from main-window lifecycle

Pushed to `origin/prep/deps-objects-assert-mcp-v1`. `main` not changed.

# Object-graph presenter — progress

## Identity

- Base SHA: `9bae06bd881e043b4f67ded7a3d731e68726dfa5`
- Branch: `prep/deps-objects-assert-mcp-v1`
- Worktree: `C:\Users\nickc\Projects\apptraverse-prep-deps-assert`
- Status: implemented, verified. Not accepted.

## App Traverse library changes

- `LoadInitialPublication` injects serialized layers then calls
  `DomainGraph::LoadRoot` so aether-objects picks the most-derived registered
  factory. No App Traverse preferred-class map.
- Distilled `Node::base` object layers are omitted from UI publication buffers.
- After LoadRoot, `LoadStoredAncestorLayers` loads stored ancestor class
  layers onto the already-constructed most-derived object (needed when
  persisted data has only `MainWindowPresenter` and the registry created
  `TestMainWindowPresenter` / `Win32MainWindowPresenter`).
- `InitializePresenters` walks reachable live objects from the GUI root and
  calls `Presenter::OnLoad()` once. Object Load does not call it.
- `Presenter` documents OnLoad/OnUnload as GUI presentation hooks; runtime-only
  `presentation_host` is not serialized. `presentation_initialized` was removed
  in the defensive-check cleanup (init runs once per GUI mirror).

## Example changes

- Graph: `Application` → `MainWindow` (Node, schema v3: x,y,width,height,presenter)
  → `MainWindowPresenter` (not Node) → `Win32MainWindowPresenter` (HWND runtime-only).
- Cycle: `presenter->window` is the same MainWindow in that Domain.
- `WinApp` no longer owns a presenter member or calls `Create`. It loads the
  GUI graph, runs `InitializePresenters`, then destroys Loading. No
  `dynamic_cast` to Win32. If `RequestStop` already happened, skip init.
- Model-side most-derived Win32 presenter exists after Load and does not create HWND.

## Tests

Headless `apptraverse_main_window_lifecycle_test` (no Win32):

- A. Neutral `MainWindowPresenter` in the buffer + registered
  `TestMainWindowPresenter` → LoadRoot materializes Test
- B. OnLoad not called after model load / serialize / GUI deserialize
- C. `InitializePresenters` once; `presenter.window` resolved; back-pointer
- D. Model vs GUI: same ObjIds, different addresses/Domains
- E. Cycle walk terminates; dropping root+keepalive does not leak the GUI graph
- F. Model-side Test exists after LoadApplication; OnLoad not called there

Windows smoke (existing checks plus):

- GUI presenter class is `Win32MainWindowPresenter`
- exactly one Main window
- close during Loading does not create Main

## MCP jobs / artifacts

Cursor `user-apptraverse` schema still has no `source_dir` argument.
**BLOCKED** for that attached MCP process. Local incremental runner is not
claimed as MCP success.

Worktree runner `tools/runners/run_apptraverse_build.py` (MSVC env, stage=build,
no clean/rebuild):

| target | run_id / artifact | status |
| --- | --- | --- |
| `apptraverse_main_window_lifecycle_test` | `apptraverse-build/20260908-203315-e7a483` | ok (test exe OK) |
| `apptraverse_main_window_headless_check` + `apptraverse_main_window_win32_smoke_check` | `apptraverse-build/20260908-203339-eb1692` | ok |

## Known limitations

- Cursor user-level MCP is still the original checkout binary (no `source_dir`).
- Ancestor-layer reload is an App Traverse pass after LoadRoot; aether-objects
  `Load(Derived)` does not itself load ancestor class layers when the derived
  class has no stored version.
- Presenter local state (DPI, monitor, scroll) is not implemented.
- `main` was not changed.

## Commits / push

1. `86f5d38019c88a5397cb8dc19d3efcf8971bbca4` — Use aether-object descendant resolution for GUI mirror loading
2. `63a6eff6080d883819855ce28c9459af065c8e52` — Move main window presentation into object graph

Pushed to `origin/prep/deps-objects-assert-mcp-v1`. `main` not changed. History not rewritten.

## git status --short

Untracked caches only (not committed):

```
?? tools/mcp/__pycache__/
?? tools/runners/__pycache__/
?? tools/runtime/__pycache__/
```

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

`5699c613e55f66ccea6eece1f2009f28d8dd6f26` — Make App Traverse MCP worktree-aware

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

After recording the implementation SHA (documentation-only follow-up):

```
 M Progress.md
?? tools/mcp/__pycache__/
?? tools/runners/__pycache__/
?? tools/runtime/__pycache__/
```
