import SwiftUI

// Projection of the published UI mirror. This type holds no catalog rules:
// button and list actions call the Objective-C bridge, and the next
// publication replaces `windows`.
final class AppleCatalogModel: ObservableObject {
  let projection: AppleCatalogProjection
  @Published var windows: [AppleWindowProjection] = []
  @Published var saveStatus: String = ""
  @Published var revision: UInt64 = 0
  @Published var viewportLabel: String = ""
  @Published var sessionLabel: String = ""

  init(projection: AppleCatalogProjection) {
    self.projection = projection
    projection.onChanged = { [weak self] in
      self?.pull()
    }
    pull()
  }

  func pull() {
    windows = projection.windows
    saveStatus = projection.saveStatus
    revision = projection.revision
    viewportLabel = projection.viewportLabel
    sessionLabel = projection.sessionLabel
  }
}

#if os(macOS)
import AppKit

// NSHostingView does not publish a SwiftUI Button title into the System Events
// accessibility tree. An NSButton title does, and the action is still the
// projection callback.
private struct ProjectionButton: NSViewRepresentable {
  let title: String
  let enabled: Bool
  let identifier: String
  var value: String = ""
  let action: () -> Void

  func makeCoordinator() -> Coordinator {
    Coordinator(action)
  }

  func makeNSView(context: Context) -> NSButton {
    let button = NSButton(
      title: title,
      target: context.coordinator,
      action: #selector(Coordinator.press))
    button.bezelStyle = .rounded
    button.setButtonType(.momentaryPushIn)
    apply(button, context: context)
    return button
  }

  func updateNSView(_ button: NSButton, context: Context) {
    apply(button, context: context)
  }

  private func apply(_ button: NSButton, context: Context) {
    button.title = title
    button.isEnabled = enabled
    button.setAccessibilityIdentifier(identifier)
    button.setAccessibilityLabel(title)
    if !value.isEmpty {
      button.setAccessibilityValue(value)
    }
    context.coordinator.action = action
  }

  final class Coordinator: NSObject {
    var action: () -> Void
    init(_ action: @escaping () -> Void) { self.action = action }
    @objc func press() { action() }
  }
}
#else
private struct ProjectionButton: View {
  let title: String
  let enabled: Bool
  let identifier: String
  var value: String = ""
  let action: () -> Void

  var body: some View {
    Button(title, action: action)
      .disabled(!enabled)
      .accessibilityLabel(title)
      .accessibilityIdentifier(identifier)
      .accessibilityValue(value)
  }
}
#endif

private func catalogButtonIdentifier(_ controlId: UInt32) -> String {
  // Control ids are the shared WindowCatalog graph contract
  // (kAddControlId / kOpenControlId / kDeleteControlId / kLanguageControlId).
  switch controlId {
  case 5002:
    return "catalog.button.add"
  case 5003:
    return "catalog.button.open"
  case 5004:
    return "catalog.button.delete"
  case 5005:
    return "catalog.button.language"
  default:
    return "catalog.button.\(controlId)"
  }
}

struct CatalogWindowPage: View {
  @ObservedObject var model: AppleCatalogModel
  let windowId: UInt64

  private var window: AppleWindowProjection? {
    model.windows.first { $0.windowId == windowId }
  }

  var body: some View {
    if let window {
      VStack(alignment: .leading, spacing: 8) {
        Text(window.title)
          .accessibilityIdentifier("catalog.window.\(window.windowId).title")
        Text("\(window.frameX),\(window.frameY),\(window.frameWidth),\(window.frameHeight)")
          .accessibilityIdentifier("catalog.window.\(window.windowId).frame")
        if window.catalogChrome {
          chrome(window)
        } else {
          Button("Close") {
            model.projection.onCloseWindow?(window.windowId)
          }
          .accessibilityIdentifier("catalog.window.\(window.windowId).close")
        }
      }
      .padding(12)
      .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
    }
  }

