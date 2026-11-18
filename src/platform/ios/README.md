# iOS SwiftUI presentation

WindowCatalog and MinimalApp use the same SwiftUI host for content, lists,
buttons, menus, and page selection. Thin C++ presenters and a Foundation bridge
publish complete control graphs. A row gesture resolves the displayed row
identity against the current publication before sending its model intent.

The host renders the model's active page. Page buttons and swipes submit intents
through the model proxy. WindowCatalog supplies its navigation ring, and
MinimalApp supplies its single active main or Detail page.

SwiftUI measures controls in points and uses the same Apple constraint layout
as macOS. It solves against the live safe-area viewport and scrolls if the
required minimum size exceeds that viewport. The backend observes the current
viewport and device settings in LOCAL. Native rotation leaves portable window
geometry to the model.

SwiftUI scene deactivation queues the application's save behind accepted model
work. A background suspension assertion allows the USER and LOCAL files to
finish writing. Foreground activation retains the bridge, model, and view
adapters. Explicit stop drains the model and releases the presenters. Fresh Debug
startup uses a new profile, and the explicit embedded consumer can cold-load the
matching supply and stored state.
