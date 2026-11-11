# iOS native presentation

The generic backend uses thin C++ Window/Button/List presenters, an Objective-C++
bridge, SwiftUI content, and a native UITableView for list tracking and selection.
The application supplies model runtime/session, UI preparation and publication
hooks, viewport intents and a model-authored ordered mobile-page snapshot.

The host renders only the model's active page. Horizontal swipes and UIPageControl
input become model intents; the host does not decide which catalog window opens,
closes or becomes active. Buttons and list rectangles are the model's rectangles
in logical points, measured inside the actual safe-area client viewport.

Native callbacks route presenter -> proxy -> model Events. Model/mirror values
remain the authority for row selection, titles, enabled state, geometry and page
identity. UIKit selection and activation recognizers do not compete with SwiftUI
row gestures. Runtime tokens and view adapters are never persistent model fields.

The host observes native UIApplication deactivation, background and activation
notifications independently of the SwiftUI view. Deactivation queues the
application's `persist_model` callback behind already accepted model work.
It uses the continuing model thread and Domain; neither the runtime, mirror,
presenters nor view adapters are torn down. One UIKit background task covers
pending saves. Background after the same deactivation is coalesced; a new
active-to-inactive transition queues another save after newer accepted work.
Activation continues the same live session and UI. Save completion has its own
status, separate from model exit. Explicit `requestStop` retains the normal
drain/save/join/teardown path. No per-click flush, UI-mirror persistence or new
checkpoint format is introduced. A suspended app need not receive a termination
callback; forced process termination cannot guarantee new I/O. The application
decides fresh Debug process startup versus embedded cold USER loading.

Queued button intents resolve their model target when executed. An already
unloaded child Close control is skipped as a real lifecycle disappearance;
the backend never reconstructs its graph to execute a late tap.
