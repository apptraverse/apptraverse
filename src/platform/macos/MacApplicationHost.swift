import Foundation
import AppTraverseMacBridge
import SwiftUI

protocol MacUiCommandSink: AnyObject {
    func clickButton(_ token: UInt64)
    func selectListRow(_ row: Int, token: UInt64)
    func activateListRow(_ row: Int, token: UInt64)
}

extension ATMacApplicationBridge: MacUiCommandSink {}

final class MacButtonAdapter: ObservableObject, Identifiable {
    let id: UInt64
    weak var commandSink: MacUiCommandSink?
    @Published var title = ""
    @Published var enabled = false
    @Published private(set) var controlID: UInt32 = 0

    init(token: UInt64, commandSink: MacUiCommandSink) {
        id = token
        self.commandSink = commandSink
    }

    func sync(_ state: ATMacButtonState) {
        title = state.title
        enabled = state.isEnabled
        controlID = state.controlID
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
    @Published private(set) var controlID: UInt32 = 0

    init(token: UInt64, commandSink: MacUiCommandSink) {
        id = token
        self.commandSink = commandSink
    }

    func sync(_ state: ATMacListState) {
        rows = state.rows.map { MacListRowSnapshot(id: $0.identity, text: $0.text) }
        selectedRow = state.selectedRow >= 0 ? state.selectedRow : nil
        controlID = state.controlID
    }

    func sync(rows: [MacListRowSnapshot], selectedRow: Int?) {
        self.rows = rows
        self.selectedRow = selectedRow
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

struct MacWindowSnapshot {
    let state: ATMacWindowState
    let buttons: [MacButtonAdapter]
    let lists: [MacListAdapter]
}

final class MacWindowAdapter: ObservableObject, Identifiable {
    let id: UInt64
    let sceneID: UUID
    var windowState: ATMacWindowState
    var pendingButtons: [MacButtonAdapter] = []
    var pendingLists: [MacListAdapter] = []
    @Published private(set) var snapshot: MacWindowSnapshot?
    private(set) var clientSize: CGSize?

    var title: String { windowState.title }
    var softenDark: Bool { windowState.softenDark }
    var layout: ATAppleWindowLayout { windowState.layout }
    var buttons: [MacButtonAdapter] { pendingButtons }
    var lists: [MacListAdapter] { pendingLists }

    init(token: UInt64, sceneID: UUID, windowState: ATMacWindowState) {
        id = token
        self.sceneID = sceneID
        self.windowState = windowState
    }

    func observeViewport(_ size: CGSize) { clientSize = size }

    func publish() {
        snapshot = MacWindowSnapshot(state: windowState, buttons: pendingButtons, lists: pendingLists)
    }
}

private struct MacButtonControl: View {
    @ObservedObject var adapter: MacButtonAdapter

    var body: some View {
        Button(action: adapter.click) {
            Text(adapter.title)
                .multilineTextAlignment(.center)
                .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
        .buttonStyle(.bordered)
        .disabled(!adapter.enabled)
        .accessibilityIdentifier("button-\(adapter.id)")
        .layoutValue(key: AppleControlID.self, value: adapter.controlID)
    }
}

private struct MacWindowContextMenu: View {
    let host: MacApplicationHost
    let isPrimary: Bool

    var body: some View {
        if isPrimary {
            ForEach(host.contextMenuItems.indices, id: \.self) { index in
                let item = host.contextMenuItems[index]
                if item.startsSection {
                    Divider()
                    Button(item.title) { host.executeContextMenu(item: index) }
                } else {
                    Toggle(item.title, isOn: Binding(get: { item.isSelected }, set: { _ in
                        host.executeContextMenu(item: index)
                    }))
                }
            }
        }
    }
}

private struct MacListControl: View {
    @ObservedObject var adapter: MacListAdapter
    let windowMenu: MacWindowContextMenu

    var body: some View {
        List(selection: adapter.selection) {
            ForEach(adapter.rows) { row in
                Text(row.text)
                    .lineLimit(1)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .contentShape(Rectangle())
                    .tag(row.id)
                    .accessibilityAction(named: Text("Open")) { adapter.activate(row.id) }
            }
        }
        .contextMenu(forSelectionType: UInt64.self, menu: { _ in windowMenu }) { selection in
            if let identity = selection.first { adapter.activate(identity) }
        }
        .listStyle(.plain)
        .overlay(Rectangle().strokeBorder(.secondary, lineWidth: 1).allowsHitTesting(false))
        .accessibilityIdentifier("list-\(adapter.id)")
        .layoutValue(key: AppleControlID.self, value: adapter.controlID)
    }
}

private let fullscreenKey = KeyEquivalent("\u{F70E}")

private struct MacWindowTokenKey: FocusedValueKey {
    typealias Value = UInt64
}

extension FocusedValues {
    fileprivate var macWindowToken: UInt64? {
        get { self[MacWindowTokenKey.self] }
        set { self[MacWindowTokenKey.self] = newValue }
    }
}

private struct MacWindowContent: View {
    let host: MacApplicationHost
    let adapter: MacWindowAdapter
    let snapshot: MacWindowSnapshot
    @Environment(\.colorSchemeContrast) private var contrast

    private var preferredScheme: ColorScheme? {
        guard contrast != .increased else { return nil }
        switch snapshot.state.theme {
        case 1: return .light
        case 2: return .dark
        default: return nil
        }
    }

    var body: some View {
        let windowMenu = MacWindowContextMenu(host: host, isPrimary: snapshot.state.isPrimary)
        GeometryReader { window in
            VStack(spacing: 0) {
                if let display = snapshot.state.display, window.size.width >= 1 {
                    AppleConstraintLayout(definition: display.toolbarLayout,
                                          viewport: CGSize(width: window.size.width, height: 0)) {
                        Button(display.actionTitle) {
                            host.setFullscreen(!display.isFullscreen, window: adapter.id)
                        }
                        .keyboardShortcut(display.isFullscreen ? .escape : fullscreenKey, modifiers: [])
                        .accessibilityIdentifier("window-fullscreen")
                        .layoutValue(key: AppleControlID.self, value: display.actionControlID)
                    }
                }
                GeometryReader { geometry in
                    ScrollView([.horizontal, .vertical]) {
                        if geometry.size.width >= 1, geometry.size.height >= 1 {
                            AppleConstraintLayout(definition: snapshot.state.layout, viewport: geometry.size) {
                                ForEach(snapshot.buttons) { MacButtonControl(adapter: $0) }
                                ForEach(snapshot.lists) { MacListControl(adapter: $0, windowMenu: windowMenu) }
                            }
                        }
                    }
                    .onAppear { adapter.observeViewport(geometry.size) }
                    .onChange(of: geometry.size) { adapter.observeViewport($0) }
                }
                .contentShape(Rectangle())
                .contextMenu { windowMenu }
            }
        }
        .focusedSceneValue(\.macWindowToken, adapter.id)
        .frame(minWidth: 1, minHeight: 1)
        .navigationTitle(snapshot.state.title)
        .background(snapshot.state.softenDark && contrast != .increased ?
                    Color(white: 0.18) : Color.clear)
        .preferredColorScheme(preferredScheme)
    }
}

private struct MacWindowPresentation: View {
    let host: MacApplicationHost
    @ObservedObject var adapter: MacWindowAdapter

    var body: some View {
        if let snapshot = adapter.snapshot {
            MacWindowContent(host: host, adapter: adapter, snapshot: snapshot)
        }
        else { ProgressView() }
    }
}

private struct MacWindowScene: View {
    @ObservedObject var host: MacApplicationHost
    let sceneID: UUID
    @Environment(\.openWindow) private var openWindow

    var body: some View {
        Group {
            if let adapter = host.window(for: sceneID) {
                MacWindowPresentation(host: host, adapter: adapter)
            } else {
                ProgressView()
            }
        }
        .background(MacNativeWindowAttachment(sceneID: sceneID, host: host)
            .frame(width: 0, height: 0))
        .onAppear {
            DispatchQueue.main.async {
                host.sceneAppeared(sceneID) {
                    openWindow(id: MacApplicationScenes.windowGroupID, value: $0)
                }
            }
        }
    }
}

private struct MacApplicationCommands: Commands {
    @ObservedObject var host: MacApplicationHost
    @FocusedValue(\.macWindowToken) private var windowToken

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
        CommandGroup(after: .windowArrangement) {
            if let token = windowToken, let display = host.windows[token]?.snapshot?.state.display,
               display.isFullscreen {
                Button(display.actionTitle) { host.setFullscreen(false, window: token) }
                    .keyboardShortcut(fullscreenKey, modifiers: [])
            }
        }
    }
}

/// The SwiftUI scene and commands shared by AppTraverse desktop applications.
public struct MacApplicationScenes: Scene {
    fileprivate static let windowGroupID = "apptraverse-window"
    @ObservedObject private var host: MacApplicationHost

    public init(host: MacApplicationHost) {
        self.host = host
        MacNativeWindows.disableTabbing()
    }

    public var body: some Scene {
        WindowGroup("AppTraverse", id: Self.windowGroupID, for: UUID.self) { $sceneID in
            MacWindowScene(host: host, sceneID: sceneID)
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
    // Stable primary scene. Child mount IDs are per-session capabilities so
    // SwiftUI restoration cannot reopen a stale presenter from another run.
    let primarySceneID = UUID(uuid: (0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0))
    private(set) var bridge: ATMacApplicationBridge?
    @Published private(set) var windows: [UInt64: MacWindowAdapter] = [:]
    @Published private(set) var menuGroups: [ATMacMenuGroupState] = []
    @Published private(set) var contextMenuItems: [ATMacMenuItemState] = []
    private(set) var buttons: [UInt64: MacButtonAdapter] = [:]
    private(set) var lists: [UInt64: MacListAdapter] = [:]
    let nativeWindows = MacNativeWindows()
    private var boundScenes = Set<UUID>()
    private var pendingFrames = Set<UInt64>()
    private var openingScenes = Set<UUID>()
    private var openScene: ((UUID) -> Void)?
    private var pendingPresentations: [(id: UUID, activate: Bool)] = []
    private var didSchedulePresentations = false
    private var terminating = false
    var onFinished: (Int32) -> Void = { exit($0) }

    public init(bridgeFactory: @escaping
        (ATMacApplicationBridgeDelegate) -> ATMacApplicationBridge) {
        self.bridgeFactory = bridgeFactory
    }

    deinit { bridge?.requestStop() }

    func window(for id: UUID) -> MacWindowAdapter? {
        windows.values.first { $0.sceneID == id }
    }

    func sceneAppeared(_ id: UUID, open: @escaping (UUID) -> Void) {
        guard !terminating else { return }
        openScene = open
        schedulePresentations()
        if id == primarySceneID {
            if bridge == nil {
                bridge = bridgeFactory(self)
                bridge!.start()
                nativeWindows.observeEnvironment { [weak self] in self?.bridge?.observeSystemEnvironment() }
            }
        } else if window(for: id) == nil {
            // A restored scene from another run cannot address this model.
            if nativeWindows[primarySceneID] == nil { open(primarySceneID) }
            nativeWindows.close(id)
        }
    }

    func nativeWindowAttached(_ id: UUID, _ native: MacNativeWindowHandle) {
        nativeWindows[id] = native
        openingScenes.remove(id)
        if id != primarySceneID && window(for: id) == nil {
            nativeWindows.close(id)
            return
        }
        bindNativeWindow(id)
        schedulePresentations()
    }

    private func bindNativeWindow(_ id: UUID) {
        guard let native = nativeWindows[id], let adapter = window(for: id),
              let snapshot = adapter.snapshot, let bridge else { return }
        if boundScenes.insert(id).inserted { bridge.bindNativeWindow(native, token: adapter.id) }
        if pendingFrames.remove(adapter.id) != nil { bridge.applyRequestedNativeFrame(adapter.id) }
        if let display = snapshot.state.display {
            bridge.applyWindowFullscreen(display.isFullscreen, token: adapter.id)
        }
    }

    func nativeWindowClosed(_ id: UUID, _ native: MacNativeWindowHandle) {
        guard nativeWindows[id] === native else { return }
        if !terminating, let adapter = window(for: id), let bridge {
            if bridge.requestCloseWindow(adapter.id) { terminating = true }
        } else if id == primarySceneID {
            requestStop()
        }
        nativeWindows[id] = nil
        boundScenes.remove(id)
        openingScenes.remove(id)
    }

    func nativeWindowFocused(_ id: UUID, _ native: MacNativeWindowHandle) {
        guard !terminating, pendingPresentations.isEmpty, nativeWindows[id] === native,
              let adapter = window(for: id) else { return }
        bridge?.activateWindow(adapter.id)
    }

    func nativeWindowChanged(_ id: UUID, _ native: MacNativeWindowHandle, commit: Bool) {
        guard !terminating, nativeWindows[id] === native,
              let adapter = window(for: id) else { return }
        bridge?.observeNativeWindow(adapter.id, commit: commit)
    }

    func nativeWindowWillMove(_ id: UUID, _ native: MacNativeWindowHandle) {
        guard !terminating, nativeWindows[id] === native,
              let adapter = window(for: id) else { return }
        bridge?.beginNativeWindowMove(adapter.id)
    }

    func nativeWindowInteractionFinished(_ id: UUID, _ native: MacNativeWindowHandle, commit: Bool) {
        guard !terminating, nativeWindows[id] === native,
              let adapter = window(for: id) else { return }
        bridge?.finishNativeWindowInteraction(adapter.id, commit: commit)
        bindNativeWindow(id)
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

    func executeContextMenu(item: Int) {
        guard !terminating, contextMenuItems.indices.contains(item) else { return }
        bridge?.executeContextMenuItem(UInt(item))
    }

    func setFullscreen(_ fullscreen: Bool, window token: UInt64) {
        guard !terminating, let window = windows[token], boundScenes.contains(window.sceneID) else { return }
        bridge?.submitWindowFullscreen(fullscreen, token: token)
    }

    private func present(_ id: UUID, activate: Bool) {
        pendingPresentations.append((id, activate))
        schedulePresentations()
    }

    private func schedulePresentations() {
        guard !didSchedulePresentations, !pendingPresentations.isEmpty else { return }
        didSchedulePresentations = true
        DispatchQueue.main.async { [weak self] in
            guard let self else { return }
            self.didSchedulePresentations = false
            guard !self.terminating else { return }
            self.pendingPresentations.removeAll { self.window(for: $0.id) == nil }
            guard let open = self.openScene else { return }
            for request in self.pendingPresentations where self.nativeWindows[request.id] == nil {
                if self.openingScenes.insert(request.id).inserted { open(request.id) }
            }
            // SwiftUI creates scenes asynchronously. Apply the model's desktop
            // order as a batch so a late window cannot steal cold-start focus.
            guard self.pendingPresentations.allSatisfy({ self.boundScenes.contains($0.id) }) else { return }
            let requests = self.pendingPresentations
            self.pendingPresentations.removeAll()
            for request in requests {
                if let native = self.nativeWindows[request.id] {
                    self.nativeWindows.raise(native, activate: request.activate)
                }
            }
            if let key = requests.last(where: { self.nativeWindows[$0.id]?.isKeyWindow == true }),
               let native = self.nativeWindows[key.id] {
                self.nativeWindowFocused(key.id, native)
            }
        }
    }

    public func bridge(_ bridge: ATMacApplicationBridge, loadWindow token: UInt64) {
        let state = bridge.windowState(token)
        let id = state.isPrimary ? primarySceneID : UUID()
        let adapter = MacWindowAdapter(token: token, sceneID: id, windowState: state)
        windows[token] = adapter
        if id != primarySceneID { present(id, activate: false) }
    }

    public func bridge(_ bridge: ATMacApplicationBridge, syncWindow token: UInt64) {
        windows[token]!.windowState = bridge.windowState(token)
    }

    public func bridge(_ bridge: ATMacApplicationBridge, applyWindowFrame token: UInt64) {
        pendingFrames.insert(token)
    }

    public func bridge(_ bridge: ATMacApplicationBridge,
                       orderWindowFront token: UInt64, activate: Bool) {
        present(windows[token]!.sceneID, activate: activate)
    }

    public func orderedWindowTokens() -> [NSNumber] {
        nativeWindows.orderedSceneIDs(windows.values.map(\.sceneID)).map {
            NSNumber(value: window(for: $0)!.id)
        }
    }

    public func bridge(_ bridge: ATMacApplicationBridge, unloadWindow token: UInt64) {
        let adapter = windows.removeValue(forKey: token)!
        pendingFrames.remove(token)
        boundScenes.remove(adapter.sceneID)
        openingScenes.remove(adapter.sceneID)
        schedulePresentations()
        if adapter.sceneID != primarySceneID { nativeWindows.close(adapter.sceneID) }
    }

    public func bridge(_ bridge: ATMacApplicationBridge, loadButton token: UInt64) {
        let state = bridge.buttonState(token)
        let adapter = MacButtonAdapter(token: token, commandSink: bridge)
        adapter.sync(state)
        buttons[token] = adapter
        windows[state.parentToken]!.pendingButtons.append(adapter)
    }

    public func bridge(_ bridge: ATMacApplicationBridge, syncButton token: UInt64) {
        buttons[token]!.sync(bridge.buttonState(token))
    }

    public func bridge(_ bridge: ATMacApplicationBridge, unloadButton token: UInt64) {
        let adapter = buttons.removeValue(forKey: token)!
        adapter.disconnect()
        for window in windows.values { window.pendingButtons.removeAll { $0 === adapter } }
    }

    public func bridge(_ bridge: ATMacApplicationBridge, loadList token: UInt64) {
        let state = bridge.listState(token)
        let adapter = MacListAdapter(token: token, commandSink: bridge)
        adapter.sync(state)
        lists[token] = adapter
        windows[state.parentToken]!.pendingLists.append(adapter)
    }

    public func bridge(_ bridge: ATMacApplicationBridge, syncList token: UInt64) {
        lists[token]!.sync(bridge.listState(token))
    }

    public func bridge(_ bridge: ATMacApplicationBridge, unloadList token: UInt64) {
        let adapter = lists.removeValue(forKey: token)!
        adapter.disconnect()
        for window in windows.values { window.pendingLists.removeAll { $0 === adapter } }
    }

    public func bridge(_ bridge: ATMacApplicationBridge, syncMenu groups: [ATMacMenuGroupState]) {
        menuGroups = groups
    }

    public func bridge(_ bridge: ATMacApplicationBridge, syncContextMenu items: [ATMacMenuItemState]) {
        contextMenuItems = items
    }

    public func bridgeDidFinishPublication(_ bridge: ATMacApplicationBridge) {
        // Loading the window precedes loading its controls. AppKit frame changes
        // can lay out SwiftUI synchronously, so bind only after the whole graph.
        for adapter in windows.values { adapter.publish() }
        for adapter in windows.values { bindNativeWindow(adapter.sceneID) }
        schedulePresentations()
    }

    public func bridgeModelDidFinish(_ bridge: ATMacApplicationBridge) {
        terminating = true
        let status = Int32(bridge.finishTeardown())
        self.bridge = nil
        openScene = nil
        boundScenes.removeAll()
        onFinished(status)
    }
}
