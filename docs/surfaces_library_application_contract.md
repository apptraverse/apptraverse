# Surfaces library — application contract (not universal API)

The `apptraverse_surfaces` / `apptraverse_surfaces_windows` libraries ship model types,
portable presenter bases, Win32 presenter implementations, and wiring helpers. They do
**not** embed a complete product graph or session lifecycle.

## Host-provided symbols

### `AttachPlatformSurfacePresenters`

Declared in `apptraverse/surfaces/surfaces_presentation.h`, **defined by the host**
(demo, test, or product). It must create platform presenter objects for a `Surface`,
wire `surface.chrome.*`, and call `WireSurfaceChromeLocalization`.

The library never implements this function; linking `apptraverse_surfaces` requires
exactly one host definition when `Surface::AddSurface` or distillation is used.

### Fixed `Application` lookup in `Surface::AddSurface`

`Surface::AddSurface()` (model thread) resolves the owning application with a **fixed**
`ObjId` from `apptraverse/surfaces/surfaces_ids.h` (`surfaces_demo::ObjId::Application`).
It then calls `AttachPlatformSurfacePresenters` on the new sibling.

This is **surfaces-demo schema**, not a generic “find root Application” API. Hosts with
a different graph layout must not call `AddSurface` unchanged, or must use the same ObjId
layout and presenter hook.

## What the library provides

- Model: `Application`, `Surfaces`, `Surface`, events, localization install helpers.
- Presenters: portable bases + `Win32Surface*Presenter` in `apptraverse_surfaces_windows`.
- GUI bridge: `InitializePresenters` / `UnloadPresenters` on `apptraverse` (not Surfaces-specific).

## What the host provides

- Initial graph construction (ObjIds, distillation, retention).
- `AttachPlatformSurfacePresenters` implementation.
- Session / publication loop (e.g. `SurfacesModelSession` in the demo).
- Platform entry (`WinApp`, notify HWND, shutdown policy).
- Win32: call `RegisterSurfacesWin32Classes()` before `InitializePresenters`, and
  `UnregisterSurfacesWin32Classes()` after `UnloadPresenters` (with a message pump so
  HWND teardown completes).
- Non-null `ModelObjectProxy` when Win32 surface windows are shown: `WM_SIZE` during
  `InitializePresenters` posts presentation size via the proxy (same-domain synchronous
  enqueue is enough for tests).

## Independence check

`apptraverse_surfaces_windows` must not include or link paths under `examples/`.
Win32 fatal errors use `apptraverse/platform/windows/win32_fatal.h` in `apptraverse_ui_windows`.

See `apptraverse_surfaces_win32_library_consumer_test` for a minimal host that links only
library targets and supplies its own `AttachPlatformSurfacePresenters`.