  @ViewBuilder
  private func chrome(_ window: AppleWindowProjection) -> some View {
    Text(model.sessionLabel)
      .accessibilityIdentifier("catalog.session")
    Text(String(model.revision))
      .accessibilityIdentifier("catalog.revision")
    Text(window.selection >= 0 ? String(window.selection) : "none")
      .accessibilityIdentifier("catalog.selection")
    VStack(alignment: .leading, spacing: 4) {
      ForEach(Array(window.rowTitles.enumerated()), id: \.offset) { index, title in
        let rowId = index < window.rowIds.count ? window.rowIds[index] : ""
        ProjectionButton(
          title: title,
          enabled: true,
          identifier: "catalog.row.\(index)",
          value: rowId,
          action: { model.projection.onSelectRow?(window.windowId, index) }
        )
      }
    }
    .accessibilityElement(children: .contain)
    .accessibilityIdentifier("catalog.list")
    HStack(spacing: 8) {
      ForEach(window.buttons, id: \.controlId) { button in
        ProjectionButton(
          title: button.title,
          enabled: button.enabled,
          identifier: catalogButtonIdentifier(button.controlId),
          action: { model.projection.onButton?(window.windowId, button.controlId) }
        )
        .disabled(!button.enabled)
      }
      ProjectionButton(
        title: "Save",
        enabled: true,
        identifier: "catalog.button.save",
        action: { model.projection.onSave?() }
      )
    }
    Text(model.saveStatus)
      .accessibilityIdentifier("catalog.save.status")
  }
}

#if os(macOS)
@_cdecl("ApptraverseMacInstallCatalogContent")
public func installMacCatalogContent(_ windowPointer: UnsafeMutableRawPointer,
                                     _ modelPointer: UnsafeMutableRawPointer,
                                     _ windowId: UInt64) {
  let window = Unmanaged<NSWindow>.fromOpaque(windowPointer).takeUnretainedValue()
  let model = Unmanaged<AppleCatalogModel>.fromOpaque(modelPointer).takeUnretainedValue()
  // Installed once per native window. Later publications update `model`.
  if window.contentView is NSHostingView<CatalogWindowPage> {
    return
  }
  window.contentView = NSHostingView(
    rootView: CatalogWindowPage(model: model, windowId: windowId))
}
#endif

#if os(iOS)
import UIKit

struct CatalogPhoneRoot: View {
  @ObservedObject var model: AppleCatalogModel

  private var catalog: AppleWindowProjection? {
    model.windows.first { $0.catalogChrome }
  }

  private var children: [AppleWindowProjection] {
    model.windows.filter { !$0.catalogChrome }
  }

  var body: some View {
    ScrollView {
      VStack(alignment: .leading, spacing: 12) {
        if let catalog {
          CatalogWindowPage(model: model, windowId: catalog.windowId)
        }
        Text(model.viewportLabel)
          .accessibilityIdentifier("catalog.viewport")
        ForEach(children, id: \.windowId) { child in
          CatalogWindowPage(model: model, windowId: child.windowId)
            .accessibilityIdentifier("catalog.page.\(child.windowId)")
        }
      }
    }
    .background(
      GeometryReader { proxy in
        Color.clear
          .onAppear { report(proxy.size) }
#if os(iOS)
          .onChange(of: proxy.size) { _, newSize in
            report(newSize)
          }
#else
          .onChange(of: proxy.size) { newSize in
            report(newSize)
          }
#endif
      }
    )
    .accessibilityIdentifier("catalog.root")
  }

  private func report(_ size: CGSize) {
    model.projection.onViewport?(Int32(size.width.rounded()),
                                 Int32(size.height.rounded()))
  }
}

@_cdecl("ApptraverseIosMakeRootController")
public func makeIosRootController(_ modelPointer: UnsafeMutableRawPointer)
  -> UnsafeMutableRawPointer {
  let model = Unmanaged<AppleCatalogModel>.fromOpaque(modelPointer).takeUnretainedValue()
  let controller = UIHostingController(rootView: CatalogPhoneRoot(model: model))
  return Unmanaged.passRetained(controller).toOpaque()
}
#endif

@_cdecl("ApptraverseReleaseCatalogModel")
public func releaseCatalogModel(_ pointer: UnsafeMutableRawPointer) {
  Unmanaged<AppleCatalogModel>.fromOpaque(pointer).release()
}

@_cdecl("ApptraverseMakeCatalogModel")
public func makeCatalogModel(_ projectionPointer: UnsafeMutableRawPointer)
  -> UnsafeMutableRawPointer {
  let projection = Unmanaged<AppleCatalogProjection>.fromOpaque(projectionPointer)
    .takeUnretainedValue()
  let model = AppleCatalogModel(projection: projection)
  return Unmanaged.passRetained(model).toOpaque()
}
