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
6. Disable RTTI + invariant-driven coding policy [done — THIS]
7. surfaces_demo — common model + headless **[NEXT]**
8. surfaces_demo — Windows minimal multi-window
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

RTTI off + invariant policy:

- `/GR-` (MSVC) / `-fno-rtti` (GCC/Clang) via `apptraverse_compile_policy`
- compile-time `no_rtti.h` guard
- no `dynamic_cast` / compiler RTTI in AppTraverse-owned code
- Session no longer repairs `ItemList::window` (pre-v1 state → re-distill)
- permanent rules in `.cursor/rules/apptraverse-coding-agent.mdc`

## Next slice

`surfaces_demo` common model + headless only (separate prompt).

```
Application
 └─ Surfaces : Node
      └─ surfaces[]
           ├─ Surface → SurfacePresenter
           ├─ Surface → SurfacePresenter
           └─ ...
```

**Desktop (later Windows slice):** one Surface = one top-level native window;
Add button creates another Surface/window; native close removes that Surface.
No resize persistence, no Z-order, no DPI in the minimal demo.

**Mobile (later):** same Surface objects as pager pages; UI `[ Add ] [ Remove current ]`.
Swipe/pager may come in the mobile port slice.

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
event semantics.

## Foundation still in force

Independent Model/GUI Domains, Event-only Node mutation, presentation_load_order,
structural keepalive, CLOSE_WINDOW, shutdown drain, distill separation,
`APPTRAVERSE_BUILD_AETHER_DEMOS`, no RTTI, invariant-driven checks.
MainWindow resize path remains regression base.
