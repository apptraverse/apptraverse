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
5. foundation hardening for multi-window / mobile surfaces [done — THIS]
6. surfaces_demo — common model **[NEXT]**
7. surfaces_demo — Windows: multiple dynamic windows + resize + Z-order
8. surfaces_demo — first mobile pager port
9. remaining platform ports
10. shared_node_demo
11. chat_demo
12. aeroadmin-x production chat

Deferred relative to surfaces/chat:

- Node execution / marquee demo
- Resource / version / cache
- Six-platform ports beyond surfaces path
- DPI / screen system events

## Current slice (just completed)

Foundation hardening for upcoming cross-platform `surfaces_demo`:

- Presenter `presentation_load_order` (OnLoad parent→child, OnUnload reverse)
- Explicit `ItemList` → `MainWindow` graph parent (no fixed ObjId lookup)
- Win32 class registration at WinApp lifetime (multi HWND / one class)
- `WM_APPTRAVERSE_CLOSE_WINDOW` identity; app decides stop
- Generic structural keepalive + `Presenter::ptr` ownership
- Shutdown drains accepted commands before Save
- Distill TU separated from load-only
- `APPTRAVERSE_BUILD_AETHER_DEMOS` gates full networking client targets
- `NOMINMAX` / `WIN32_LEAN_AND_MEAN` only on WIN32

Storage I/O remains assumed infallible (out of scope).

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

Do not implement surfaces_demo in the foundation-hardening commits.

## Foundation still in force

Add/Remove Item discrete commands, independent Model/GUI Domains, Event-only Node
mutation, MainWindow resize path as regression base.
