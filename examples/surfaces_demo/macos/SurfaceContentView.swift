import AppKit
import SwiftUI

// Per-Surface window content. Stateless: the model graph is the state, and
// every button press goes back through MacSurfaceActions → MacSurfacePresenter.
struct SurfaceContentView: View {
  let actions: any MacSurfaceActions

  var body: some View {
    VStack(alignment: .leading) {
      HStack(spacing: 8) {
        Button("Add") { actions.addSurface() }
          .accessibilityIdentifier("surface.add")
        Button("Close this window") { actions.closeThisWindow() }
          .accessibilityIdentifier("surface.close")
      }
      Spacer()
    }
    .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
    .padding(12)
  }
}

@_cdecl("ApptraverseInstallMacSurfaceContent")
public func installMacSurfaceContent(_ window: NSWindow,
                                     _ actions: any MacSurfaceActions) {
  window.contentView = NSHostingView(
      rootView: SurfaceContentView(actions: actions))
}
