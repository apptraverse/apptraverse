Status: active
Progress: Progress.md

# App Traverse — next application

This plan sequences a new minimal application on App Traverse.
Architecture of later stages is recorded here and is not to be redesigned
in a slice that does not own that stage.

Status vocabulary: implemented / verified / accepted-by-user are distinct.
Do not mark accepted.

## Current slice

Infra (this iteration): one user-level App Traverse MCP server accepts an
explicit `source_dir` so build/test/process tools can target a git worktree.
Later application stages below are unchanged.

**Loading window, model thread, distilled Application/MainWindow, serialized
initial GUI mirror, shutdown.**

Constraints for this slice:

- exactly two threads: Windows GUI thread and model thread
- GUI never creates, loads, or touches model Domain objects
- first launch: create → distill → destroy graph/Domain → new Domain → load
- subsequent launch: load existing state, no second distillation
- initial GUI mirror via serialized publication buffer (same path as later
  incremental updates), not `CopyModelGraphToUiDomain` from the GUI thread
- presenter (GUI thread only) creates one empty native main window from the
  mirrored MainWindow bounds
- correct stop during Loading and after Ready; no TerminateThread

Out of scope for this slice: chat, contacts, Aether, presence, resize events,
node periodic execution, hierarchical redraw, shared-sync, network,
DPI/screen system events.

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
