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

Included and verified on that SHA:

- Windows desktop (multi-window, geometry persistence)
- Android / Android Emulator (pager, `mobile_current`)
- Web / Emscripten / WASM (tabs, IndexedDB checkpoint)

Pending merge (other Cursors; do not treat as landed):

- Linux desktop — **GTK3 only** (`feature/surfaces-linux-v1`)
- macOS desktop (`feature/surfaces-macos-v1`)
- iOS / iPhone Simulator (`feature/surfaces-ios-v1`)

Mac Cursor owns macOS + iPhone Simulator only. Linux Cursor owns GTK3 Linux only.

## Roadmap (surfaces before SharedNode)

1. main_window_runtime_demo — lifecycle/mirror foundation [done]
2. journal retention/compaction [landed]
3. dynamic_objects_demo — Add / Remove Item [done]
4. foundation hardening + UI ownership / ObjId proxy [done]
5. dynamic_objects cleanup (invariants / Win32 routing) [done]
6. Disable RTTI + invariant-driven coding policy [done]
7. surfaces_demo — common model + headless [done]
8. surfaces_demo — Windows multi-window + persisted geometry [done]
9. surfaces_demo — Android pager + `mobile_current` [done — in `surfaces-demo`]
10. surfaces_demo — Web/WASM tabs + IDBFS checkpoint [done — in `surfaces-demo`]
11. surfaces_demo — Linux GTK3 port **[NEXT — Linux Cursor]**
12. surfaces_demo — macOS desktop port **[NEXT — Mac Cursor]**
13. surfaces_demo — iOS / iPhone Simulator **[NEXT — Mac Cursor]**
14. shared_node_demo
15. chat_demo
16. aeroadmin-x production chat

Deferred relative to surfaces/chat:

- Node execution / marquee demo
- Resource / version / cache
- DPI / screen system events
- Surface Z-order / active Surface in model (not in desktop UX)

## Canonical current Surface

Persisted identity: `Surfaces::mobile_current` (Surface reference / ObjId),
not a positional numeric index. UI may keep a runtime page index.

Required APIs: `SetCurrentSurfaceEvent`, `Surface::MakeCurrent()`,
`SurfacePresenter::PageShown()`.

Desktop leaves `mobile_current` empty for presentation; every Surface HWND is
shown. Mobile/Web report the visible page through `PageShown`.

Web checkpoints `Application::Save` after each model publication, then IDBFS
sync — browser reload/tab close is not a reliable graceful shutdown. This is
**Web-host policy only**, not common `SurfacesModelSession`, and must not be
copied onto Windows/Android.

## Presenter hierarchy

```
SurfacePresenter
  ↓
DesktopSurfacePresenter
  ├─ Win32SurfacePresenter   [in surfaces-demo]
  ├─ MacSurfacePresenter     [pending]
  └─ LinuxSurfacePresenter   [pending — GTK3]

SurfacePresenter
  ↓
MobileSurfacePresenter
  └─ AndroidSurfacePresenter [in surfaces-demo]

SurfacePresenter
  └─ WebSurfacePresenter     [in surfaces-demo; not under Mobile]
```

## Known follow-ups (not this slice)

- `Node::SetMaterializedChangeNotifier` is still a process-global static. Must
  become per-Domain / per-Application before two independent sessions in
  `shared_node_demo`.

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

## Foundation still in force

Independent Model/GUI Domains, Event-only Node mutation, presentation_load_order,
structural keepalive (`Domain::Find`), native-X app STOP + Close-button Remove,
shutdown geometry snapshot before RequestStop, distill separation, no RTTI,
invariant-driven checks.
