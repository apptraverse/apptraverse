import AppTraverseIosBridge
import SwiftUI

protocol IosUiCommandSink: AnyObject {
    func clickButton(_ token: UInt64)
    func selectListRow(_ row: Int, token: UInt64)
}

extension ATIosApplicationBridge: IosUiCommandSink {}

struct IosListRowSnapshot: Identifiable, Equatable {
    let id: UInt64
    let text: String
}

final class IosButtonAdapter: ObservableObject, Identifiable {
    let id: UInt64
    weak var commandSink: IosUiCommandSink?
    @Published var title = ""
    @Published var enabled = false

    init(token: UInt64, commandSink: IosUiCommandSink) {
        id = token
        self.commandSink = commandSink
    }

    func sync(_ state: ATIosButtonState) {
        title = state.title
        enabled = state.isEnabled
    }

    func click() {
        commandSink?.clickButton(id)
    }

    func disconnect() {
        commandSink = nil
    }
}

final class IosListAdapter: ObservableObject, Identifiable {
    let id: UInt64
    weak var commandSink: IosUiCommandSink?
    @Published var rows: [IosListRowSnapshot] = []
    @Published var selectedRow: Int?

    init(token: UInt64, commandSink: IosUiCommandSink) {
        id = token
        self.commandSink = commandSink
    }

    func sync(_ state: ATIosListState) {
        rows = state.rows.map {
            IosListRowSnapshot(id: $0.identity, text: $0.text)
        }
        selectedRow = state.selectedRow >= 0 ? state.selectedRow : nil
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

final class IosWindowSceneModel: ObservableObject, Identifiable {
    let id: UInt64
    weak var commandSink: IosUiCommandSink?
    @Published var title = ""
    @Published var buttons: [IosButtonAdapter] = []
    @Published var lists: [IosListAdapter] = []

    init(token: UInt64, commandSink: IosUiCommandSink) {
        id = token
        self.commandSink = commandSink
    }

    func sync(_ state: ATIosWindowState) {
        title = state.title
    }

    func disconnect() {
        commandSink = nil
        for button in buttons {
            button.disconnect()
        }
        for list in lists {
            list.disconnect()
        }
        buttons = []
        lists = []
    }
}

private struct IosWindowView: View {
    @ObservedObject var scene: IosWindowSceneModel

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            ForEach(scene.buttons) { button in
                Button(button.title) { button.click() }
                    .disabled(!button.enabled)
            }
            ForEach(scene.lists) { list in
                List(selection: list.selection) {
                    ForEach(Array(list.rows.enumerated()),
                            id: \.element.id) { row in
                        Text(row.element.text).tag(Optional(row.offset))
                    }
                }
                .frame(minHeight: 120)
            }
            Spacer(minLength: 0)
        }
        .padding()
        .navigationTitle(scene.title)
    }
}

public struct IosApplicationRootView: View {
    @ObservedObject public var host: IosApplicationHost
    @Environment(\.scenePhase) private var scenePhase

    public init(host: IosApplicationHost) {
        self.host = host
    }

    public var body: some View {
        NavigationStack {
            Group {
                if let main = host.mainScene {
                    IosWindowView(scene: main)
                } else {
                    ProgressView()
                }
            }
        }
        .sheet(isPresented: $host.showDetail, onDismiss: {
            host.detailDismissedFromUi()
        }) {
            if let detail = host.detailScene {
                NavigationStack {
                    IosWindowView(scene: detail)
                        .toolbar {
                            ToolbarItem(placement: .cancellationAction) {
                                Button("Close") { host.closeDetailFromUi() }
                            }
                        }
                }
            }
        }
        .onAppear { host.startIfNeeded() }
        .onChange(of: scenePhase) { _, phase in
            if phase == .background {
                host.requestBackgroundCheckpoint()
            }
        }
    }
}

