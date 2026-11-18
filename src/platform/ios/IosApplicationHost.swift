import AppTraverseIosBridge
import Foundation
import SwiftUI

protocol IosUiCommandSink: AnyObject {
    func clickButton(_ token: UInt64)
    func selectListRow(_ row: Int, token: UInt64)
    func activateListRow(_ row: Int, token: UInt64)
}

extension ATIosApplicationBridge: IosUiCommandSink {}

struct IosListRowSnapshot: Identifiable, Equatable {
    let id: UInt64
    let text: String
}

final class IosButtonAdapter: ObservableObject, Identifiable {
    let id: UInt64
    weak var commandSink: IosUiCommandSink?
    @Published private(set) var title = ""
    @Published private(set) var enabled = false
    private(set) var controlID: UInt32 = 0

    init(token: UInt64, commandSink: IosUiCommandSink) {
        id = token
        self.commandSink = commandSink
    }

    func sync(_ state: ATIosButtonState) {
        controlID = state.controlID
        title = state.title
        enabled = state.isEnabled
    }

    func click() {
        if enabled { commandSink?.clickButton(id) }
    }

    func disconnect() { commandSink = nil }
}

final class IosListAdapter: ObservableObject, Identifiable {
    let id: UInt64
    weak var commandSink: IosUiCommandSink?
    @Published var rows: [IosListRowSnapshot] = []
    @Published private(set) var selectedRow: Int?
    private(set) var controlID: UInt32 = 0

    init(token: UInt64, commandSink: IosUiCommandSink) {
        id = token
        self.commandSink = commandSink
    }

    func sync(_ state: ATIosListState) {
        controlID = state.controlID
        rows = state.rows.map { IosListRowSnapshot(id: $0.identity, text: $0.text) }
        selectedRow = state.selectedRow >= 0 ? state.selectedRow : nil
    }

    var selectedIdentity: UInt64? {
        guard let row = selectedRow, rows.indices.contains(row) else { return nil }
        return rows[row].id
    }

    func select(_ identity: UInt64) {
        if let row = rows.firstIndex(where: { $0.id == identity }) {
            commandSink?.selectListRow(row, token: id)
        }
    }

    func activate(_ identity: UInt64) {
        if let row = rows.firstIndex(where: { $0.id == identity }) {
            commandSink?.activateListRow(row, token: id)
        }
    }

    func disconnect() { commandSink = nil }
}

struct IosWindowSnapshot {
    let state: ATIosWindowState
    let buttons: [IosButtonAdapter]
    let lists: [IosListAdapter]
}

final class IosWindowSceneModel: ObservableObject, Identifiable {
    let id: UInt64
    let identity: UInt64
    var state: ATIosWindowState
    var buttons: [IosButtonAdapter] = []
    var lists: [IosListAdapter] = []
    @Published private(set) var snapshot: IosWindowSnapshot?

    init(token: UInt64, state: ATIosWindowState) {
        id = token
        identity = state.identity
        self.state = state
    }

    var title: String { state.title }

    func publish() {
        snapshot = IosWindowSnapshot(state: state, buttons: buttons, lists: lists)
    }
}

private struct IosButtonControl: View {
    @ObservedObject var adapter: IosButtonAdapter

    var body: some View {
        Button(action: adapter.click) {
            Text(adapter.title)
                .multilineTextAlignment(.center)
                .frame(maxWidth: .infinity, minHeight: 44, maxHeight: .infinity)
        }
        .buttonStyle(.bordered)
        .disabled(!adapter.enabled)
        .accessibilityIdentifier("button-\(adapter.id)")
        .layoutValue(key: AppleControlID.self, value: adapter.controlID)
    }
}

private struct IosListControl: View {
    @ObservedObject var adapter: IosListAdapter

    var body: some View {
        List(adapter.rows) { row in
            let selected = adapter.selectedIdentity == row.id
            Text(row.text)
                .frame(maxWidth: .infinity, minHeight: 44, alignment: .leading)
                .contentShape(Rectangle())
                .gesture(TapGesture(count: 2).exclusively(before: TapGesture()).onEnded { tap in
                    switch tap {
                    case .first: adapter.activate(row.id)
                    case .second: adapter.select(row.id)
                    }
                })
                .accessibilityAddTraits(selected ? [.isButton, .isSelected] : .isButton)
                .accessibilityAction { adapter.select(row.id) }
                .accessibilityIdentifier("row-\(row.id)")
                .listRowInsets(EdgeInsets(top: 0, leading: 12, bottom: 0, trailing: 12))
                .listRowBackground(selected ? Color.accentColor.opacity(0.17) : Color.clear)
        }
        .listStyle(.plain)
        .scrollContentBackground(.hidden)
        .overlay(Rectangle().strokeBorder(.secondary, lineWidth: 1).allowsHitTesting(false))
        .accessibilityIdentifier("list-\(adapter.id)")
        .layoutValue(key: AppleControlID.self, value: adapter.controlID)
    }
}

