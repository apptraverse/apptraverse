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
- macOS desktop — **SwiftUI content in an AppKit window** (`MacSurfacePresenter`)
- iOS / iPhone Simulator — **SwiftUI content in a UIKit pager**
  (`IOSSurfacePresenter`)

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

## SwiftUI view layer (Apple platforms)

SwiftUI owns window/page **content**. It does not own the window set, window
lifecycle, frames, or Z-order: those follow from the model graph
(`Surfaces::surfaces`, `desktop_*`, `mobile_current`) and stay with the
presenter. `WindowGroup` is deliberately not used — it would move the window
set into SwiftUI scene storage and break both the geometry contract and
`RestoreActiveSurfaceZOrder`.

Boundary (macOS):

```
SwiftUI view (SurfaceContentView.swift)
  → id<MacSurfaceActions>   (pure Objective-C protocol; SurfaceWindowDelegate)
  → MacSurfacePresenter     (AddClick / RemoveClick / PageShown)
  → ModelObjectProxy → model event
```

Boundary (iOS), same shape through the single mobile host:

```
SwiftUI view (SurfaceContentView.swift)
  → id<IOSSurfaceActions>   (pure ObjC protocol; SurfacesRootViewController)
  → IOSApp                  (AddCurrentClick / RemoveCurrentClick)
  → current IOSSurfacePresenter → ModelObjectProxy → model event
```

Two constraints fix this shape:

- Swift's clang importer is Xcode's (Apple Clang 15) and cannot parse pinned
  `aether-miscpp` C++, while the C++ needs `clang++-mp-20`. So the
  Swift-visible header is pure Objective-C and all C++ stays in `.mm`.
- The single ObjC++ → Swift call is `@_cdecl`
  (`ApptraverseInstallMacSurfaceContent`): it installs an `NSHostingView` into
  the presenter's window. No generated `-Swift.h` (that header needs clang
  modules, unavailable under `clang++-mp-20`) and no object ownership crosses
  the boundary.
- Swift targets link with the ObjC++ driver (`LINKER_LANGUAGE OBJCXX`); the
  Swift driver rejects the C++ policy flags such as `-fno-rtti`.

iOS adds three constraints of its own:

- The pager stays UIKit. `TabView(.page)` or a SwiftUI app lifecycle would move
  page order and the current page into SwiftUI state, breaking `mobile_current`
  identity and the desired-id reconcile that `IOSApp` uses for swipe races.
  ObjC++ keeps every container `UIView` and its frame (`RelayoutPages`,
  `viewDidLayoutSubviews`); SwiftUI only draws inside a container.
- iOS has no public `UIHostingView`, so content is a `UIHostingController`,
  which its own view does not retain. It is attached with
  `objc_setAssociatedObject` to the container UIKit already owns: that ties its
  lifetime to the container and lets the stateless update entry point
  (`ApptraverseUpdateIOSSurfaceBar`) find it again without global Swift state.
- Model-derived control state is re-supplied, never mirrored. Every publication
  passes `RemovableFromPager()` back in, so `Remove current` has no Swift state.

SwiftUI macOS buttons are private `NSControl` subclasses with no title and no
`accessibilityIdentifier` on the `NSView`, and SwiftUI builds its accessibility
tree only for an attached AX client. Native smoke tests therefore drive
`MacSurfaceActions` (the production boundary SwiftUI calls) and keep native
assertions for window geometry, key window, and Z-order.

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
- The iOS bundle has no launch storyboard, so iOS runs it scaled from a 320×480
  logical screen and in the light appearance. Layout and the SwiftUI content are
  correct inside that box; native full-screen geometry is a separate slice.
- iOS has no user-driven application close, and `simctl terminate` does not
  deliver `applicationWillTerminate`, so `Application::Save` does not run on a
  simulator kill: the newest topology change reloads from the earlier runtime
  write. Pre-existing, identical before the SwiftUI port.

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
