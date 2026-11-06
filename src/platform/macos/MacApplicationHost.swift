import AppKit
import AppTraverseMacBridge
import SwiftUI

struct MacControlFrame: Equatable {
    var x: Double
    var y: Double
    var width: Double
    var height: Double

    var cgRect: CGRect {
        CGRect(x: x, y: y, width: width, height: height)
    }
}

protocol MacUiCommandSink: AnyObject {
    func clickButton(_ token: UInt64)
    func selectListRow(_ row: Int, token: UInt64)
    func submitWindowFrame(_ frame: CGRect, token: UInt64)
}

extension ATMacApplicationBridge: MacUiCommandSink {
    func submitWindowFrame(_ frame: CGRect, token: UInt64) {
        submitWindowX(frame.origin.x, y: frame.origin.y,
                      width: frame.width, height: frame.height, token: token)
    }
}

final class MacButtonAdapter: ObservableObject, Identifiable {
    let id: UInt64
    weak var commandSink: MacUiCommandSink?
    @Published var title = ""
    @Published var enabled = false
    @Published var frame = MacControlFrame(x: 0, y: 0, width: 0, height: 0)

    init(token: UInt64, commandSink: MacUiCommandSink) {
        id = token
        self.commandSink = commandSink
    }

    func sync(_ state: ATMacButtonState) {
        title = state.title
        enabled = state.isEnabled
        frame = MacControlFrame(x: state.x, y: state.y,
                                width: state.width, height: state.height)
    }

    func click() {
        commandSink?.clickButton(id)
    }

    func disconnect() {
        commandSink = nil
    }
}

struct MacListRowSnapshot: Identifiable, Equatable {
    let id: UInt64
    let text: String
}

final class MacListAdapter: ObservableObject, Identifiable {
    let id: UInt64
    weak var commandSink: MacUiCommandSink?
    @Published var rows: [MacListRowSnapshot] = []
    @Published var selectedRow: Int?
    @Published var frame = MacControlFrame(x: 0, y: 0, width: 0, height: 0)

    init(token: UInt64, commandSink: MacUiCommandSink) {
        id = token
        self.commandSink = commandSink
    }

    func sync(_ state: ATMacListState) {
        rows = state.rows.map {
            MacListRowSnapshot(id: $0.identity, text: $0.text)
        }
        selectedRow = state.selectedRow >= 0 ? state.selectedRow : nil
        frame = MacControlFrame(x: state.x, y: state.y,
                                width: state.width, height: state.height)
    }

    func sync(rows: [MacListRowSnapshot], selectedRow: Int?,
              frame: MacControlFrame) {
        self.rows = rows
        self.selectedRow = selectedRow
        self.frame = frame
    }

    var selection: Binding<Int?> {
        Binding(
            get: { self.selectedRow },
            set: { value in
                if let row = value {
                    self.commandSink?.selectListRow(row, token: self.id)
                }
            })
    }

    func disconnect() {
        commandSink = nil
    }
}

final class MacWindowAdapter: ObservableObject, Identifiable {
    let id: UInt64
    weak var commandSink: MacUiCommandSink?
    let window: NSWindow
    @Published var title = ""
    @Published var buttons: [MacButtonAdapter] = []
    @Published var lists: [MacListAdapter] = []
    var applyingMirrorFrame = false
    var userAdjustingFrame = false
    var pendingNativeReport = false
    var lastReportedClient: NSRect?

