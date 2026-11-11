import AppKit
import AppTraverseMacBridge
import SwiftUI

struct MacControlFrame: Equatable {
    var x: Double
    var y: Double
    var width: Double
    var height: Double

    var cgRect: CGRect { CGRect(x: x, y: y, width: width, height: height) }
}

protocol MacUiCommandSink: AnyObject {
    func clickButton(_ token: UInt64)
    func selectListRow(_ row: Int, token: UInt64)
    func activateListRow(_ row: Int, token: UInt64)
    func submitWindowFrame(_ frame: CGRect, token: UInt64,
                           updatesRestore: Bool)
}

extension ATMacApplicationBridge: MacUiCommandSink {
    func submitWindowFrame(_ frame: CGRect, token: UInt64,
                           updatesRestore: Bool) {
        submitWindowX(frame.origin.x, y: frame.origin.y,
                      width: frame.width, height: frame.height, token: token,
                      updatesRestore: updatesRestore)
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
        guard enabled else { return }
        commandSink?.clickButton(id)
    }

    func disconnect() { commandSink = nil }
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
        rows = state.rows.map { MacListRowSnapshot(id: $0.identity, text: $0.text) }
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
        Binding(get: { self.selectedRow }, set: { value in
            if let row = value {
                self.commandSink?.selectListRow(row, token: self.id)
            }
        })
    }

    func activate(_ row: Int) { commandSink?.activateListRow(row, token: id) }
    func disconnect() { commandSink = nil }
}

// Coordinates in the model use the primary screen's upper-left origin. AppKit
// uses a bottom-left origin, while window sizes in the model are client sizes.
enum MacDesktopCoordinates {
    static var primaryTop: CGFloat { NSScreen.screens[0].frame.maxY }

    static func modelFrame(of window: NSWindow) -> CGRect {
        let content = window.contentRect(forFrameRect: window.frame)
        return CGRect(x: window.frame.minX, y: primaryTop - window.frame.maxY,
                      width: content.width, height: content.height)
    }

    static func nativeFrame(_ model: CGRect, window: NSWindow) -> CGRect {
        let outer = window.frameRect(forContentRect:
            CGRect(origin: .zero, size: model.size))
        return CGRect(x: model.minX, y: primaryTop - model.minY - outer.height,
                      width: outer.width, height: outer.height)
    }

    static func modelWorkArea(_ screen: NSScreen) -> CGRect {
        let area = screen.visibleFrame
        return CGRect(x: area.minX, y: primaryTop - area.maxY,
                      width: area.width, height: area.height)
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

    init(token: UInt64, commandSink: MacUiCommandSink,
         delegate: NSWindowDelegate) {
        id = token
        self.commandSink = commandSink
        window = NSWindow(contentRect: .zero,
            styleMask: [.titled, .closable, .miniaturizable, .resizable],
            backing: .buffered, defer: false)
        window.delegate = delegate
        window.isReleasedWhenClosed = false
        window.contentView = NSHostingView(rootView: MacWindowContent(adapter: self))
    }

    func sync(_ state: ATMacWindowState) {
        title = state.title
        window.title = state.title
    }

    func applyFrame(_ state: ATMacWindowState) {
        guard !window.inLiveResize else { return }
        let model = CGRect(x: state.x, y: state.y,
                           width: state.width, height: state.height)
        let requested = MacDesktopCoordinates.nativeFrame(model, window: window)
        if window.frame != requested {
            applyingMirrorFrame = true
            window.setFrame(requested, display: true)
            applyingMirrorFrame = false
            // AppKit may constrain a requested frame. Report the actual native
            // result as an Event; reflected reports do not request another move.
            reportFrame()
        }
    }

    func initialFrame(_ state: ATMacWindowState) {
        applyingMirrorFrame = true
        window.setContentSize(NSSize(width: state.width, height: state.height))
        if state.hasExplicitPosition {
            let model = CGRect(x: state.x, y: state.y,
                               width: state.width, height: state.height)
            window.setFrame(MacDesktopCoordinates.nativeFrame(model, window: window),
                            display: false)
        } else {
            // AppKit chooses the cascade; the resulting frame is reported to
            // the model, which owns subsequent placement decisions.
            window.cascadeTopLeft(from: .zero)
        }
        applyingMirrorFrame = false
    }

    func reportFrame(updatesRestore: Bool = true) {
        guard !applyingMirrorFrame else { return }
        commandSink?.submitWindowFrame(MacDesktopCoordinates.modelFrame(of: window),
            token: id, updatesRestore: updatesRestore && !window.isZoomed &&
                !window.styleMask.contains(.fullScreen) && !window.isMiniaturized)
    }

    func disconnect() {
        commandSink = nil
        window.delegate = nil
        window.contentView = nil
        window.orderOut(nil)
        window.close()
    }
}

private struct MacCatalogButtonStyle: ButtonStyle {
    @Environment(\.isEnabled) private var isEnabled

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .foregroundColor(isEnabled ? .primary : .gray)
            .background(configuration.isPressed
                ? Color(nsColor: .selectedControlColor)
                : Color(white: 0.94))
            .overlay(Rectangle().strokeBorder(Color(white: 0.55), lineWidth: 1))
    }
}