private struct IosWindowView: View {
    @ObservedObject var scene: IosWindowSceneModel
    let viewport: CGSize
    @Environment(\.colorSchemeContrast) private var contrast

    var body: some View {
        if let snapshot = scene.snapshot, viewport.width >= 1, viewport.height >= 1 {
            ScrollView([.horizontal, .vertical]) {
                AppleConstraintLayout(definition: snapshot.state.layout, viewport: viewport) {
                    ForEach(snapshot.buttons) { IosButtonControl(adapter: $0) }
                    ForEach(snapshot.lists) { IosListControl(adapter: $0) }
                }
            }
            .scrollBounceBehavior(.basedOnSize)
            .background(snapshot.state.softenDark && contrast != .increased ?
                        Color(white: 0.18) : Color.clear)
        }
    }
}

private struct IosPagePicker: View {
    @ObservedObject var host: IosApplicationHost

    var body: some View {
        GeometryReader { geometry in
            ScrollViewReader { scroll in
                ScrollView(.horizontal) {
                    HStack(spacing: 0) {
                        ForEach(Array(host.pageTokens.enumerated()), id: \.element) { index, token in
                            let selected = token == host.activeToken
                            Button {
                                host.selectPage(token)
                            } label: {
                                Circle()
                                    .fill(selected ? Color.accentColor : Color.secondary.opacity(0.4))
                                    .frame(width: 8, height: 8)
                                    .frame(width: 44, height: 44)
                                    .contentShape(Rectangle())
                            }
                            .buttonStyle(.plain)
                            .accessibilityLabel(Text(host.scenes[token]!.title))
                            .accessibilityAddTraits(selected ? .isSelected : [])
                            .accessibilityIdentifier("mobile-page-\(index)")
                            .id(token)
                        }
                    }
                    .frame(minWidth: geometry.size.width)
                }
                .scrollIndicators(.hidden)
                .scrollBounceBehavior(.basedOnSize)
                .onChange(of: host.activeToken, initial: true) {
                    if let token = host.activeToken { scroll.scrollTo(token, anchor: .center) }
                }
            }
        }
        .frame(height: 44)
        .accessibilityIdentifier("mobile-pages")
    }
}

struct IosSystemSettings: Equatable {
    let displayScale: Double
    let bodyTextScale: Double
    let dark: Bool
    let highContrast: Bool
    let reducedMotion: Bool
    let voiceOver: Bool

    func bridgeSettings(systemDark: Bool) -> ATIosDeviceSettings {
        ATIosDeviceSettings(displayScale: displayScale, bodyTextScale: bodyTextScale,
            dark: systemDark, highContrast: highContrast, reducedMotion: reducedMotion, voiceOver: voiceOver)
    }
}

public struct IosApplicationRootView: View {
    @ObservedObject public var host: IosApplicationHost
    @Environment(\.scenePhase) private var phase
    @Environment(\.displayScale) private var displayScale
    @Environment(\.colorScheme) private var scheme
    @Environment(\.colorSchemeContrast) private var contrast
    @Environment(\.accessibilityReduceMotion) private var reducedMotion
    @Environment(\.accessibilityVoiceOverEnabled) private var voiceOver
    @ScaledMetric(relativeTo: .body) private var bodyTextScale = 1.0

    public init(host: IosApplicationHost) { self.host = host }

    private var preferredScheme: ColorScheme? {
        guard contrast != .increased else { return nil }
        switch host.activeScene?.snapshot?.state.theme {
        case 1: return .light
        case 2: return .dark
        default: return nil
        }
    }

    private var settings: IosSystemSettings {
        IosSystemSettings(displayScale: displayScale, bodyTextScale: bodyTextScale,
            dark: scheme == .dark, highContrast: contrast == .increased,
            reducedMotion: reducedMotion, voiceOver: voiceOver)
    }

