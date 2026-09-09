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

## Chat-critical roadmap (AeroAdmin-X)

1. main_window_runtime_demo — lifecycle/mirror foundation [done / regression base]
2. journal retention/compaction [landed]
3. dynamic_objects_demo — Add Item [done]
4. dynamic_objects_demo — Remove Item [done]
5. shared_node_demo — two independent domains + memory messages **[NEXT]**
6. shared_node_demo — ACK/dedup/reorder
7. shared_node_demo — presence/offline
8. shared_node_demo — unload/reload
9. chat_demo — minimal App Traverse chat
10. aeroadmin-x — production chat integration

Deferred relative to chat (still roadmap, not blocking):

- Node execution / marquee demo
- Resource / version / cache
- Six-platform ports
- DPI / screen system events

## Current slice (just completed)

Dynamic object removal: live `ItemList::items` loses the Item via
`RemoveItemEvent`; historical `AddItemEvent` may still hold the Item.
GUI shows live topology only. Structural publication + selective
`OnUnload` / `OnLoad` update presenters without recreating survivors.

```
Application
 └── MainWindow
      └── ItemList          (Node; journal of Add/Remove Events)
           ├── Item         (ae::Obj; number — display only)
           │    └── ItemPresenter → Win32ItemPresenter ([x])
           └── ItemListPresenter → Win32ItemListPresenter
```

Constraints observed:

- GUI Remove → `RemoveItemCommand{ObjId}` → model → `RemoveItemEvent` → Apply
- no model pointer across domains/threads
- no physical GC of historical Item / journal
- no SharedNode / network / presence / chat
- discrete Add/Remove commands (variant deque; not coalesced)
- no disk Save on Remove; shutdown Save only
- ItemList keeps default unlimited journal retention

## Next slice

`shared_node_demo`: two independent Application/Domain/Storage in one process
+ in-memory message transport. No ACK, retry, or presence yet.

## Foundation still in force

Object-graph presenter and MainWindow resize path remain the regression base.
See Progress.md for retention and window-changed details.

Journal retention (summary): `JournalRetentionPolicy`, compaction before
shutdown Save; MainWindow `max_events=0`. Dynamic ItemList does **not**
adopt MainWindow's retain-none policy.

## Later stages (deferred; architecture unchanged)

1. Resize events — implemented/verified, not accepted
2. shared_node_demo (see roadmap)
3. Node execution / model update loop / marquee (after chat-critical path)
4. Minimal redraw / dirty regions
5. System events: DPI, screens
6. Æther / presence / network
7. Platform ports
