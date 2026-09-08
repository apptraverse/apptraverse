Status: active
Progress: Progress.md

# App Traverse — next application

This plan sequences a new minimal application on App Traverse.
Architecture of later stages is recorded here and is not to be redesigned
in a slice that does not own that stage.

Status vocabulary: implemented / verified / accepted-by-user are distinct.
Do not mark accepted.

## Current slice

Object-graph presenter: MainWindow owns a `MainWindowPresenter` in the
aether-objects graph; the Windows executable registers
`Win32MainWindowPresenter` as the most-derived descendant. Load uses
aether-objects `DomainGraph::LoadRoot` (no App Traverse class-mapping
registry). Native HWND is created only in a GUI presentation-initialization
pass, not during object Load.

```
Application
 └── MainWindow          (Node; x, y, width, height; no DPI)
      └── presenter → MainWindowPresenter   (not a Node; no journal)
           └── window → MainWindow          (same Domain object)
                      └── Win32MainWindowPresenter  (most-derived on Windows)
```

Phases (not the same thing):

1. Object Load / GUI deserialize — construct objects, restore state, resolve
   ObjPtrs. No `Presenter::OnLoad()`, no HWND.
2. GUI presentation initialization — `InitializePresenters` walks
   reachable live objects and calls `Presenter::OnLoad()` once.
   `Win32MainWindowPresenter::OnLoad()` is where `CreateWindowExW` happens.

Presenter local state (not implemented in this slice): not journaled; not
model-owned; GUI may mutate it later; it may be persisted later. Incremental
model publication must not overwrite newer GUI presenter state without an
explicit rule. DPI / monitor / screen events are the next system-event stage,
not this one.

Constraints:

- exactly two threads: Windows GUI thread and model thread
- GUI never creates, loads, or touches model Domain objects
- first launch: create → distill → destroy graph/Domain → new Domain → load
- subsequent launch: load existing state, no second distillation
- initial GUI mirror via serialized publication buffer, not
  `CopyModelGraphToUiDomain` from the GUI thread
- shared class registry: model Domain may also materialize
  `Win32MainWindowPresenter`; construction/Load must not create HWND
- if initial publication arrives after `RequestStop`, skip presentation init
- correct stop during Loading and after Ready; no TerminateThread

Out of scope for this slice: chat, contacts, Aether, presence, resize events,
node periodic execution, hierarchical redraw, shared-sync, network,
DPI/screen system events, WindowChangedEvent, periodic model tick.

## Later stages (deferred; architecture unchanged)

1. Resize events (native resize → serialized command → model)
2. Node execution / model update loop
3. Minimal redraw / dirty regions
4. State restoration beyond the initial Application/MainWindow load
5. System events: DPI, screens
6. Æther / presence / network
7. Connections
8. Shared-sync
9. Messages
10. Platform ports