    public var body: some View {
        NavigationStack {
            VStack(spacing: 0) {
                GeometryReader { geometry in
                    Group {
                        if let scene = host.activeScene {
                            IosWindowView(scene: scene, viewport: geometry.size).id(scene.id)
                        } else {
                            ProgressView()
                        }
                    }
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
                    .contentShape(Rectangle())
                    .onChange(of: geometry.size, initial: true) { host.reportViewport(geometry.size) }
                    .simultaneousGesture(DragGesture(minimumDistance: 40).onEnded { value in
                        if abs(value.translation.width) > 60 &&
                            abs(value.translation.width) > abs(value.translation.height) * 2 {
                            host.swipe(value.translation.width < 0 ? 1 : -1)
                        }
                    })
                }
                if host.pageTokens.count > 1 { IosPagePicker(host: host) }
            }
            .navigationTitle(host.activeScene?.title ?? "")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItemGroup(placement: .topBarTrailing) {
                    ForEach(Array(host.menuGroups.enumerated()), id: \.offset) { groupIndex, group in
                        Menu(group.title.replacingOccurrences(of: "&", with: "")) {
                            ForEach(Array(group.items.enumerated()), id: \.offset) { itemIndex, item in
                                Toggle(item.title, isOn: Binding(get: { item.isSelected }, set: { _ in
                                    host.executeMenu(group: groupIndex, item: itemIndex)
                                }))
                            }
                        }
                        .accessibilityIdentifier("catalog-menu-\(groupIndex)")
                    }
                }
            }
        }
        .preferredColorScheme(preferredScheme)
        .onChange(of: settings, initial: true) {
            host.observeSettings(settings, followsSystemAppearance: preferredScheme == nil)
        }
        .onChange(of: phase, initial: true) {
            if phase == .active {
                host.observeSettings(settings, followsSystemAppearance: preferredScheme == nil)
                host.sceneDidBecomeActive()
            } else {
                host.sceneWillDeactivate()
            }
        }
    }
}

private struct IosPresentation {
    var pages: [UInt64] = []
    var active: UInt64?
    var menu: [ATIosMenuGroupState] = []
}

public final class IosApplicationHost: NSObject, ObservableObject, ATIosApplicationBridgeDelegate {
    private let bridgeFactory: (ATIosApplicationBridgeDelegate) -> ATIosApplicationBridge
    private(set) var bridge: ATIosApplicationBridge!
    private(set) var scenes: [UInt64: IosWindowSceneModel] = [:]
    private(set) var buttons: [UInt64: IosButtonAdapter] = [:]
    private(set) var lists: [UInt64: IosListAdapter] = [:]
    @Published private var presentation = IosPresentation()
    private var pendingPresentation = IosPresentation()
    private var started = false
    private var stopping = false
    private(set) var applicationActive = false
    private var manualStop = false
    private(set) var latestViewport = CGSize.zero
    private var latestSettings: ATIosDeviceSettings?
    private var systemDark = false
    private var lastExitStatus: Int?
    private var lastBackgroundSaveStatus: Int?
    private var pendingBackgroundSaves = 0
    private var backgroundSaveFailure: Int?
    private let suspension = IosSuspensionAssertion()

    var pageTokens: [UInt64] { presentation.pages }
    var activeToken: UInt64? { presentation.active }
    var menuGroups: [ATIosMenuGroupState] { presentation.menu }
    var activeScene: IosWindowSceneModel? { activeToken.flatMap { scenes[$0] } }
    var mainScene: IosWindowSceneModel? { pageTokens.first.flatMap { scenes[$0] } }
    public var isRunning: Bool { started && !stopping }

    public init(bridgeFactory: @escaping
        (ATIosApplicationBridgeDelegate) -> ATIosApplicationBridge) {
        self.bridgeFactory = bridgeFactory
    }

    deinit { bridge?.requestStop() }

    func observeSettings(_ settings: IosSystemSettings, followsSystemAppearance: Bool) {
        // A preferred scheme may change the environment of the presentation.
        // Record the system value only while SwiftUI follows the system.
        if followsSystemAppearance { systemDark = settings.dark }
        let value = settings.bridgeSettings(systemDark: systemDark)
        latestSettings = value
        if started && !stopping { bridge.observeDeviceSettings(value) }
    }

    public func startIfNeeded() {
        guard !started && !manualStop && applicationActive else { return }
        started = true
        stopping = false
        bridge = bridgeFactory(self)
        if let latestSettings { bridge.observeDeviceSettings(latestSettings) }
        bridge.start()
        bridge.submitViewportWidth(latestViewport.width, height: latestViewport.height)
    }

    public func lastModelExitCode() -> Int? { lastExitStatus }

    func executeMenu(group: Int, item: Int) {
        guard started && !stopping else { return }
        bridge.executeMenuGroup(UInt(group), item: UInt(item))
    }

    public func reportViewport(_ size: CGSize) {
        latestViewport = size
        if started && !stopping { bridge.submitViewportWidth(size.width, height: size.height) }
    }

    public func swipe(_ delta: Int) {
        if started && !stopping { bridge.submitMobileSwipe(delta) }
    }

