import Foundation
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
    func submitWindowSize(_ size: CGSize, token: UInt64)
}

extension ATMacApplicationBridge: MacUiCommandSink {
    func submitWindowSize(_ size: CGSize, token: UInt64) {
        submitWindowWidth(size.width, height: size.height, token: token)
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

    var selection: Binding<UInt64?> {
        Binding(get: {
            guard let row = self.selectedRow, self.rows.indices.contains(row) else { return nil }
            return self.rows[row].id
        }, set: { identity in
            if let row = self.rows.firstIndex(where: { $0.id == identity }) {
                self.commandSink?.selectListRow(row, token: self.id)
            }
        })
    }

    func activate(_ identity: UInt64) {
        if let row = rows.firstIndex(where: { $0.id == identity }) {
            commandSink?.activateListRow(row, token: id)
        }
    }
    func disconnect() { commandSink = nil }
}

final class MacWindowAdapter: ObservableObject, Identifiable {
    let id: UInt64
    let sceneID: UUID
    weak var commandSink: MacUiCommandSink?
    @Published var title = ""
    @Published var buttons: [MacButtonAdapter] = []
    @Published var lists: [MacListAdapter] = []
    @Published private(set) var requestedSize: CGSize?
    @Published private(set) var clientSize: CGSize?

    init(token: UInt64, sceneID: UUID, commandSink: MacUiCommandSink) {
        id = token
        self.sceneID = sceneID
        self.commandSink = commandSink
    }

    func sync(_ state: ATMacWindowState) {
        if title != state.title { title = state.title }
    }

    func requestSize(_ size: CGSize) {
        if size != clientSize { requestedSize = size }
    }

    func observeSize(_ size: CGSize) {
        guard size.width >= 1, size.height >= 1 else { return }
        let measured = CGSize(width: size.width.rounded(), height: size.height.rounded())
        if let requestedSize {
            // Ignore the old viewport while SwiftUI applies the scene's content
            // constraints. Release them as soon as layout reaches the new size.
            guard measured == requestedSize else { return }
            self.requestedSize = nil
        }
        guard clientSize != measured else { return }
        clientSize = measured
        commandSink?.submitWindowSize(measured, token: id)
    }

    func disconnect() { commandSink = nil }
}

private struct MacButtonControl: View {
    @ObservedObject var adapter: MacButtonAdapter

    var body: some View {
        Button(action: adapter.click) {
            Text(adapter.title)
                .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
        .buttonStyle(.bordered)
        .disabled(!adapter.enabled)
        .frame(width: adapter.frame.width, height: adapter.frame.height)
        .accessibilityIdentifier("button-\(adapter.id)")
        .position(x: adapter.frame.x + adapter.frame.width / 2,
                  y: adapter.frame.y + adapter.frame.height / 2)
    }
}

private struct MacListControl: View {
    @ObservedObject var adapter: MacListAdapter

    var body: some View {
        List(selection: adapter.selection) {
            ForEach(adapter.rows) { row in
                Text(row.text)
                    .lineLimit(1)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .contentShape(Rectangle())
                    .tag(row.id)
                    .simultaneousGesture(TapGesture(count: 2).onEnded {
                        adapter.activate(row.id)
                    })
                    .accessibilityAction(named: Text("Open")) { adapter.activate(row.id) }
            }
        }
        .listStyle(.plain)
        .overlay(Rectangle().strokeBorder(.secondary, lineWidth: 1).allowsHitTesting(false))
        .frame(width: adapter.frame.width, height: adapter.frame.height)
        .accessibilityIdentifier("list-\(adapter.id)")
        .position(x: adapter.frame.x + adapter.frame.width / 2,
                  y: adapter.frame.y + adapter.frame.height / 2)
    }
}

private struct MacWindowContent: View {
    @ObservedObject var adapter: MacWindowAdapter
    let followsSystemAppearance: Bool
    var fixedPanelSize = false

    private var sizeConstraint: CGSize? {
        adapter.requestedSize ?? (fixedPanelSize ? adapter.clientSize : nil)
    }

