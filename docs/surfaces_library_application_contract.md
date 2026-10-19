# Surfaces library — application contract (not universal API)

The `apptraverse_surfaces` / `apptraverse_surfaces_windows` libraries ship **demo**
model types, portable presenter bases, Win32 presenter implementations, and wiring
helpers. They do **not** own session lifecycle, message pump, or product root choice.

Generic runtime lives in `apptraverse_application_runtime` (`ModelPublicationSession`,
`ModelObjectProxy`, `PresentationSizeQueue`, UI mirror). See `docs/library_roadmap.md`.

## Host-provided hooks (Surfaces demo)

### `SurfacePlatformAttachFn` / `platform_attach`

Runtime-only field on `Surfaces` (not serialized). The host sets
`surfaces.platform_attach` before model `AddSurface` runs (demo:
`WireSurfacesDemoPlatformBindings` → `AttachSurfacesDemoPlatformPresenters` in
`examples/surfaces_demo`, or test stubs in `tests/surfaces_test_presentation.cpp`).

No global link symbol; no `AttachPlatformSurfacePresenters` in the universal path.

### `host_application`

Runtime-only `Application*` on `Surfaces` for model-thread chrome wiring during
`AddSurface`. Set in `on_model_root_loaded` after load/bootstrap.

## What the library provides

- Model: `Application`, `Surfaces`, `Surface`, events, demo localization install.
- Presenters: portable bases + `Win32Surface*Presenter` in `apptraverse_surfaces_windows`.
- Presentation size: `PostSurfacePresentationSize` → generic `PresentationSizeQueue` on
  `ModelObjectProxy` (Surfaces binds applier in demo session config).

## What the host / demo provides

- Initial graph (ObjIds, distillation, retention) and `ConfigureSurfacesDemoModelSession`.
- Platform attach implementation and Win32 class registration.
- Platform entry (`WinApp`, notify HWND, shutdown policy) — still in demo until
  `apptraverse_win_application_host` lands (roadmap phase 1 follow-up).
- Non-null `ModelObjectProxy` bound to the session queue when Win32 windows are shown.

## Second consumer (no Surface types)

`apptraverse_minimal_runtime_consumer_test` uses the same `ModelPublicationSession`
with its own root class and ObjId (`MinimalConsumerRoot`). It does not link
`apptraverse_surfaces`.

## Independence

`apptraverse_surfaces_windows` must not include or link `examples/*`.
Win32 fatal errors: `apptraverse/platform/windows/win32_fatal.h` in `apptraverse_ui_windows`.
Local builds do not require the full Aether client SDK; `apptraverse_shared_sync` is
optional via the aggregate `apptraverse` INTERFACE link.
