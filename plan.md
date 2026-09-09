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
6. UI control ownership + GUI→model ObjId proxy [done — THIS]
7. surfaces_demo — common model **[NEXT]**
8. surfaces_demo — Windows: multiple dynamic windows + resize + Z-order
9. surfaces_demo — first mobile pager port
10. remaining platform ports
11. shared_node_demo
12. chat_demo
13. aeroadmin-x production chat

Deferred relative to surfaces/chat:

- Node execution / marquee demo
- Resource / version / cache
- Six-platform ports beyond surfaces path
- DPI / screen system events

## Current slice (just completed)

Ownership of UI controls and GUI→model path (before `surfaces_demo`):

- Each logical UI object owns its Presenter; Presenter owns only that object's
  native presentation (no presenter ownership tree).
- `AddItem` / `AddItemPresenter` / `Win32AddItemPresenter` — BUTTON HWND leaves
  `MainWindowPresenter`.
- Remove `[x]` stays inside `ItemPresenter` (not a separate model button object).
- `ModelObjectProxy`: GUI Presenter → ObjId + method → model Domain Find →
  model method → Event. Session/WinApp carry generic work only.
- `AddItem::Click` / `Item::Remove` create Events; free `CommitAdd*` and typed
  Session Add/Remove commands removed.
- Dirty Node publication via `NoteMaterializedChange` notifier (no Event-type
  dispatch in Session).

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

Do not start surfaces_demo in the ownership/proxy commits.

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