    var body: some View {
        GeometryReader { geometry in
            ZStack(alignment: .topLeading) {
                ForEach(adapter.buttons) { MacButtonControl(adapter: $0) }
                ForEach(adapter.lists) { MacListControl(adapter: $0) }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
            .onAppear { observe(geometry.size) }
            .onChange(of: geometry.size, perform: observe)
        }
        .frame(minWidth: sizeConstraint?.width ?? 1,
               maxWidth: sizeConstraint?.width ?? .infinity,
               minHeight: sizeConstraint?.height ?? 1,
               maxHeight: sizeConstraint?.height ?? .infinity)
        .navigationTitle(adapter.title)
        .preferredColorScheme(followsSystemAppearance ? nil : .light)
    }

    private func observe(_ size: CGSize) {
        DispatchQueue.main.async { adapter.observeSize(size) }
    }
}

@available(macOS 14.0, *)
private struct MacWindowScene: View {
    @ObservedObject var host: MacApplicationHost
    let sceneID: UUID
    @Environment(\.openWindow) private var openWindow
    @Environment(\.dismissWindow) private var dismissWindow
    @Environment(\.controlActiveState) private var controlActiveState

    var body: some View {
        Group {
            if let adapter = host.window(for: sceneID) {
                MacWindowContent(adapter: adapter,
                                 followsSystemAppearance: host.followsSystemAppearance)
            } else {
                ProgressView()
            }
        }
        .onAppear {
            DispatchQueue.main.async {
                host.sceneAppeared(sceneID, open: { openWindow(id: MacApplicationScenes.windowGroupID,
                                                               value: $0) },
                                   dismiss: {
                    dismissWindow(id: MacApplicationScenes.windowGroupID, value: sceneID)
                })
                if controlActiveState == .key { host.sceneFocused(sceneID) }
            }
        }
        .onDisappear { DispatchQueue.main.async { host.sceneDisappeared(sceneID) } }
        .onChange(of: controlActiveState) { state in
            if state == .key { DispatchQueue.main.async { host.sceneFocused(sceneID) } }
        }
    }
}

// Ventura has openWindow, but no matching SwiftUI window-dismissal action.
// Keep every child independently addressable in the primary window.
private struct MacTabbedWorkspace: View {
    @ObservedObject var host: MacApplicationHost
    @State private var selection: UUID

    init(host: MacApplicationHost) {
        self.host = host
        _selection = State(initialValue: host.primarySceneID)
    }

    var body: some View {
        Group {
            if let primary = host.window(for: host.primarySceneID) {
                if host.windows.count == 1 {
                    MacWindowContent(adapter: primary,
                                     followsSystemAppearance: host.followsSystemAppearance)
                } else {
                    TabView(selection: $selection) {
                        ForEach(host.orderedPanels) { adapter in
                            MacWorkspacePanel(host: host, adapter: adapter)
                                .tabItem { MacPanelTitle(adapter: adapter) }
                                .tag(adapter.sceneID)
                        }
                    }
                    .navigationTitle(primary.title)
                }
            } else {
                ProgressView()
            }
        }
        .onAppear {
            DispatchQueue.main.async {
                host.sceneAppeared(host.primarySceneID, open: { selection = $0 }, dismiss: {})
            }
        }
        .onDisappear {
            DispatchQueue.main.async { host.sceneDisappeared(host.primarySceneID) }
        }
        .onChange(of: selection) { id in
            DispatchQueue.main.async { host.sceneFocused(id) }
        }
        .onChange(of: host.windows.keys.sorted()) { _ in
            if host.window(for: selection) == nil { selection = host.primarySceneID }
        }
    }
}

private struct MacPanelTitle: View {
    @ObservedObject var adapter: MacWindowAdapter
    var body: some View { Text(adapter.title) }
}

private struct MacWorkspacePanel: View {
    @ObservedObject var host: MacApplicationHost
    @ObservedObject var adapter: MacWindowAdapter

    var body: some View {
        if adapter.sceneID == host.primarySceneID {
            MacWindowContent(adapter: adapter, followsSystemAppearance: host.followsSystemAppearance)
        } else {
            VStack(alignment: .trailing) {
                Button {
                    host.requestClose(adapter.sceneID)
                } label: {
                    Label("Close Panel", systemImage: "xmark")
                }
                .padding(.horizontal)
                ScrollView([.horizontal, .vertical]) {
                    MacWindowContent(adapter: adapter,
                                     followsSystemAppearance: host.followsSystemAppearance,
                                     fixedPanelSize: true)
                }
            }
        }
    }
}

private struct MacApplicationCommands: Commands {
    @ObservedObject var host: MacApplicationHost

