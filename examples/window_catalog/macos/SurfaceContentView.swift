import AppKit
import SwiftUI

// Per-Surface window content. Stateless: the model graph is the state, and
// every button press goes back through MacSurfaceActions → MacSurfacePresenter.
// `isWide` comes from SurfacePresenter::IsWide after model publication.
struct SurfaceContentView: View {
  let actions: any MacSurfaceActions
  let isWide: Bool

  var body: some View {
    VStack(alignment: .leading) {
      if isWide {
        HStack(spacing: 8) {
          Button("Add") { actions.addSurface() }
            .accessibilityIdentifier("surface.add")
          Button("Close this window") { actions.closeThisWindow() }
            .accessibilityIdentifier("surface.close")
        }
      } else {
        VStack(alignment: .leading, spacing: 8) {
          Button("Add") { actions.addSurface() }
            .accessibilityIdentifier("surface.add")
          Button("Close this window") { actions.closeThisWindow() }
            .accessibilityIdentifier("surface.close")
        }
      }
      Spacer()
    }
    .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
    .padding(12)
  }
}

@_cdecl("ApptraverseInstallMacSurfaceContent")
public func installMacSurfaceContent(_ window: NSWindow,
                                     _ actions: any MacSurfaceActions,
                                     _ isWide: Bool) {
  window.contentView = NSHostingView(
      rootView: SurfaceContentView(actions: actions, isWide: isWide))
}

@_cdecl("ApptraverseUpdateMacSurfaceContent")
public func updateMacSurfaceContent(_ window: NSWindow, _ isWide: Bool) {
  // Replace contentView: mutating rootView can leave prior AppKit button
  // hosts in the hierarchy alongside the new layout.
  let hosting = window.contentView as! NSHostingView<SurfaceContentView>
  let actions = hosting.rootView.actions
  window.contentView = NSHostingView(
      rootView: SurfaceContentView(actions: actions, isWide: isWide))
}
