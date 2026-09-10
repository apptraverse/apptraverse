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

## Roadmap (surfaces before SharedNode)

1. main_window_runtime_demo — lifecycle/mirror foundation [done]
2. journal retention/compaction [landed]
3. dynamic_objects_demo — Add / Remove Item [done]
4. foundation hardening + UI ownership / ObjId proxy [done]
5. dynamic_objects cleanup (invariants / Win32 routing) [done]
6. Disable RTTI + invariant-driven coding policy [done]
7. surfaces_demo — common model + headless [done]
8. surfaces_demo — Windows minimal multi-window [done — THIS]
9. surfaces_demo — macOS desktop port **[NEXT]**
10. surfaces_demo — Linux desktop port
11. surfaces_demo — iOS
12. surfaces_demo — Android
13. surfaces_demo — WASM
14. shared_node_demo
15. chat_demo
16. aeroadmin-x production chat

Deferred relative to surfaces/chat:

- Node execution / marquee demo
- Resource / version / cache
- DPI / screen system events
- Surface resize persistence / Z-order (not in minimal surfaces demo)

## Current slice (just completed)

Windows minimal multi-window:

```
SurfacePresenter
  ↓
DesktopSurfacePresenter
  ├─ Win32SurfacePresenter   [done]
  ├─ MacSurfacePresenter     [later]
  └─ LinuxSurfacePresenter   [later]
```

Mobile later (not created yet):

```
SurfacePresenter
  ↓
MobileSurfacePresenter
  ├─ IOSSurfacePresenter
  └─ AndroidSurfacePresenter
```

- one Surface = one top-level HWND; Add button; X removes that Surface
- last-window X = app exit without Remove (Surface persisted)
- no resize/Z-order/DPI

## Next slice

**macOS desktop port** — `MacSurfacePresenter : DesktopSurfacePresenter`.

**Mobile later:** pager; `[ Add ] [ Remove current ]` selects current
`SurfacePresenter` on the presentation side. Model has no `current_surface`.

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
structural keepalive, CLOSE_WINDOW / last-window STOP, shutdown drain, distill
separation, no RTTI, invariant-driven checks.