    var body: some Commands {
        CommandGroup(replacing: .newItem) {}
        CommandGroup(replacing: .appTermination) {
            Button("Quit AppTraverse", action: host.requestStop)
                .keyboardShortcut("q", modifiers: .command)
        }
        CommandGroup(after: .appSettings) {
            ForEach(host.menuGroups.indices, id: \.self) { groupIndex in
                let group = host.menuGroups[groupIndex]
                Menu(group.title.replacingOccurrences(of: "&", with: "")) {
                    ForEach(group.items.indices, id: \.self) { itemIndex in
                        let item = group.items[itemIndex]
                        Toggle(item.title, isOn: Binding(get: { item.isSelected }, set: { _ in
                            host.executeMenu(group: groupIndex, item: itemIndex)
                        }))
                    }
                }
            }
        }
    }
}

/// The SwiftUI scene and commands shared by AppTraverse desktop applications.
public struct MacApplicationScenes: Scene {
    fileprivate static let windowGroupID = "apptraverse-window"
    @ObservedObject private var host: MacApplicationHost

    public init(host: MacApplicationHost) { self.host = host }

    public var body: some Scene {
        WindowGroup("AppTraverse", id: Self.windowGroupID, for: UUID.self) { $sceneID in
            if #available(macOS 14.0, *) {
                MacWindowScene(host: host, sceneID: sceneID)
            } else {
                MacTabbedWorkspace(host: host)
            }
        } defaultValue: {
            host.primarySceneID
        }
        .defaultSize(width: 680, height: 500)
        .windowResizability(.contentSize)
        .commands { MacApplicationCommands(host: host) }
    }
}

public final class MacApplicationHost: NSObject, ObservableObject, ATMacApplicationBridgeDelegate {
    private let bridgeFactory: (ATMacApplicationBridgeDelegate) -> ATMacApplicationBridge
    let followsSystemAppearance: Bool
    // Stable primary scene. Child mount IDs are per-session capabilities so
    // SwiftUI restoration cannot reopen a stale presenter from another run.
    let primarySceneID = UUID(uuid: (0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0))
    private(set) var bridge: ATMacApplicationBridge?
    @Published private(set) var windows: [UInt64: MacWindowAdapter] = [:]
    @Published private(set) var menuGroups: [ATMacMenuGroupState] = []
    private(set) var buttons: [UInt64: MacButtonAdapter] = [:]
    private(set) var lists: [UInt64: MacListAdapter] = [:]
    private(set) var sceneDismissals: [UUID: () -> Void] = [:]
    private var openScene: ((UUID) -> Void)?
    private var pendingPresentations: [UUID] = []
    private var didSchedulePresentations = false
    private var focusOrder: [UUID] = []
    private var panelOrder: [UInt64] = []
    private var terminating = false
    var onFinished: (Int32) -> Void = { exit($0) }

    public init(followsSystemAppearance: Bool = false, bridgeFactory: @escaping
        (ATMacApplicationBridgeDelegate) -> ATMacApplicationBridge) {
        self.followsSystemAppearance = followsSystemAppearance
        self.bridgeFactory = bridgeFactory
    }

    deinit { bridge?.requestStop() }

    var orderedPanels: [MacWindowAdapter] { panelOrder.compactMap { windows[$0] } }

    func window(for id: UUID) -> MacWindowAdapter? {
        windows.values.first { $0.sceneID == id }
    }

    func sceneAppeared(_ id: UUID, open: @escaping (UUID) -> Void,
                       dismiss: @escaping () -> Void) {
        guard !terminating else { return }
        openScene = open
        if id == primarySceneID {
            sceneDismissals[id] = dismiss
            if bridge == nil {
                bridge = bridgeFactory(self)
                bridge!.start()
            }
        } else if window(for: id) != nil {
            sceneDismissals[id] = dismiss
        } else {
            // Drop a child restored by SwiftUI without a matching presenter.
            if sceneDismissals[primarySceneID] == nil { open(primarySceneID) }
            dismiss()
        }
    }

