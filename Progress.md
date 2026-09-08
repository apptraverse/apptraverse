Status: implemented, verified locally. Not accepted.

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

## App Traverse library changes

- `PublicationChannel::TakePublished` now CAS-marks `in_ui_` before clearing `published_`, so `AcquireProducer` cannot wipe the buffer the consumer is reading. Added `TakePublishedCopy()`.
- `SerializeInitialPublication` / `LoadInitialPublication`: root ObjId + existing graph-fragment bytes. GUI Domain creates shells from the buffer; it does not read model objects.
- `EnableNoninteractiveCrt()` hooked from `EnsureObjectRegistration()` so Debug asserts abort to stderr instead of a modal dialog.
- `apptraverse` PUBLIC-links `aether::objects` + `aether::miscpp` only. Full `aether` is linked by chat/presence/model-ui targets that include `aether/clock.h` / `aether/all.h`. This slice does not compile the network client.

## Example-only changes

`examples/main_window_runtime_demo/`:

- `Application` → `MainWindow` (Node) with `x, y, width, height, dpi`
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

- MCP job `20260908-192546-076c2a` / artifact `apptraverse-jobs/20260908-192546-076c2a` → failed: `unknown target 'apptraverse_main_window_headless_check'`
- Nested MCP build artifact `apptraverse-build/20260908-192550-0b0418`

Local runner in this worktree (authoritative for this slice):

- build `apptraverse-build/20260908-192310-3bd9ad` status=ok (first successful compile of the four targets)
- rebuild `apptraverse-build/20260908-192425-8eeced` status=ok

## Headless tests

Profile `win64-ninja-msvc-debug`.

- `apptraverse_publication_channel_test` — OK (unread not overwritten; in-UI slot survives; copy releases; concurrent producer cannot clear held slot)
- `apptraverse_main_window_lifecycle_test` — OK
  - fresh: distill + destroy Domain + reload + initial publication
  - existing: no second distill
  - initial mirror: same ObjIds, different addresses/Domains, same MainWindow bounds, UI `base` invalid and journal empty
  - thread ownership: create/destroy on model thread; GUI copy on consumer thread
  - stop while Distilling: join completes, `published` stays false
  - stop after Ready: join completes
- Synchronization is `condition_variable` waits, not `sleep()` as proof.

## Windows smoke

`apptraverse_main_window_win32_smoke_test` — OK (`main_window_win32_smoke_test OK`, exit 0)

- in-process: Loading appears, Main appears, Loading gone, close Main, thread exits
- in-process close during Loading (`hold_stage=Distilling`): join, no Main
- child `win32_main_window_runtime_demo`: same Loading → Main → close → process exit 0
- child close during Loading (`--hold-stage 2`): process exit 0

## Shutdown

- During Loading: GUI `WM_CLOSE` → `RequestStop` + wake; message loop until `done_event`; join; destroy Loading/notify/GUI Domain on GUI thread.
- After Ready: same stop path; presenter/Main HWND destroyed on GUI thread after join.
- No `TerminateThread`. Model objects are not freed on the GUI thread.

## git status --short

Recorded after the documentation commit; `__pycache__` under `tools/runners/` remains untracked and is not committed.