    init(token: UInt64, commandSink: MacUiCommandSink,
         delegate: NSWindowDelegate) {
        id = token
        self.commandSink = commandSink
        window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 640, height: 480),
            styleMask: [.titled, .closable, .miniaturizable, .resizable],
            backing: .buffered,
            defer: false)
        window.delegate = delegate
        window.isReleasedWhenClosed = false
        window.contentView =
            NSHostingView(rootView: MacWindowContent(adapter: self))
    }

    func sync(_ state: ATMacWindowState) {
        title = state.title
        window.title = state.title
        let client = NSRect(x: state.x, y: state.y,
                            width: state.width, height: state.height)
        if userAdjustingFrame { return }
        if pendingNativeReport, let last = lastReportedClient {
            if !Self.rectsMatch(client, last) { return }
            pendingNativeReport = false
            lastReportedClient = nil
        }
        let targetFrame = window.frameRect(forContentRect: client)
        if !framesMatchClient(client) {
            applyingMirrorFrame = true
            window.setFrame(targetFrame, display: true)
            applyingMirrorFrame = false
        }
    }

    private static func rectsMatch(_ a: NSRect, _ b: NSRect) -> Bool {
        abs(a.origin.x - b.origin.x) < 0.5 &&
            abs(a.origin.y - b.origin.y) < 0.5 &&
            abs(a.width - b.width) < 0.5 &&
            abs(a.height - b.height) < 0.5
    }

    private func framesMatchClient(_ client: NSRect) -> Bool {
        let current = window.contentRect(forFrameRect: window.frame)
        return Self.rectsMatch(current, client)
    }

    func reportFrame() {
        guard !applyingMirrorFrame else { return }
        let client = window.contentRect(forFrameRect: window.frame)
        lastReportedClient = client
        pendingNativeReport = true
        commandSink?.submitWindowFrame(
            CGRect(x: client.origin.x, y: client.origin.y,
                   width: client.width, height: client.height), token: id)
    }

    func disconnect() {
        commandSink = nil
        window.delegate = nil
        window.contentView = nil
        window.orderOut(nil)
        window.close()
    }
}

private struct MacButtonControl: View {
    @ObservedObject var adapter: MacButtonAdapter

    var body: some View {
        Button(adapter.title) { adapter.click() }
            .disabled(!adapter.enabled)
            .frame(width: adapter.frame.width, height: adapter.frame.height)
            .position(x: adapter.frame.x + adapter.frame.width / 2,
                      y: adapter.frame.y + adapter.frame.height / 2)
    }
}

private struct MacListControl: View {
    @ObservedObject var adapter: MacListAdapter

    var body: some View {
        List(selection: adapter.selection) {
            ForEach(Array(adapter.rows.enumerated()),
                    id: \.element.id) { row in
                Text(row.element.text).tag(Optional(row.offset))
            }
        }
        .frame(width: adapter.frame.width, height: adapter.frame.height)
        .position(x: adapter.frame.x + adapter.frame.width / 2,
                  y: adapter.frame.y + adapter.frame.height / 2)
    }
}

private struct MacWindowContent: View {
    @ObservedObject var adapter: MacWindowAdapter

    var body: some View {
        ZStack(alignment: .topLeading) {
            ForEach(adapter.buttons) { MacButtonControl(adapter: $0) }
            ForEach(adapter.lists) { MacListControl(adapter: $0) }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity,
               alignment: .topLeading)
    }
}

