import AppKit
import SwiftUI

private struct ControlFrame {
    var x: Double
    var y: Double
    var width: Double
    var height: Double

    var cgRect: CGRect {
        CGRect(x: x, y: y, width: width, height: height)
    }
}

private final class ButtonAdapter: ObservableObject, Identifiable {
    let id: UInt64
    weak var bridge: ATMinimalNativeBridge?
    @Published var title = ""
    @Published var enabled = false
    @Published var frame = ControlFrame(x: 0, y: 0, width: 0, height: 0)

    init(token: UInt64, bridge: ATMinimalNativeBridge) {
        id = token
        self.bridge = bridge
    }

    func sync(_ state: ATMinimalButtonState) {
        title = state.title
        enabled = state.isEnabled
        frame = ControlFrame(x: state.x, y: state.y,
                             width: state.width, height: state.height)
    }

    func click() {
        bridge?.clickButton(id)
    }
}

private final class ListAdapter: ObservableObject, Identifiable {
    let id: UInt64
    weak var bridge: ATMinimalNativeBridge?
    @Published var rows: [String] = []
    @Published var selectedRow: Int?
    @Published var frame = ControlFrame(x: 0, y: 0, width: 0, height: 0)

    init(token: UInt64, bridge: ATMinimalNativeBridge) {
        id = token
        self.bridge = bridge
    }

    func sync(_ state: ATMinimalListState) {
        rows = state.rows
        selectedRow = state.selectedRow >= 0 ? state.selectedRow : nil
        frame = ControlFrame(x: state.x, y: state.y,
                             width: state.width, height: state.height)
    }

    var selection: Binding<Int?> {
        Binding(
            get: { self.selectedRow },
            set: { value in
                if let row = value {
                    self.bridge?.selectListRow(row, token: self.id)
                }
            })
    }
}

private final class WindowAdapter: ObservableObject, Identifiable {
    let id: UInt64
    weak var bridge: ATMinimalNativeBridge?
    let window: NSWindow
    @Published var title = ""
    @Published var buttons: [ButtonAdapter] = []
    @Published var lists: [ListAdapter] = []
    var applyingMirrorFrame = false

    init(token: UInt64, bridge: ATMinimalNativeBridge,
         delegate: NSWindowDelegate) {
        id = token
        self.bridge = bridge
        window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 640, height: 480),
            styleMask: [.titled, .closable, .miniaturizable, .resizable],
            backing: .buffered,
            defer: false)
        window.delegate = delegate
        window.isReleasedWhenClosed = false
        window.contentView = NSHostingView(rootView: WindowContent(adapter: self))
    }

    func sync(_ state: ATMinimalWindowState) {
        title = state.title
        window.title = state.title
        let frame = NSRect(x: state.x, y: state.y,
                           width: state.width, height: state.height)
        if window.contentLayoutRect.size != frame.size ||
            window.frame.origin != frame.origin {
            applyingMirrorFrame = true
            window.setContentSize(frame.size)
            window.setFrameOrigin(frame.origin)
            applyingMirrorFrame = false
        }
    }

    func reportFrame() {
        guard !applyingMirrorFrame else { return }
        let content = window.contentLayoutRect
        bridge?.submitWindowX(window.frame.origin.x,
                              y: window.frame.origin.y,
                              width: content.width,
                              height: content.height,
                              token: id)
    }
}

private struct ButtonControl: View {
    @ObservedObject var adapter: ButtonAdapter

    var body: some View {
        Button(adapter.title) { adapter.click() }
            .disabled(!adapter.enabled)
            .frame(width: adapter.frame.width, height: adapter.frame.height)
            .position(x: adapter.frame.x + adapter.frame.width / 2,
                      y: adapter.frame.y + adapter.frame.height / 2)
    }
}

private struct ListControl: View {
    @ObservedObject var adapter: ListAdapter

    var body: some View {
        List(selection: adapter.selection) {
            ForEach(Array(adapter.rows.enumerated()), id: \.offset) { row in
                Text(row.element).tag(Optional(row.offset))
            }
        }
        .frame(width: adapter.frame.width, height: adapter.frame.height)
        .position(x: adapter.frame.x + adapter.frame.width / 2,
                  y: adapter.frame.y + adapter.frame.height / 2)
    }
}

private struct WindowContent: View {
    @ObservedObject var adapter: WindowAdapter

    var body: some View {
        ZStack(alignment: .topLeading) {
            ForEach(adapter.buttons) { ButtonControl(adapter: $0) }
            ForEach(adapter.lists) { ListControl(adapter: $0) }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity,
               alignment: .topLeading)
    }
}

