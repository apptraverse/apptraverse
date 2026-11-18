# macOS SwiftUI presentation

WindowCatalog and MinimalApp use the shared `MacApplicationScenes` scene from
their SwiftUI `App` entry points. The scene owns windows, lists, buttons, and
commands. Thin C++ presenters and the Foundation bridge publish display state
and send actions through `ModelObjectProxy`. Each application composition
identifies its primary model window and supplies its own close and quit policy.

On macOS 14 or newer, child presenters map to distinct SwiftUI windows. The host
uses `openWindow` and `dismissWindow` with scene identities. On macOS 13,
secondary presentations appear as independently selectable tabs in the primary
window so model-driven close works without AppKit. A model close unmounts the
tab or dismisses the corresponding scene. A user close goes through the
application's policy. Quit drains model work and reports its exit status after
presenters have been torn down.

The model retains its semantic desktop geometry. The SwiftUI host renders the
size portion of explicit frame requests and observes client viewport dimensions
in points. SwiftUI owns screen placement. Scene activation supplies the model's
application-level ordering; the host does not read screen coordinates, monitor
work areas, or the system window stack. OS window restoration cannot rebind
child presenters from another process, since child scene IDs are valid for one
mount only.

`ctest -R '^apptraverse_macos_'` exercises the adapter boundaries, the
WindowCatalog fresh and embedded/cold flows, and independent MinimalApp
presentation and teardown. The integration cases use real SwiftUI scenes and
invoke control actions through their adapters.
