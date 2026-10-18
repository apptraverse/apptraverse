# surfaces_demo smoke (Windows) — 2026-10-18

## Baseline and isolation

| Item | Value |
| --- | --- |
| Repository | `apptraverse/apptraverse` (worktree) |
| Branch | `cursor/surfaces-demo-smoke` @ `21f4524f487c4a8924517a8caf9a6d4a5382805b` |
| Baseline ref | `feature/ui-shell-windows` (same SHA as local HEAD at start) |
| Worktree | `C:\Users\nickc\Projects\apptraverse-wt-surfaces-demo-smoke` |
| Build dir | `C:\Users\nickc\Projects\apptraverse-wt-surfaces-demo-smoke\build-surfaces-demo-smoke-release` |
| `apptraverse-app` | Not modified (Host/Admin comparison read-only) |

`AGENTS.md` was read from `apptraverse-app` (surfaces_demo named as model reference). Main repo has no root `AGENTS.md`.

## Build (VS2022 x64 Release)

CMake (examples only, no full test suite):

```text
cmake -S C:\Users\nickc\Projects\apptraverse-wt-surfaces-demo-smoke ^
  -B C:\Users\nickc\Projects\apptraverse-wt-surfaces-demo-smoke\build-surfaces-demo-smoke-release ^
  -G "Visual Studio 17 2022" -A x64 ^
  -DAPPTRAVERSE_BUILD_EXAMPLES=ON -DBUILD_TESTING=OFF

cmake --build ...\build-surfaces-demo-smoke-release --config Release ^
  --target win32_surfaces_demo win32_surfaces_demo_load_only -j
```

| Target | Result |
| --- | --- |
| `win32_surfaces_demo` | **PASS** (compile/link) |
| `win32_surfaces_demo_load_only` | **PASS** (compile/link) |

| Artifact | Path | SHA256 |
| --- | --- | --- |
| `win32_surfaces_demo.exe` | `...\build-surfaces-demo-smoke-release\examples\surfaces_demo\windows\Release\win32_surfaces_demo.exe` | `A36546366CABCAAFA1118EE2B596957A7518440A5A4DF59A430BE2420228D008` |
| `win32_surfaces_demo_load_only.exe` | `...\Release\win32_surfaces_demo_load_only.exe` | (same build tree; hash not re-recorded after load-only-only relink) |

Launch command (intended):

```text
win32_surfaces_demo.exe --state-dir <absolute-test-profile>
```

Window class (from `win_presenters.h`): `AppTraverseSurfacesWindow`.

## Runtime GUI smoke — **BLOCKED**

### Observed failure

Both Release and Debug executables exit immediately:

```text
fatal: LoadApplication failed id=1
exit code 3
```

Distill bootstrap **does** write under `--state-dir` (folders `1`…`4`), but the model thread aborts on the first `LoadApplication<Application>`.

### Minimal repro (same tree, no surfaces UI)

```text
apptraverse_directory_domain_storage_test.exe
→ CHECK failed: node.is_loaded() at directory_domain_storage_test.cpp:47
```