private struct MacButtonControl: View {
    @ObservedObject var adapter: MacButtonAdapter

    var body: some View {
        Button(action: { adapter.click() }) {
            Text(adapter.title)
                .font(.system(size: 12))
                .frame(width: adapter.frame.width, height: adapter.frame.height)
                .contentShape(Rectangle())
        }
        .buttonStyle(MacCatalogButtonStyle())
        .disabled(!adapter.enabled)
        .accessibilityIdentifier("button-\(adapter.id)")
        .position(x: adapter.frame.x + adapter.frame.width / 2,
                  y: adapter.frame.y + adapter.frame.height / 2)
    }
}

// The native list owns mouse tracking and double-click recognition. Attaching
// a SwiftUI row gesture to List competes with AppKit's selection recognizer.
private struct MacNativeList: NSViewRepresentable {
    @ObservedObject var adapter: MacListAdapter

    func makeCoordinator() -> Coordinator { Coordinator(adapter: adapter) }

    func makeNSView(context: Context) -> NSScrollView {
        let table = NSTableView(frame: NSRect(x: 0, y: 0,
            width: adapter.frame.width, height: adapter.frame.height))
        table.autoresizingMask = [.width]
        table.headerView = nil
        table.style = .plain
        table.rowHeight = 20
        table.intercellSpacing = .zero
        table.allowsMultipleSelection = false
        table.allowsEmptySelection = true
        table.backgroundColor = .white
        table.columnAutoresizingStyle = .uniformColumnAutoresizingStyle
        table.setAccessibilityIdentifier("list-\(adapter.id)")
        let column = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("primary"))
        column.resizingMask = .autoresizingMask
        column.width = adapter.frame.width
        table.addTableColumn(column)
        table.delegate = context.coordinator
        table.dataSource = context.coordinator
        table.target = context.coordinator
        table.doubleAction = #selector(Coordinator.activate(_:))
        let scroll = NSScrollView()
        scroll.hasVerticalScroller = true
        scroll.autohidesScrollers = true
        scroll.drawsBackground = true
        scroll.backgroundColor = .white
        scroll.documentView = table
        context.coordinator.sync(table)
        return scroll
    }

    func updateNSView(_ scroll: NSScrollView, context: Context) {
        context.coordinator.adapter = adapter
        context.coordinator.sync(scroll.documentView as! NSTableView)
    }

    static func dismantleNSView(_ scroll: NSScrollView, coordinator: Coordinator) {
        let table = scroll.documentView as! NSTableView
        table.target = nil
        table.delegate = nil
        table.dataSource = nil
        coordinator.adapter = nil
    }

    final class Coordinator: NSObject, NSTableViewDataSource, NSTableViewDelegate {
        weak var adapter: MacListAdapter?
        private var displayedRows: [MacListRowSnapshot] = []
        private var appliedSelection: Int?
        private var applyingPublication = false

        init(adapter: MacListAdapter) { self.adapter = adapter }

        func sync(_ table: NSTableView) {
            guard let adapter else { return }
            let rowsChanged = displayedRows != adapter.rows
            let selectionChanged = appliedSelection != adapter.selectedRow
            // Reapplying an unchanged mirror selection during a layout update
            // would undo a native click while its model command is in flight.
            guard rowsChanged || selectionChanged else { return }
            applyingPublication = true
            if rowsChanged {
                displayedRows = adapter.rows
                table.reloadData()
            }
            appliedSelection = adapter.selectedRow
            if let selected = adapter.selectedRow {
                table.selectRowIndexes(IndexSet(integer: selected),
                                       byExtendingSelection: false)
            } else {
                table.deselectAll(nil)
            }
            applyingPublication = false
        }

        func numberOfRows(in tableView: NSTableView) -> Int { displayedRows.count }

        func tableView(_ tableView: NSTableView,
                       objectValueFor tableColumn: NSTableColumn?, row: Int) -> Any? {
            displayedRows[row].text
        }

        func tableView(_ tableView: NSTableView,
                       viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
            let identifier = NSUserInterfaceItemIdentifier("primary-cell")
            let cell: NSTableCellView
            if let existing = tableView.makeView(withIdentifier: identifier,
                                                owner: self) as? NSTableCellView {
                cell = existing
            } else {
                cell = NSTableCellView()
                cell.identifier = identifier
                let text = NSTextField(labelWithString: "")
                text.font = .systemFont(ofSize: 12)
                text.lineBreakMode = .byTruncatingTail
                text.translatesAutoresizingMaskIntoConstraints = false
                cell.textField = text
                cell.addSubview(text)
                NSLayoutConstraint.activate([
                    text.leadingAnchor.constraint(equalTo: cell.leadingAnchor, constant: 4),
                    text.trailingAnchor.constraint(equalTo: cell.trailingAnchor, constant: -4),
                    text.centerYAnchor.constraint(equalTo: cell.centerYAnchor)
                ])
            }
            cell.textField!.stringValue = displayedRows[row].text
            return cell
        }

        func tableView(_ tableView: NSTableView,
                       selectionIndexesForProposedSelection proposed: IndexSet) -> IndexSet {
            // Like a desktop listbox, clicking whitespace does not clear its
            // selection. Publications may still explicitly render no selection.
            if !applyingPublication && proposed.isEmpty {
                return tableView.selectedRowIndexes
            }
            return proposed
        }

        func tableViewSelectionDidChange(_ notification: Notification) {
            guard !applyingPublication,
                  let table = notification.object as? NSTableView,
                  let adapter,
                  displayedRows.indices.contains(table.selectedRow) else { return }
            let identity = displayedRows[table.selectedRow].id
            // Displayed rows may lag a structural publication. Resolve the
            // clicked identity in the current mirror before submitting input.
            if let row = adapter.rows.firstIndex(where: { $0.id == identity }) {
                adapter.commandSink?.selectListRow(row, token: adapter.id)
            }
        }

        @objc func activate(_ table: NSTableView) {
            guard let adapter,
                  displayedRows.indices.contains(table.clickedRow) else { return }
            let identity = displayedRows[table.clickedRow].id
            if let row = adapter.rows.firstIndex(where: { $0.id == identity }) {
                adapter.activate(row)
            }
        }
    }
}

