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
7. surfaces_demo — common model + headless [done — THIS]
8. surfaces_demo — Windows minimal multi-window **[NEXT]**
9. surfaces_demo — macOS desktop port
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

`surfaces_demo` common model + headless:

```
Application
 └── surfaces → Surfaces : Node
      └── Surface : Node → SurfacePresenter
```

- Add / Remove via `Surface::AddSurface` / `Surface::Remove` and Events only
- `SurfacePresenter::AddClick` / `RemoveClick` → `ModelObjectProxy` (no `current_surface` in model)
- Dynamic `Surface : Node` structural publication proven headless
- No platform GUI

## Next slice

**Windows minimal multi-window** (separate prompt):

Each Surface = one identical top-level window:

```
+------------------+
| Surface N         |
| [ Add ]           |
+------------------+
```

- Add → new Surface → new identical window
- Native close (X) → `RemoveClick` of that SurfacePresenter
- No resize persistence, no Z-order, no DPI

**Mobile later:** pager pages; UI `[ Add ] [ Remove current ]`.
"Remove current" is presentation-side: pager picks the current page's
`SurfacePresenter` and calls `RemoveClick()`. Model has no `current_surface`.

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
structural keepalive, CLOSE_WINDOW, shutdown drain, distill separation,
`APPTRAVERSE_BUILD_AETHER_DEMOS`, no RTTI, invariant-driven checks.
MainWindow resize path remains regression base.