public final class MacApplicationHost: NSObject,
    NSApplicationDelegate, NSWindowDelegate, ATMacApplicationBridgeDelegate {
    private let bridgeFactory:
        (ATMacApplicationBridgeDelegate) -> ATMacApplicationBridge
    private var bridge: ATMacApplicationBridge!
    private var windows: [UInt64: MacWindowAdapter] = [:]
    private var buttons: [UInt64: MacButtonAdapter] = [:]
    private var lists: [UInt64: MacListAdapter] = [:]
    private var terminating = false
    private var modelFinished = false

    public init(bridgeFactory: @escaping
        (ATMacApplicationBridgeDelegate) -> ATMacApplicationBridge) {
        self.bridgeFactory = bridgeFactory
    }

    public func run() {
        let application = NSApplication.shared
        application.delegate = self
        application.setActivationPolicy(.regular)
        application.run()
        withExtendedLifetime(self) {}
    }

    public func applicationDidFinishLaunching(_ notification: Notification) {
        bridge = bridgeFactory(self)
        bridge.start()
    }

    public func applicationShouldTerminate(_ sender: NSApplication)
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

    public func windowShouldClose(_ sender: NSWindow) -> Bool {
        let adapter = windows.values.first { $0.window === sender }!
        if bridge.requestCloseWindow(adapter.id) {
            terminating = true
        }
        return false
    }

    public func windowWillMove(_ notification: Notification) {
        adapter(for: notification)?.userAdjustingFrame = true
    }

    public func windowDidMove(_ notification: Notification) {
        guard let adapter = adapter(for: notification) else { return }
        adapter.reportFrame()
        if !adapter.window.inLiveResize {
            adapter.userAdjustingFrame = false
        }
    }

    public func windowWillStartLiveResize(_ notification: Notification) {
        adapter(for: notification)?.userAdjustingFrame = true
    }

    public func windowDidEndLiveResize(_ notification: Notification) {
        guard let adapter = adapter(for: notification) else { return }
        adapter.reportFrame()
        adapter.userAdjustingFrame = false
    }

    public func windowDidResize(_ notification: Notification) {
        guard let adapter = adapter(for: notification) else { return }
        adapter.reportFrame()
        if !adapter.window.inLiveResize {
            adapter.userAdjustingFrame = false
        }
    }

    public func capturePresenterTokensFrontToBack() -> [UInt64] {
        NSApp.windows.compactMap { window in
            windows.values.first { $0.window === window }?.id
        }
    }

    public func restoreDesktopZOrder(presenterTokensFrontToBack: [UInt64]) {
        for token in presenterTokensFrontToBack.reversed() {
            windows[token]?.window.orderBack(nil)
        }
        for token in presenterTokensFrontToBack {
            windows[token]?.window.orderFront(nil)
        }
    }

    public func focusPresenter(token: UInt64) {
        guard let adapter = windows[token] else { return }
        if adapter.window.isMiniaturized {
            adapter.window.deminiaturize(nil)
        }
        adapter.window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
    }

    public func bridge(_ bridge: ATMacApplicationBridge,
                       loadWindow token: UInt64) {
        let adapter = MacWindowAdapter(token: token, commandSink: bridge,
                                       delegate: self)
        windows[token] = adapter
        adapter.sync(bridge.windowState(token))
        adapter.window.orderFront(nil)
    }

    public func bridge(_ bridge: ATMacApplicationBridge,
                       syncWindow token: UInt64) {
        windows[token]!.sync(bridge.windowState(token))
    }

    public func bridge(_ bridge: ATMacApplicationBridge,
                       unloadWindow token: UInt64) {
        let adapter = windows.removeValue(forKey: token)!
        adapter.disconnect()
    }

    public func bridge(_ bridge: ATMacApplicationBridge,
                       loadButton token: UInt64) {
        let state = bridge.buttonState(token)
        let adapter = MacButtonAdapter(token: token, commandSink: bridge)
        adapter.sync(state)
        buttons[token] = adapter
        let window = windows[state.parentToken]!
        window.buttons.append(adapter)
    }

    public func bridge(_ bridge: ATMacApplicationBridge,
                       syncButton token: UInt64) {
        buttons[token]!.sync(bridge.buttonState(token))
    }

    public func bridge(_ bridge: ATMacApplicationBridge,
                       unloadButton token: UInt64) {
        let adapter = buttons.removeValue(forKey: token)!
        adapter.disconnect()
        for window in windows.values {
            window.buttons.removeAll { $0 === adapter }
        }
    }

    public func bridge(_ bridge: ATMacApplicationBridge,
                       loadList token: UInt64) {
        let state = bridge.listState(token)
        let adapter = MacListAdapter(token: token, commandSink: bridge)
        adapter.sync(state)
        lists[token] = adapter
        let window = windows[state.parentToken]!
        window.lists.append(adapter)
    }

    public func bridge(_ bridge: ATMacApplicationBridge,
                       syncList token: UInt64) {
        lists[token]!.sync(bridge.listState(token))
    }

    public func bridge(_ bridge: ATMacApplicationBridge,
                       unloadList token: UInt64) {
        let adapter = lists.removeValue(forKey: token)!
        adapter.disconnect()
        for window in windows.values {
            window.lists.removeAll { $0 === adapter }
        }
    }

    public func bridgeModelDidFinish(_ bridge: ATMacApplicationBridge) {
        bridge.finishTeardown()
        modelFinished = true
        if terminating {
            NSApp.terminate(nil)
        }
    }

    @objc public func bridgeCaptureDesktopPresenterTokensFrontToBack()
        -> [NSNumber] {
        capturePresenterTokensFrontToBack().map { NSNumber(value: $0) }
    }

    @objc public func bridgeRestoreDesktopZOrder(
        _ presenterTokensFrontToBack: [NSNumber]) {
        restoreDesktopZOrder(
            presenterTokensFrontToBack: presenterTokensFrontToBack.map {
                $0.uint64Value
            })
    }

    @objc public func bridgeFocusDesktopPresenter(_ token: UInt64) {
        focusPresenter(token: token)
    }

    private func adapter(for notification: Notification) -> MacWindowAdapter? {
        guard let window = notification.object as? NSWindow else { return nil }
        return windows.values.first { $0.window === window }
    }
}
