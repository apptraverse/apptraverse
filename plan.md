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
3. dynamic_objects_demo — Add Item [done]
4. dynamic_objects_demo — Remove Item [done]
5. foundation hardening for multi-window / mobile surfaces [done]
6. UI control ownership + GUI→model ObjId proxy [done]
7. dynamic_objects_demo final cleanup (invariants / Win32 routing) [done — THIS]
8. surfaces_demo — common model **[NEXT]**
9. surfaces_demo — Windows: multiple dynamic windows + resize + Z-order
10. surfaces_demo — first mobile pager port
11. remaining platform ports
12. shared_node_demo
13. chat_demo
14. aeroadmin-x production chat

Deferred relative to surfaces/chat:

- Node execution / marquee demo
- Resource / version / cache
- Six-platform ports beyond surfaces path
- DPI / screen system events

## Current slice (just completed)

Final cleanup of `dynamic_objects_demo` before `surfaces_demo`:

- `ReadyForPresentation` checks only parent `presentation_loaded`
- Broken required object relations are invariants (not "not ready")
- No `dynamic_cast` in dynamic demo / ModelObjectProxy production path
- `Ptr::as<T>()` / `ObjPtr` hierarchy conversion / Registry
  `GenerationDistance` for Presenter walk
- Generic Win32 `DispatchChildCommand` → `Win32Presenter::OnCommand`
- `CLOSE_WINDOW` → direct `RequestStop` (single MainWindow demo)
- Stale `Item::Remove` remains the only Remove no-op

## Next slice

`surfaces_demo` common model only (separate prompt). Direction:

```
Application
 └── SurfaceList / Surfaces Node
      ├── Surface A : Node
      ├── Surface B : Node
      └── ...
```

Desktop: one Surface → one top-level window; dynamic create/delete; move/resize Events;
logical Z-order separate. Mobile: same Surface objects → pager pages; swipe → active
Surface Event; page order ≠ activation ≠ desktop Z-order.

Do not start surfaces_demo in the cleanup commits.

## Known follow-ups (not this slice)

- `Node::SetMaterializedChangeNotifier` is still a process-global static. Must
  become per-Domain / per-Application before two independent sessions in
  `shared_node_demo`.
- Multi-dirty / dynamic Node publication protocol belongs to the first
  headless `surfaces_demo` slice (first dynamically created `Surface : Node`).

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
event semantics. Same pattern later for Surface / AddSurface presenters.

## Foundation still in force

Independent Model/GUI Domains, Event-only Node mutation, presentation_load_order,
structural keepalive, CLOSE_WINDOW, shutdown drain, distill separation,
`APPTRAVERSE_BUILD_AETHER_DEMOS`. MainWindow resize path remains regression base.
