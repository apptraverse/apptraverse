# macOS SwiftUI presentation

WindowCatalog and MinimalApp use the shared `MacApplicationScenes` from their
SwiftUI `App` entry points. SwiftUI owns content, lists, buttons, commands, and
separate `WindowGroup` scenes. A small native attachment disables automatic
tabbing and supplies window close, activation, screen geometry, and placement.
Normal user close calls the application's close policy. Model unload removes
its matching native window, including when WindowCatalog deletes an open row.

The bridge publishes immutable constraint snapshots. SwiftUI measures its
controls and reuses the common C++ constraint solver with the current viewport
in points. The layout can therefore switch between horizontal and vertical
arrangements during resize without portable frame round-trips. A scroll view
preserves controls when the required minimum exceeds the available viewport.

Desktop placement is stored in LOCAL as outer-window geometry in logical
points relative to the work area. Native events submit observations into
`UiDeviceState`; they don't synthesize portable window-frame requests. Explicit
semantic requests still move and resize the native window. The platform uses
the current screen scale for DPI metadata and restores LOCAL monitor placement
through the common monitor resolution events. WindowCatalog's language and
theme commands come from the same distilled menu as Windows.

`ctest -R '^apptraverse_macos_'` exercises both applications on actual SwiftUI
scenes, including native resize, separate windows, close, deletion, and cold
restore. The control test drives intents through the same adapters used by the
SwiftUI views.
