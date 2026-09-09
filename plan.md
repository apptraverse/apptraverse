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
- shared `ModelSession` has no platform handles (`HWND`/`HANDLE`/`void*` stand-ins)
- `Run(std::function<void()> on_published)` is a required production boundary:
  called on the model thread after the serialized buffer is published, without
  holding `mu`; Windows posts to the notify HWND; tests signal their waiter
- return from `Run` means Application, reachable graph, Domain, and storage
  have already been destroyed on the model thread; Windows `SetEvent` is after
  that return, in the thread lambda, not in `ModelSession`
- Windows `FatalWin32(operation, DWORD)` is the reused example helper for
  required Win32 failures; it is not a logging framework
- GUI never creates, loads, or touches model Domain objects
- first launch (development build with `APPTRAVERSE_ENABLE_DISTILLATION`):
  create → distill → destroy graph/Domain → new Domain → load
- load-only / production executable: load existing persisted state only;
  missing or broken state is fatal
- initial GUI mirror via serialized publication buffer, not
  `CopyModelGraphToUiDomain` from the GUI thread
- shared class registry: model Domain may also materialize
  `Win32MainWindowPresenter`; construction/Load must not create HWND
- startup is not cancelable; Loading is not a user window
- shutdown only after the model is loaded and Main exists; no TerminateThread

Out of scope for this slice: chat, contacts, Aether, presence, resize events,
node periodic execution, hierarchical redraw, shared-sync, network,
DPI/screen system events, WindowChangedEvent, periodic model tick.

## Later stages (deferred; architecture unchanged)

TODO: move `LoadStoredAncestorLayers` into aether-objects. App Traverse
still reloads stored ancestor class layers after LoadRoot when the
persisted graph has only a base class and the registry created a descendant.
Do not redesign that pass in a main-window lifecycle slice.

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
