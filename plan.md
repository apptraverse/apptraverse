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
3. dynamic_objects_demo — Add Item **[THIS ITERATION]**
4. dynamic_objects_demo — Remove Item
5. shared_node_demo — two independent domains + memory messages
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

## Current slice

Dynamic object creation: model `ItemList` commits `AddItemEvent` carrying a
pre-created `Item::ptr` (stable ObjId on replay). Incremental publication uses
`SerializeStructuralNodePublication` / `ApplyStructuralPublication` so newly
reachable objects enter the GUI Domain. `InitializeNewPresenters` activates
only presenters that are not yet `presentation_loaded`.

```
Application
 └── MainWindow
      └── ItemList          (Node; journal of AddItemEvent)
           ├── Item         (ae::Obj; number)
           │    └── ItemPresenter → Win32ItemPresenter
           └── ItemListPresenter → Win32ItemListPresenter
```

Constraints for this slice:

- GUI Add → `AddItemCommand` → model thread → Event → journal → Apply
- no direct model mutation from GUI handlers
- no Delete / Remove
- no SharedNode / network / presence / chat
- no resize-style coalescing of Add (deque of discrete commands)
- no disk Save on Add; shutdown Save only
- ItemList keeps default unlimited journal retention (replay/restart)

## Foundation still in force

Object-graph presenter and MainWindow resize path remain the regression base.
See earlier sections in Progress.md for retention and window-changed details.

Journal retention (summary): `JournalRetentionPolicy`, compaction before
shutdown Save; MainWindow `max_events=0`. Not changed by this slice except
that dynamic ItemList does **not** adopt MainWindow's retain-none policy.

## Later stages (deferred; architecture unchanged)

1. Resize events — implemented/verified, not accepted
2. dynamic_objects_demo Remove Item (next after Add verification)
3. shared_node_demo …
4. Node execution / model update loop / marquee (after chat-critical path)
5. Minimal redraw / dirty regions
6. System events: DPI, screens
7. Æther / presence / network
8. Platform ports
