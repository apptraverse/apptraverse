Status: active
Progress: Progress.md

# App Traverse — next application

This plan sequences a new minimal application on App Traverse.
Architecture of later stages is recorded here and is not to be redesigned
in a slice that does not own that stage.

Status vocabulary: implemented / verified / accepted-by-user are distinct.
Do not mark accepted.

Coding-agent rules (incremental build, fail-fast, no extra entities, commit/push):
`.cursor/rules/apptraverse-coding-agent.mdc`.

## Current canonical application branch

**`surfaces-demo`** (`origin/surfaces-demo`)

Included on that branch:

- Windows desktop (multi-window, geometry persistence)
- Android / Android Emulator (pager, `mobile_current`)
- Web / Emscripten / WASM (tabs, IndexedDB checkpoint)
- Linux desktop — **X11/Xlib** (`LinuxSurfacePresenter`)
- macOS desktop — **AppKit** (`MacSurfacePresenter`)
- iOS / iPhone Simulator — **UIKit** (`IOSSurfacePresenter`)

All current platform ports are merged.

## Roadmap (surfaces before SharedNode)

1. main_window_runtime_demo — lifecycle/mirror foundation [done]
2. journal retention/compaction [landed]
3. dynamic_objects_demo — Add / Remove Item [done]
4. foundation hardening + UI ownership / ObjId proxy [done]
5. dynamic_objects cleanup (invariants / Win32 routing) [done]
6. Disable RTTI + invariant-driven coding policy [done]
7. surfaces_demo — common model + headless [done]
8. surfaces_demo — Windows multi-window + persisted geometry [done]
9. surfaces_demo — Android pager + `mobile_current` [done]
10. surfaces_demo — Web/WASM tabs + IDBFS checkpoint [done]
11. surfaces_demo — Linux X11 desktop port [done]
12. surfaces_demo — macOS desktop port [done]
13. surfaces_demo — iOS / iPhone Simulator [done]
14. pre-shared runtime hardening [done]
15. shared_node_demo — two independent headless replicas **[NEXT]**
16. chat_demo
17. aeroadmin-x production chat

Deferred relative to surfaces/chat:

- Node execution / marquee demo
- Resource / version / cache
- DPI / screen system events
- Full desktop Z-order stack (only active Surface restored on desktop;
  Windows + Linux + macOS restore via `mobile_current`)

## Canonical current Surface

Persisted identity: `Surfaces::mobile_current` (Surface reference / ObjId),
not a positional numeric index. UI may keep a runtime page index.

Required APIs: `SetCurrentSurfaceEvent`, `Surface::MakeCurrent()`,
`SurfacePresenter::PageShown()`.

Mobile/Web report the visible page through `PageShown`.

Windows, Linux, and macOS desktop also use `mobile_current` for active-window /
Z-order restore: focus → `PageShown`; on startup raise + focus the persisted
current Surface.

Web checkpoints `Application::Save` after each model publication, then IDBFS
sync — browser reload/tab close is not a reliable graceful shutdown. This is
**Web-host policy only**, not common `SurfacesModelSession`, and must not be
copied onto Windows/Android/Linux/macOS/iOS.

## Presenter hierarchy

```
SurfacePresenter
  ↓
DesktopSurfacePresenter
  ├─ Win32SurfacePresenter
  ├─ MacSurfacePresenter
  └─ LinuxSurfacePresenter   — X11/Xlib

SurfacePresenter
  ↓
MobileSurfacePresenter
  ├─ AndroidSurfacePresenter
  └─ IOSSurfacePresenter

SurfacePresenter
  └─ WebSurfacePresenter     — not under Mobile
```

## Known follow-ups (not this slice)

- Runtime base snapshot (`CaptureBaseState` / `DomainGraph::Save`) may write
  storage before explicit `Application::Save` (characterized: 2 `Store` calls
  per `InitializeRuntimeNode`). Defer Overlay flushing / persistence redesign.
- Publication scaling / full-graph cost.
- Android presenter ownership / UI weaknesses.
- Mobile lifecycle persistence limitations beyond current checkpoints.
- Linux host remains X11/Xlib (not GTK3).

## Cross-platform invariant (foundation)

```
native input
  → platform presenter
  → common presenter method
  → ModelObjectProxy(ObjId)
  → model object method
  → Event / Commit
```

Session/runtime transports work and publications; it contains no application
event semantics.

ModelWork execution is independent of GUI publication consumption.
`PublicationChannel` backpressure delays only the next GUI snapshot.

## Foundation still in force

Independent Model/GUI Domains, Event-only Node mutation, presentation_load_order,
structural keepalive (`Domain::Find`), instance-scoped Node materialized-change
notifier (no process-global callback), native-X app STOP + Close-button Remove,
shutdown geometry snapshot before RequestStop, distill separation, no RTTI,
invariant-driven checks.