public final class IosApplicationHost: NSObject, ObservableObject,
    ATIosApplicationBridgeDelegate {
    private let bridgeFactory:
        (ATIosApplicationBridgeDelegate) -> ATIosApplicationBridge
    private var bridge: ATIosApplicationBridge!
    @Published var mainScene: IosWindowSceneModel?
    @Published var detailScene: IosWindowSceneModel?
    @Published var showDetail = false
    private var buttons: [UInt64: IosButtonAdapter] = [:]
    private var lists: [UInt64: IosListAdapter] = [:]
    private var started = false
    private var lastBackgroundSaveResult: Int?

    public init(bridgeFactory: @escaping
        (ATIosApplicationBridgeDelegate) -> ATIosApplicationBridge) {
        self.bridgeFactory = bridgeFactory
    }

    public func startIfNeeded() {
        guard !started else { return }
        started = true
        bridge = bridgeFactory(self)
        bridge.start()
    }

    public func requestBackgroundCheckpoint() {
        bridge?.requestBackgroundSave()
    }

    public func lastBackgroundSaveExitCode() -> Int? {
        lastBackgroundSaveResult
    }

    public func closeDetailFromUi() {
        guard let detail = detailScene else { return }
        _ = bridge.requestCloseWindow(detail.id)
    }

    func detailDismissedFromUi() {
        guard let detail = detailScene else { return }
        _ = bridge.requestCloseWindow(detail.id)
    }

    public func bridge(_ bridge: ATIosApplicationBridge,
                       loadWindow token: UInt64) {
        let state = bridge.windowState(token)
        let scene = IosWindowSceneModel(token: token, commandSink: bridge)
        scene.sync(state)
        if mainScene == nil {
            mainScene = scene
        } else {
            detailScene = scene
            showDetail = true
        }
    }

    public func bridge(_ bridge: ATIosApplicationBridge,
                       syncWindow token: UInt64) {
        scene(for: token)?.sync(bridge.windowState(token))
    }

    public func bridge(_ bridge: ATIosApplicationBridge,
                       unloadWindow token: UInt64) {
        if detailScene?.id == token {
            let scene = detailScene!
            detailScene = nil
            showDetail = false
            scene.disconnect()
            return
        }
        if mainScene?.id == token {
            let scene = mainScene!
            mainScene = nil
            scene.disconnect()
        }
    }

    public func bridge(_ bridge: ATIosApplicationBridge,
                       loadButton token: UInt64) {
        let state = bridge.buttonState(token)
        let adapter = IosButtonAdapter(token: token, commandSink: bridge)
        adapter.sync(state)
        buttons[token] = adapter
        scene(for: state.parentToken)?.buttons.append(adapter)
    }

    public func bridge(_ bridge: ATIosApplicationBridge,
                       syncButton token: UInt64) {
        buttons[token]!.sync(bridge.buttonState(token))
    }

    public func bridge(_ bridge: ATIosApplicationBridge,
                       unloadButton token: UInt64) {
        let adapter = buttons.removeValue(forKey: token)!
        adapter.disconnect()
        for scene in [mainScene, detailScene].compactMap({ $0 }) {
            scene.buttons.removeAll { $0 === adapter }
        }
    }

    public func bridge(_ bridge: ATIosApplicationBridge,
                       loadList token: UInt64) {
        let state = bridge.listState(token)
        let adapter = IosListAdapter(token: token, commandSink: bridge)
        adapter.sync(state)
        lists[token] = adapter
        scene(for: state.parentToken)?.lists.append(adapter)
    }

    public func bridge(_ bridge: ATIosApplicationBridge,
                       syncList token: UInt64) {
        lists[token]!.sync(bridge.listState(token))
    }

    public func bridge(_ bridge: ATIosApplicationBridge,
                       unloadList token: UInt64) {
        let adapter = lists.removeValue(forKey: token)!
        adapter.disconnect()
        for scene in [mainScene, detailScene].compactMap({ $0 }) {
            scene.lists.removeAll { $0 === adapter }
        }
    }

    public func bridgeModelDidFinish(_ bridge: ATIosApplicationBridge) {
        bridge.finishTeardown()
    }

    public func bridge(_ bridge: ATIosApplicationBridge,
                       backgroundSaveDidFinish result: NSInteger) {
        lastBackgroundSaveResult = Int(result)
    }

    private func scene(for token: UInt64) -> IosWindowSceneModel? {
        if mainScene?.id == token { return mainScene }
        if detailScene?.id == token { return detailScene }
        return nil
    }
}
