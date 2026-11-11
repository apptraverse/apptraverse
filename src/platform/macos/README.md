# macOS native presentation

The backend uses thin C++ Window/Button/List presenters, an Objective-C++ bridge,
and a SwiftUI host inside native AppKit windows. Model commands are addressed
through `ModelObjectProxy`; only publications supply displayed state.

The application supplies its model runtime, preparation hook and publication
sidecar callbacks through `MacApplicationBridgeCallbacks`. The host does not
know WindowCatalog classes or root IDs. Focus and desktop Z-order are backend
operations addressed by logical window identity.

Desktop geometry uses top-left screen coordinates and client sizes. Initial
placement may be selected by AppKit, then actual frame/work area are reported
through model Events. A model publication changes native geometry only through
`ApplyRequestedFrameFromMirror`; reflected native reports never snap a live
window back to old geometry. Native handles, adapters and callback tokens are
runtime-only.

`MacApplicationHost.run()` returns the joined model runtime's status after
tearing down presenters. Close and Quit enqueue native frame reports before
the application's close/quit policy and runtime stop barrier.