After the failed run, `%TEMP%\apptraverse_directory_domain_storage_test\100000\` contains persisted layers (e.g. Node class dir with version `4`, 9 bytes), but `Node::ptr::Load()` leaves `cached_` empty.

Also fails on:

- `apptraverse_surfaces_model_test.exe` (first directory persistence case)
- `apptraverse_surfaces_win32_smoke_test.exe` (first `WinApp::Run` that needs load)

Same behavior on checked-out `origin/main` (`ffccf64`) with the same build directory — **not** surfaces-demo-specific.

### Impact on requested checks

| Check | Status |
| --- | --- |
| Visible Surface window / Add / drag 60s / resize / post-release drift | **NOT RUN** (process exits before UI) |
| Restart + geometry restore | **NOT RUN** |
| `win32_surfaces_demo_load_only` | **NOT RUN** |
| Motion CSV | Not produced (no live window) |

Automated script: `scripts/surfaces_demo_smoke_test.ps1` (fails at launch until directory load works).

### Likely layer

Directory persistence / `DomainGraph::LoadRoot` on this Windows host (MSVC 19.44, SDK 10.0.26100). Investigation stopped at `directory_domain_storage_test` without changing movement or Host/Admin architecture.

## Mechanism (surfaces_demo) — from source

### Pipeline

1. **Native move/resize** — Win32 default non-client handling (`DefWindowProc`). No `WM_ENTERSIZEMOVE` / `WM_EXITSIZEMOVE` handlers in surfaces demo.
2. **`WM_SIZE`** — client size → `PresentationSizeChanged(w,h)` → model proxy → `Surface::SetPresentationSize` → journal event → structural publication → UI mirror → `OnModelChanged` → **`LayoutControls()` only** (child buttons).
3. **Desktop outer frame (x/y/w/h)** — **not** reported during drag. `QueueCurrentBounds()` (`GetWindowRect` → `UpdateModelBounds` → `Surface::SetDesktopBounds`) runs only from `WinApp::RequestApplicationStop()` (`QueueAllWindowBounds()`), i.e. **shutdown path** (also queues foreground via `PageShown` / `MakeCurrent`).
4. **Model thread** — `ModelObjectProxy::Invoke` → journal → dirty node → incremental publication → GUI `ApplySurfacesStructural` → presenter `OnModelChanged`.
5. **`SetWindowPos` on main Surface HWND** — **never** for mirror sync. Uses:
   - `CreateWindowExW` initial placement from `surface->desktop_*` on `OnLoad`
   - Child buttons in `LayoutControls`
   - `SetWindowPos(..., HWND_TOP, SWP_NOMOVE|SWP_NOSIZE)` for Z-order restore only (`RestoreActiveSurfaceZOrder`)

### When coordinates enter the model

| Field | When updated in model |
| --- | --- |
| `desktop_x/y/width/height` | On **application stop** (native snapshot), not during interactive move/resize |
| `presentation_width/height` | On **`WM_SIZE`** (each client size change, via events) |
| `mobile_current` | `WM_ACTIVATE` when surface activated; forced on stop for foreground |

### Presentation size vs desktop bounds

- **Presentation size** = client area; drives `IsWide()` and button layout (model-backed, published).
- **Desktop bounds** = outer `GetWindowRect` frame; persisted for restore; **not** fed back to HWND during normal operation.

### Stale publication guard (demo)

No ack/history/serial queues. Coarse coalescing: model publishes when GUI channel not busy; during drag, desktop bounds are not in the model at all, so there is no mirror-driven `SetWindowPos` loop on the main window.

## Contrast with Host/Admin (`apptraverse` UI shell @ same SHA)

| Aspect | surfaces_demo | Host/Admin (`WinDesktopWindowPresenter`) |
| --- | --- | --- |
| Frame during drag | OS owns window; model unaware | `WM_ENTERSIZEMOVE` / `WM_EXITSIZEMOVE`, `WM_WINDOWPOSCHANGED` → `HandleNativeFrameChanged` / `TrackInteractiveNativeFrame` |
| Model updates | Bounds at exit; presentation on `WM_SIZE` | `SubmitFrameFromNative` → `ReportUiDesktopWindowFrameFromNativeEvent` (during/after move) |
| Mirror → HWND | No `SetWindowPos` on main frame from publication | `ApplyRequestedFrameFromMirror()` for **requested** frames only; 21f4524 explicitly avoids `SetWindowPos` on **report** publications |
| `OnModelChanged` | Layout child controls | Title sync; frame via sidecar / `ApplyRequestedFrameFromMirror`, not blind mirror `SetWindowPos` |

Host/Admin reuse candidates (conceptual, not copied in this pass): `ModelObjectProxy`, publication channel, `UiDesktopWindow` Report vs Request split, `ApplyRequestedFrameFromMirror` + `interactive_move_active_` gating — **not** surfaces_demo’s “only flush bounds on stop” policy.

## Reuse recommendation (for apptraverse-app)

Adopt **interaction gating** and **Report vs Request** semantics from the UI shell, not surfaces_demo’s shutdown-only bounds flush, if the goal is to stop mirror-driven jumps during drag while still persisting geometry. surfaces_demo avoids the bug class by **not** syncing frame to model during drag at all.

## Next step to unblock GUI smoke

Fix `DirectoryDomainStorage` / object load round-trip on Windows until `apptraverse_directory_domain_storage_test` passes; then re-run `scripts/surfaces_demo_smoke_test.ps1` unchanged.
