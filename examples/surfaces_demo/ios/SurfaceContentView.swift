import ObjectiveC
import SwiftUI
import UIKit

// One Surface page. Stateless: caption and tint are derived from the Surface by
// IOSSurfacePresenter and handed over at install time.
struct SurfacePageView: View {
  let title: String
  let hue: Double

  var body: some View {
    Text(title)
      .font(.system(size: 28))
      .frame(maxWidth: .infinity, maxHeight: .infinity)
      .background(Color(hue: hue, saturation: 0.18, brightness: 1.0))
  }
}

// Host bottom bar. `canRemove` mirrors RemovableFromPager and is re-supplied on
// every publication, so the model stays the only source of truth.
struct SurfaceBarView: View {
  let actions: any IOSSurfaceActions
  let canRemove: Bool

  var body: some View {
    HStack {
      Button("Add") { actions.addSurface() }
      Spacer()
      Button("Remove current") { actions.removeCurrentSurface() }
        .disabled(!canRemove)
    }
    .padding(.horizontal, 16)
    .frame(maxWidth: .infinity, maxHeight: .infinity)
  }
}

// iOS has no public UIHostingView, and a UIHostingController is not retained by
// its own view. Attaching it to the container it fills ties its lifetime to the
// container UIKit already owns and lets the update entry point find it again.
// Only the address of this variable is used.
private var hostingControllerKey: UInt8 = 0

private func install<Content: View>(_ root: Content, into container: UIView) {
  let hosting = UIHostingController(rootView: root)
  // The container is frame-laid-out by UIKit (RelayoutPages /
  // viewDidLayoutSubviews); constraints let the content follow any frame.
  hosting.view.translatesAutoresizingMaskIntoConstraints = false
  container.addSubview(hosting.view)
  NSLayoutConstraint.activate([
    hosting.view.leadingAnchor.constraint(equalTo: container.leadingAnchor),
    hosting.view.trailingAnchor.constraint(equalTo: container.trailingAnchor),
    hosting.view.topAnchor.constraint(equalTo: container.topAnchor),
    hosting.view.bottomAnchor.constraint(equalTo: container.bottomAnchor),
  ])
  objc_setAssociatedObject(container, &hostingControllerKey, hosting,
                           .OBJC_ASSOCIATION_RETAIN_NONATOMIC)
}

@_cdecl("ApptraverseInstallIOSSurfacePage")
public func installIOSSurfacePage(_ container: UIView, _ title: NSString,
                                  _ hue: Double) {
  install(SurfacePageView(title: title as String, hue: hue), into: container)
}

@_cdecl("ApptraverseInstallIOSSurfaceBar")
public func installIOSSurfaceBar(_ container: UIView,
                                 _ actions: any IOSSurfaceActions,
                                 _ canRemove: Bool) {
  install(SurfaceBarView(actions: actions, canRemove: canRemove),
          into: container)
}

@_cdecl("ApptraverseUpdateIOSSurfaceBar")
public func updateIOSSurfaceBar(_ container: UIView, _ canRemove: Bool) {
  let hosting = objc_getAssociatedObject(container, &hostingControllerKey)
      as! UIHostingController<SurfaceBarView>
  hosting.rootView = SurfaceBarView(actions: hosting.rootView.actions,
                                    canRemove: canRemove)
}