    func sceneDisappeared(_ id: UUID) {
        sceneDismissals.removeValue(forKey: id)
        focusOrder.removeAll { $0 == id }
        guard !terminating else { return }
        requestClose(id)
    }

    func requestClose(_ id: UUID) {
        guard !terminating else { return }
        if let window = window(for: id), let bridge {
            if bridge.requestCloseWindow(window.id) { terminating = true }
        } else if id == primarySceneID {
            requestStop()
        }
    }

    func sceneFocused(_ id: UUID) {
        guard !terminating, let window = window(for: id) else { return }
        if focusOrder.first != id {
            focusOrder.removeAll { $0 == id }
            focusOrder.insert(id, at: 0)
            bridge?.activateWindow(window.id)
        }
    }

    public func requestStop() {
        guard !terminating, let bridge else { return }
        terminating = true
        bridge.requestStop()
    }

    func executeMenu(group: Int, item: Int) {
        guard !terminating, menuGroups.indices.contains(group),
              menuGroups[group].items.indices.contains(item) else { return }
        bridge?.executeMenuGroup(UInt(group), item: UInt(item))
    }

    private func present(_ id: UUID) {
        pendingPresentations.append(id)
        guard !didSchedulePresentations else { return }
        didSchedulePresentations = true
        DispatchQueue.main.async { [weak self] in
            guard let self else { return }
            self.didSchedulePresentations = false
            let requests = self.pendingPresentations
            self.pendingPresentations.removeAll()
            guard !self.terminating else { return }
            // Replay the model's requested order after the full publication is
            // mounted. The system owns the actual desktop stacking order.
            for id in requests where self.window(for: id) != nil {
                self.openScene?(id)
            }
        }
    }

    public func bridge(_ bridge: ATMacApplicationBridge, loadWindow token: UInt64) {
        let state = bridge.windowState(token)
        let id = state.isPrimary ? primarySceneID : UUID()
        let adapter = MacWindowAdapter(token: token, sceneID: id, commandSink: bridge)
        adapter.sync(state)
        adapter.requestSize(CGSize(width: state.width, height: state.height))
        windows[token] = adapter
        if id == primarySceneID { panelOrder.insert(token, at: 0) }
        else { panelOrder.append(token) }
        focusOrder.append(id)
        if id != primarySceneID { present(id) }
    }

    public func bridge(_ bridge: ATMacApplicationBridge, syncWindow token: UInt64) {
        windows[token]!.sync(bridge.windowState(token))
    }

    public func bridge(_ bridge: ATMacApplicationBridge, applyWindowFrame token: UInt64) {
        let state = bridge.windowState(token)
        windows[token]!.requestSize(CGSize(width: state.width, height: state.height))
    }

    public func bridge(_ bridge: ATMacApplicationBridge,
                       orderWindowFront token: UInt64, activate: Bool) {
        present(windows[token]!.sceneID)
    }

    public func orderedWindowTokens() -> [NSNumber] {
        focusOrder.compactMap { id in window(for: id).map { NSNumber(value: $0.id) } }
    }

    public func bridge(_ bridge: ATMacApplicationBridge, unloadWindow token: UInt64) {
        let adapter = windows.removeValue(forKey: token)!
        panelOrder.removeAll { $0 == token }
        adapter.disconnect()
        focusOrder.removeAll { $0 == adapter.sceneID }
        let dismiss = sceneDismissals.removeValue(forKey: adapter.sceneID)
        if adapter.sceneID != primarySceneID, let dismiss {
            DispatchQueue.main.async { dismiss() }
        }
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

    public func bridge(_ bridge: ATMacApplicationBridge, syncMenu groups: [ATMacMenuGroupState]) {
        menuGroups = groups
    }

    public func bridgeModelDidFinish(_ bridge: ATMacApplicationBridge) {
        terminating = true
        let status = Int32(bridge.finishTeardown())
        self.bridge = nil
        openScene = nil
        sceneDismissals.removeAll()
        onFinished(status)
    }
}