private final class MacApplicationHost: NSObject,
    NSApplicationDelegate, NSWindowDelegate, ATMinimalNativeBridgeDelegate {
    private var bridge: ATMinimalNativeBridge!
    private var windows: [UInt64: WindowAdapter] = [:]
    private var buttons: [UInt64: ButtonAdapter] = [:]
    private var lists: [UInt64: ListAdapter] = [:]
    private var terminating = false
    private var modelFinished = false

    func applicationDidFinishLaunching(_ notification: Notification) {
        let stateDirectory = Self.stateDirectory()
        bridge = ATMinimalNativeBridge(
            userDirectory: stateDirectory.path, delegate: self)
        bridge.start()
    }

    func applicationShouldTerminate(_ sender: NSApplication)
        -> NSApplication.TerminateReply {
        if modelFinished {
            return .terminateNow
        }
        if !terminating {
            terminating = true
            bridge.requestStop()
        }
        return .terminateCancel
    }

    func windowShouldClose(_ sender: NSWindow) -> Bool {
        let adapter = windows.values.first { $0.window === sender }!
        if bridge.requestCloseWindow(adapter.id) {
            terminating = true
        }
        return false
    }

    func windowDidMove(_ notification: Notification) {
        adapter(for: notification)?.reportFrame()
    }

    func windowDidResize(_ notification: Notification) {
        adapter(for: notification)?.reportFrame()
    }

    func bridge(_ bridge: ATMinimalNativeBridge, loadWindow token: UInt64) {
        let adapter = WindowAdapter(token: token, bridge: bridge,
                                    delegate: self)
        windows[token] = adapter
        adapter.sync(bridge.windowState(token))
        adapter.window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
    }

    func bridge(_ bridge: ATMinimalNativeBridge, syncWindow token: UInt64) {
        windows[token]!.sync(bridge.windowState(token))
    }

    func bridge(_ bridge: ATMinimalNativeBridge, unloadWindow token: UInt64) {
        let adapter = windows.removeValue(forKey: token)!
        adapter.window.delegate = nil
        adapter.window.orderOut(nil)
        adapter.window.close()
    }

    func bridge(_ bridge: ATMinimalNativeBridge, loadButton token: UInt64) {
        let state = bridge.buttonState(token)
        let adapter = ButtonAdapter(token: token, bridge: bridge)
        adapter.sync(state)
        buttons[token] = adapter
        let window = windows[state.parentToken]!
        window.buttons.append(adapter)
    }

    func bridge(_ bridge: ATMinimalNativeBridge, syncButton token: UInt64) {
        buttons[token]!.sync(bridge.buttonState(token))
    }

    func bridge(_ bridge: ATMinimalNativeBridge, unloadButton token: UInt64) {
        let adapter = buttons.removeValue(forKey: token)!
        for window in windows.values {
            window.buttons.removeAll { $0 === adapter }
        }
    }

    func bridge(_ bridge: ATMinimalNativeBridge, loadList token: UInt64) {
        let state = bridge.listState(token)
        let adapter = ListAdapter(token: token, bridge: bridge)
        adapter.sync(state)
        lists[token] = adapter
        let window = windows[state.parentToken]!
        window.lists.append(adapter)
    }

    func bridge(_ bridge: ATMinimalNativeBridge, syncList token: UInt64) {
        lists[token]!.sync(bridge.listState(token))
    }

    func bridge(_ bridge: ATMinimalNativeBridge, unloadList token: UInt64) {
        let adapter = lists.removeValue(forKey: token)!
        for window in windows.values {
            window.lists.removeAll { $0 === adapter }
        }
    }

    func bridgeModelDidFinish(_ bridge: ATMinimalNativeBridge) {
        bridge.finishTeardown()
        modelFinished = true
        if terminating {
            NSApp.terminate(nil)
        }
    }

    private func adapter(for notification: Notification) -> WindowAdapter? {
        guard let window = notification.object as? NSWindow else { return nil }
        return windows.values.first { $0.window === window }
    }

    private static func stateDirectory() -> URL {
        if let index = CommandLine.arguments.firstIndex(of: "--state-dir"),
           CommandLine.arguments.indices.contains(index + 1) {
            return URL(fileURLWithPath: CommandLine.arguments[index + 1])
        }
        return FileManager.default.urls(
            for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("AppTraverse/MinimalApp", isDirectory: true)
    }
}

@main
private enum MinimalAppMain {
    static func main() {
        let application = NSApplication.shared
        let delegate = MacApplicationHost()
        application.delegate = delegate
        application.setActivationPolicy(.regular)
        application.run()
        withExtendedLifetime(delegate) {}
    }
}