private struct MacListControl: View {
    @ObservedObject var adapter: MacListAdapter

    var body: some View {
        MacNativeList(adapter: adapter)
            .overlay(Rectangle().strokeBorder(Color(white: 0.55), lineWidth: 1))
            .frame(width: adapter.frame.width, height: adapter.frame.height)
            .position(x: adapter.frame.x + adapter.frame.width / 2,
                      y: adapter.frame.y + adapter.frame.height / 2)
    }
}

private struct MacWindowContent: View {
    @ObservedObject var adapter: MacWindowAdapter

    var body: some View {
        ZStack(alignment: .topLeading) {
            Color.white
            ForEach(adapter.buttons) { MacButtonControl(adapter: $0) }
            ForEach(adapter.lists) { MacListControl(adapter: $0) }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
        .preferredColorScheme(.light)
    }
}

public final class MacApplicationHost: NSObject,
    NSApplicationDelegate, NSWindowDelegate, ATMacApplicationBridgeDelegate {
    private let bridgeFactory: (ATMacApplicationBridgeDelegate) -> ATMacApplicationBridge
    var bridge: ATMacApplicationBridge!
    var windows: [UInt64: MacWindowAdapter] = [:]
    var buttons: [UInt64: MacButtonAdapter] = [:]
    var lists: [UInt64: MacListAdapter] = [:]
    private var terminating = false
    private var modelFinished = false
    private var exitStatus: Int32 = 1

    public init(bridgeFactory: @escaping
        (ATMacApplicationBridgeDelegate) -> ATMacApplicationBridge) {
        self.bridgeFactory = bridgeFactory
    }

    @discardableResult public func run() -> Int32 {
        let application = NSApplication.shared
        application.delegate = self
        application.setActivationPolicy(.regular)
        installMenu()
        application.run()
        withExtendedLifetime(self) {}
        return exitStatus
    }

    private func installMenu() {
        let menu = NSMenu()
        let item = NSMenuItem()
        let applicationMenu = NSMenu()
        applicationMenu.addItem(withTitle: "Close Window",
            action: #selector(NSWindow.performClose(_:)), keyEquivalent: "w")
        applicationMenu.addItem(.separator())
        applicationMenu.addItem(withTitle: "Quit AppTraverse",
            action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")
        item.submenu = applicationMenu
        menu.addItem(item)
        NSApp.mainMenu = menu
    }

    public func applicationDidFinishLaunching(_ notification: Notification) {
        bridge = bridgeFactory(self)
        bridge.start()
    }

    public func applicationShouldTerminate(_ sender: NSApplication)
        -> NSApplication.TerminateReply {
        requestStop()
        return .terminateCancel
    }

    public func requestStop() {
        guard !modelFinished && !terminating else { return }
        // Queue final native reports before the runtime stop barrier.
        for adapter in windows.values { adapter.reportFrame() }
        terminating = true
        bridge.requestStop()
    }

    public func windowShouldClose(_ sender: NSWindow) -> Bool {
        guard !terminating else { return false }
        let adapter = windows.values.first { $0.window === sender }!
        // Closing the root may end the whole session: enqueue every final
        // native frame before the application callback requests its stop.
        for window in windows.values { window.reportFrame() }
        if bridge.requestCloseWindow(adapter.id) { terminating = true }
        return false
    }

    public func windowDidMove(_ notification: Notification) {
        guard !terminating else { return }
        adapter(for: notification)?.reportFrame()
    }
    public func windowDidResize(_ notification: Notification) {
        guard !terminating else { return }
        guard let adapter = adapter(for: notification) else { return }
        adapter.reportFrame(updatesRestore: !adapter.window.inLiveResize)
    }
    public func windowDidEndLiveResize(_ notification: Notification) {
        guard !terminating else { return }
        adapter(for: notification)?.reportFrame()
    }
    public func windowDidChangeScreen(_ notification: Notification) {
        guard !terminating else { return }
        guard let adapter = adapter(for: notification),
              let screen = adapter.window.screen else { return }
        reportScreen(screen, token: adapter.id)
    }

    private func reportScreen(_ screen: NSScreen, token: UInt64) {
        let area = MacDesktopCoordinates.modelWorkArea(screen)
        bridge.submitScreenX(area.minX, y: area.minY, width: area.width,
                              height: area.height, token: token)
    }

    public func bridge(_ bridge: ATMacApplicationBridge, loadWindow token: UInt64) {
        let adapter = MacWindowAdapter(token: token, commandSink: bridge, delegate: self)
        windows[token] = adapter
        let state = bridge.windowState(token)
        adapter.sync(state)
        adapter.initialFrame(state)
        adapter.window.makeKeyAndOrderFront(nil)
        adapter.reportFrame()
        reportScreen(adapter.window.screen!, token: token)
        NSApp.activate(ignoringOtherApps: true)
    }
    public func bridge(_ bridge: ATMacApplicationBridge, syncWindow token: UInt64) {
        windows[token]!.sync(bridge.windowState(token))
    }
    public func bridge(_ bridge: ATMacApplicationBridge, applyWindowFrame token: UInt64) {
        windows[token]!.applyFrame(bridge.windowState(token))
    }
    public func bridge(_ bridge: ATMacApplicationBridge,
                       orderWindowFront token: UInt64, activate: Bool) {
        let window = windows[token]!.window
        if window.isMiniaturized { window.deminiaturize(nil) }
        if activate {
            NSApp.activate(ignoringOtherApps: true)
            window.makeKeyAndOrderFront(nil)
        } else { window.orderFront(nil) }
    }
    public func orderedWindowTokens() -> [NSNumber] {
        NSApp.orderedWindows.compactMap { window in
            windows.values.first { $0.window === window }.map { NSNumber(value: $0.id) }
        }
    }
    public func bridge(_ bridge: ATMacApplicationBridge, unloadWindow token: UInt64) {
        windows.removeValue(forKey: token)!.disconnect()
    }
    public func bridge(_ bridge: ATMacApplicationBridge, loadButton token: UInt64) {
        let state = bridge.buttonState(token)
        let adapter = MacButtonAdapter(token: token, commandSink: bridge)
        adapter.sync(state)
        buttons[token] = adapter
        windows[state.parentToken]!.buttons.append(adapter)
    }
    public func bridge(_ bridge: ATMacApplicationBridge, syncButton token: UInt64) {
        buttons[token]!.sync(bridge.buttonState(token))
    }
    public func bridge(_ bridge: ATMacApplicationBridge, unloadButton token: UInt64) {
        let adapter = buttons.removeValue(forKey: token)!
        adapter.disconnect()
        for window in windows.values { window.buttons.removeAll { $0 === adapter } }
    }
    public func bridge(_ bridge: ATMacApplicationBridge, loadList token: UInt64) {
        let state = bridge.listState(token)
        let adapter = MacListAdapter(token: token, commandSink: bridge)
        adapter.sync(state)
        lists[token] = adapter
        windows[state.parentToken]!.lists.append(adapter)
    }
    public func bridge(_ bridge: ATMacApplicationBridge, syncList token: UInt64) {
        lists[token]!.sync(bridge.listState(token))
    }
    public func bridge(_ bridge: ATMacApplicationBridge, unloadList token: UInt64) {
        let adapter = lists.removeValue(forKey: token)!
        adapter.disconnect()
        for window in windows.values { window.lists.removeAll { $0 === adapter } }
    }
    public func bridgeModelDidFinish(_ bridge: ATMacApplicationBridge) {
        exitStatus = Int32(bridge.finishTeardown())
        modelFinished = true
        NSApp.stop(nil)
        // Wake the AppKit event pump so Run returns the model's exit status.
        let event = NSEvent.otherEvent(with: .applicationDefined,
            location: .zero, modifierFlags: [], timestamp: 0,
            windowNumber: 0, context: nil, subtype: 0, data1: 0, data2: 0)!
        NSApp.postEvent(event, atStart: true)
    }

    private func adapter(for notification: Notification) -> MacWindowAdapter? {
        guard let window = notification.object as? NSWindow else { return nil }
        return windows.values.first { $0.window === window }
    }
}