    public func selectPage(_ token: UInt64) {
        if started && !stopping && pageTokens.contains(token) { bridge.submitMobilePage(token) }
    }

    public func closeDetailFromUi() {
        guard started && !stopping, let scene = activeScene, activeToken != pageTokens.first else { return }
        _ = bridge.requestCloseWindow(scene.id)
    }

    public func requestStop() {
        manualStop = true
        guard started && !stopping else { return }
        stopping = true
        bridge.requestStop()
    }

    public func sceneWillDeactivate() {
        guard applicationActive else { return }
        applicationActive = false
        requestBackgroundCheckpoint()
    }

    public func sceneDidEnterBackground() { sceneWillDeactivate() }

    public func sceneDidBecomeActive() {
        applicationActive = true
        startIfNeeded()
    }

    public func requestBackgroundCheckpoint() {
        guard started && !stopping else { return }
        lastBackgroundSaveStatus = nil
        if pendingBackgroundSaves == 0 { backgroundSaveFailure = nil }
        pendingBackgroundSaves += 1
        suspension.begin()
        bridge.requestBackgroundSave()
    }

    public func lastBackgroundSaveExitCode() -> Int? { lastBackgroundSaveStatus }

    public func bridge(_ bridge: ATIosApplicationBridge, loadWindow token: UInt64) {
        scenes[token] = IosWindowSceneModel(token: token, state: bridge.windowState(token))
    }

    public func bridge(_ bridge: ATIosApplicationBridge, syncWindow token: UInt64) {
        scenes[token]!.state = bridge.windowState(token)
    }

    public func bridge(_ bridge: ATIosApplicationBridge, unloadWindow token: UInt64) {
        scenes.removeValue(forKey: token)
    }

    public func bridge(_ bridge: ATIosApplicationBridge,
                       applyMobilePages tokens: [NSNumber], activeToken token: UInt64) {
        pendingPresentation.pages = tokens.map(\.uint64Value)
        pendingPresentation.active = token
    }

    public func bridge(_ bridge: ATIosApplicationBridge, syncMenu groups: [ATIosMenuGroupState]) {
        pendingPresentation.menu = groups
    }

    public func bridge(_ bridge: ATIosApplicationBridge, loadButton token: UInt64) {
        let state = bridge.buttonState(token)
        let adapter = IosButtonAdapter(token: token, commandSink: bridge)
        adapter.sync(state)
        buttons[token] = adapter
        scenes[state.parentToken]!.buttons.append(adapter)
    }

    public func bridge(_ bridge: ATIosApplicationBridge, syncButton token: UInt64) {
        buttons[token]!.sync(bridge.buttonState(token))
    }

    public func bridge(_ bridge: ATIosApplicationBridge, unloadButton token: UInt64) {
        let adapter = buttons.removeValue(forKey: token)!
        adapter.disconnect()
        for scene in scenes.values { scene.buttons.removeAll { $0 === adapter } }
    }

    public func bridge(_ bridge: ATIosApplicationBridge, loadList token: UInt64) {
        let state = bridge.listState(token)
        let adapter = IosListAdapter(token: token, commandSink: bridge)
        adapter.sync(state)
        lists[token] = adapter
        scenes[state.parentToken]!.lists.append(adapter)
    }

    public func bridge(_ bridge: ATIosApplicationBridge, syncList token: UInt64) {
        lists[token]!.sync(bridge.listState(token))
    }

    public func bridge(_ bridge: ATIosApplicationBridge, unloadList token: UInt64) {
        let adapter = lists.removeValue(forKey: token)!
        adapter.disconnect()
        for scene in scenes.values { scene.lists.removeAll { $0 === adapter } }
    }

    public func bridgeDidFinishPublication(_ bridge: ATIosApplicationBridge) {
        // Project each complete presenter graph after its structure and
        // controls have been reconciled on the main thread.
        for scene in scenes.values { scene.publish() }
        presentation = pendingPresentation
    }

    public func bridgeModelDidFinish(_ bridge: ATIosApplicationBridge) {
        lastExitStatus = Int(bridge.finishTeardown())
        self.bridge = nil
        started = false
        stopping = false
        pendingPresentation = IosPresentation()
        presentation = pendingPresentation
        suspension.end()
    }

    public func bridge(_ bridge: ATIosApplicationBridge, backgroundSaveDidFinish result: Int) {
        if result != 0 { backgroundSaveFailure = result }
        pendingBackgroundSaves -= 1
        if pendingBackgroundSaves == 0 {
            lastBackgroundSaveStatus = backgroundSaveFailure ?? result
            suspension.end()
        }
    }
}
