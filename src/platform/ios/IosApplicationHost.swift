import AppTraverseIosBridge
import SwiftUI
import UIKit

struct IosControlFrame: Equatable {
    var x: Double
    var y: Double
    var width: Double
    var height: Double
}

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
    @Published var title = ""
    @Published var enabled = false
    @Published var frame = IosControlFrame(x: 0, y: 0, width: 0, height: 0)

    init(token: UInt64, commandSink: IosUiCommandSink) {
        id = token
        self.commandSink = commandSink
    }

    func sync(_ state: ATIosButtonState) {
        title = state.title
        enabled = state.isEnabled
        frame = IosControlFrame(x: state.x, y: state.y,
                                width: state.width, height: state.height)
    }

    func click() {
        guard enabled else { return }
        commandSink?.clickButton(id)
    }

    func disconnect() { commandSink = nil }
}

final class IosListAdapter: ObservableObject, Identifiable {
    let id: UInt64
    weak var commandSink: IosUiCommandSink?
    @Published var rows: [IosListRowSnapshot] = []
    @Published var selectedRow: Int?
    @Published var frame = IosControlFrame(x: 0, y: 0, width: 0, height: 0)

    init(token: UInt64, commandSink: IosUiCommandSink) {
        id = token
        self.commandSink = commandSink
    }

    func sync(_ state: ATIosListState) {
        rows = state.rows.map { IosListRowSnapshot(id: $0.identity, text: $0.text) }
        selectedRow = state.selectedRow >= 0 ? state.selectedRow : nil
        frame = IosControlFrame(x: state.x, y: state.y,
                                width: state.width, height: state.height)
    }

    func select(_ row: Int) { commandSink?.selectListRow(row, token: id) }
    func activate(_ row: Int) { commandSink?.activateListRow(row, token: id) }
    var selection: Binding<Int?> {
        Binding(get: { self.selectedRow }, set: { value in
            if let row = value { self.select(row) }
        })
    }
    func disconnect() { commandSink = nil }
}

final class IosWindowSceneModel: ObservableObject, Identifiable {
    let id: UInt64
    let identity: UInt64
    weak var commandSink: IosUiCommandSink?
    @Published var title = ""
    @Published var buttons: [IosButtonAdapter] = []
    @Published var lists: [IosListAdapter] = []

    init(token: UInt64, identity: UInt64, commandSink: IosUiCommandSink) {
        id = token
        self.identity = identity
        self.commandSink = commandSink
    }

    func sync(_ state: ATIosWindowState) { title = state.title }

    func disconnect() {
        commandSink = nil
        buttons = []
        lists = []
    }
}

// UIKit owns its native tracking, scroll and double-tap recognizers. The model
// publication owns confirmed selection; native input never edits the mirror.
struct IosNativeList: UIViewRepresentable {
    @ObservedObject var adapter: IosListAdapter

    final class Coordinator: NSObject, UITableViewDataSource, UITableViewDelegate,
                             UIGestureRecognizerDelegate {
        weak var adapter: IosListAdapter?
        var rows: [IosListRowSnapshot] = []
        var applyingSnapshot = false

        func tableView(_ tableView: UITableView, numberOfRowsInSection section: Int) -> Int {
            rows.count
        }
        func tableView(_ tableView: UITableView,
                       cellForRowAt indexPath: IndexPath) -> UITableViewCell {
            let cell = tableView.dequeueReusableCell(withIdentifier: "row", for: indexPath)
            var content = cell.defaultContentConfiguration()
            content.text = rows[indexPath.row].text
            content.textProperties.font = .systemFont(ofSize: 16)
            cell.contentConfiguration = content
            return cell
        }
        private func currentIndex(forDisplayedRow row: Int) -> Int? {
            guard rows.indices.contains(row), let adapter else { return nil }
            let identity = rows[row].id
            // UIKit may still display the preceding snapshot while SwiftUI has
            // already published a new adapter value. Match that displayed
            // identity into the current mirror instead of reusing its index.
            return adapter.rows.firstIndex { $0.id == identity }
        }
        func tableView(_ tableView: UITableView, didSelectRowAt indexPath: IndexPath) {
            guard !applyingSnapshot,
                  let index = currentIndex(forDisplayedRow: indexPath.row) else { return }
            adapter?.select(index)
        }
        func activateDisplayedRow(_ row: Int) {
            guard !applyingSnapshot,
                  let index = currentIndex(forDisplayedRow: row) else { return }
            adapter?.activate(index)
        }
        @objc func doubleTap(_ gesture: UITapGestureRecognizer) {
            guard gesture.state == .ended,
                  let table = gesture.view as? UITableView,
                  let path = table.indexPathForRow(at: gesture.location(in: table)) else { return }
            activateDisplayedRow(path.row)
        }
        func gestureRecognizer(_ gestureRecognizer: UIGestureRecognizer,
            shouldRecognizeSimultaneouslyWith otherGestureRecognizer: UIGestureRecognizer) -> Bool {
            true
        }
        func sync(_ adapter: IosListAdapter, table: UITableView) {
            self.adapter = adapter
            applyingSnapshot = true
            if rows != adapter.rows {
                rows = adapter.rows
                table.reloadData()
            }
            let path = adapter.selectedRow.map { IndexPath(row: $0, section: 0) }
            if table.indexPathForSelectedRow != path {
                table.selectRow(at: path, animated: false, scrollPosition: .none)
            }
            applyingSnapshot = false
        }
    }

    func makeCoordinator() -> Coordinator { Coordinator() }
    func makeUIView(context: Context) -> UITableView {
        let table = UITableView(frame: .zero, style: .plain)
        table.register(UITableViewCell.self, forCellReuseIdentifier: "row")
        table.dataSource = context.coordinator
        table.delegate = context.coordinator
        table.rowHeight = 44
        table.allowsMultipleSelection = false
        table.accessibilityIdentifier = "list-\(adapter.id)"
        table.layer.borderWidth = 1
        table.layer.borderColor = UIColor.separator.cgColor
        let tap = UITapGestureRecognizer(target: context.coordinator,
                                         action: #selector(Coordinator.doubleTap(_:)))
        tap.numberOfTapsRequired = 2
        tap.cancelsTouchesInView = false
        tap.delaysTouchesBegan = false
        tap.delaysTouchesEnded = false
        tap.delegate = context.coordinator
        table.addGestureRecognizer(tap)
        context.coordinator.sync(adapter, table: table)
        return table
    }
    func updateUIView(_ uiView: UITableView, context: Context) {
        context.coordinator.sync(adapter, table: uiView)
    }
    static func dismantleUIView(_ uiView: UITableView, coordinator: Coordinator) {
        uiView.delegate = nil
        uiView.dataSource = nil
        coordinator.adapter = nil
    }
}

private struct IosModelButtonStyle: ButtonStyle {
    @Environment(\.isEnabled) private var enabled
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .foregroundStyle(enabled ? Color.primary : Color.secondary)
            .background(configuration.isPressed ? Color(uiColor: .systemGray3)
                                                : Color(uiColor: .secondarySystemBackground))
            .overlay(RoundedRectangle(cornerRadius: 6).strokeBorder(
                Color(uiColor: .separator), lineWidth: 1))
    }
}

private struct IosButtonControl: View {
    @ObservedObject var adapter: IosButtonAdapter
    var body: some View {
        Button(action: { adapter.click() }) {
            Text(adapter.title)
                .frame(width: adapter.frame.width, height: adapter.frame.height)
                .contentShape(Rectangle())
        }
        .buttonStyle(IosModelButtonStyle())
        .disabled(!adapter.enabled)
        .accessibilityIdentifier("button-\(adapter.id)")
        .position(x: adapter.frame.x + adapter.frame.width / 2,
                  y: adapter.frame.y + adapter.frame.height / 2)
    }
}

private struct IosListControl: View {
    @ObservedObject var adapter: IosListAdapter
    var body: some View {
        IosNativeList(adapter: adapter)
            .frame(width: adapter.frame.width, height: adapter.frame.height)
            .position(x: adapter.frame.x + adapter.frame.width / 2,
                      y: adapter.frame.y + adapter.frame.height / 2)
    }
}

private struct IosWindowView: View {
    @ObservedObject var scene: IosWindowSceneModel
    var body: some View {
        ZStack(alignment: .topLeading) {
            Color(uiColor: .systemBackground)
            ForEach(scene.buttons) { IosButtonControl(adapter: $0) }
            ForEach(scene.lists) { IosListControl(adapter: $0) }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
        .navigationTitle(scene.title)
        .navigationBarTitleDisplayMode(.inline)
    }
}

private struct IosPagerIndicator: UIViewRepresentable {
    @ObservedObject var host: IosApplicationHost
    final class Coordinator: NSObject {
        weak var host: IosApplicationHost?
        @objc func changed(_ control: UIPageControl) {
            guard let host, host.pageTokens.indices.contains(control.currentPage) else { return }
            host.selectPage(host.pageTokens[control.currentPage])
        }
    }
    func makeCoordinator() -> Coordinator { Coordinator() }
    func makeUIView(context: Context) -> UIPageControl {
        let control = UIPageControl()
        control.pageIndicatorTintColor = .systemGray3
        control.currentPageIndicatorTintColor = .systemBlue
        control.accessibilityIdentifier = "mobile-pages"
        control.addTarget(context.coordinator, action: #selector(Coordinator.changed(_:)),
                          for: .valueChanged)
        return control
    }
    func updateUIView(_ control: UIPageControl, context: Context) {
        context.coordinator.host = host
        control.numberOfPages = host.pageTokens.count
        if let active = host.activeToken {
            control.currentPage = host.pageTokens.firstIndex(of: active)!
        }
    }
    static func dismantleUIView(_ control: UIPageControl, coordinator: Coordinator) {
        coordinator.host = nil
        control.removeTarget(nil, action: nil, for: .allEvents)
    }
}

public struct IosApplicationRootView: View {
    @ObservedObject public var host: IosApplicationHost

    public init(host: IosApplicationHost) { self.host = host }

    public var body: some View {
        NavigationStack {
          VStack(spacing: 0) {
            GeometryReader { geometry in
                Group {
                    if let scene = host.activeScene {
                        IosWindowView(scene: scene)
                    } else {
                        ProgressView()
                    }
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .onAppear { host.reportViewport(geometry.size) }
                .onChange(of: geometry.size) { _, size in host.reportViewport(size) }
                .simultaneousGesture(DragGesture(minimumDistance: 40).onEnded { value in
                    if abs(value.translation.width) > 60 &&
                        abs(value.translation.width) > abs(value.translation.height) * 2 {
                        host.swipe(value.translation.width < 0 ? 1 : -1)
                    }
                })
            }
            if host.pageTokens.count > 1 {
                IosPagerIndicator(host: host).frame(height: 24)
            }
          }
        }
        .onAppear { host.startIfNeeded() }
    }
}

public final class IosApplicationHost: NSObject, ObservableObject,
    ATIosApplicationBridgeDelegate {
    private let bridgeFactory: (ATIosApplicationBridgeDelegate) -> ATIosApplicationBridge
    var bridge: ATIosApplicationBridge!
    @Published var scenes: [UInt64: IosWindowSceneModel] = [:]
    @Published var pageTokens: [UInt64] = []
    @Published var activeToken: UInt64?
    var buttons: [UInt64: IosButtonAdapter] = [:]
    var lists: [UInt64: IosListAdapter] = [:]
    private var started = false
    private var stopping = false
    private var applicationActive = true
    private var manualStop = false
    private var latestViewport = CGSize.zero
    private var lastExitStatus: Int?
    private var lastBackgroundSaveStatus: Int?
    private var pendingBackgroundSaves = 0
    private var backgroundSaveFailure: Int?
    private var backgroundTask = UIBackgroundTaskIdentifier.invalid

    var activeScene: IosWindowSceneModel? {
        activeToken.flatMap { scenes[$0] }
    }
    var mainScene: IosWindowSceneModel? {
        pageTokens.first.flatMap { scenes[$0] }
    }
    public var isRunning: Bool { started && !stopping }

    public init(bridgeFactory: @escaping
        (ATIosApplicationBridgeDelegate) -> ATIosApplicationBridge) {
        self.bridgeFactory = bridgeFactory
        super.init()
        let notifications = NotificationCenter.default
        notifications.addObserver(self, selector: #selector(applicationWillResignActive),
                                  name: UIApplication.willResignActiveNotification, object: nil)
        notifications.addObserver(self, selector: #selector(applicationDidEnterBackground),
                                  name: UIApplication.didEnterBackgroundNotification, object: nil)
        notifications.addObserver(self, selector: #selector(applicationDidBecomeActive),
                                  name: UIApplication.didBecomeActiveNotification, object: nil)
    }

    deinit { NotificationCenter.default.removeObserver(self) }

    @objc private func applicationWillResignActive() { sceneWillDeactivate() }
    @objc private func applicationDidEnterBackground() { sceneDidEnterBackground() }
    @objc private func applicationDidBecomeActive() { sceneDidBecomeActive() }

    public func startIfNeeded() {
        guard !started && !manualStop && applicationActive else { return }
        started = true
        stopping = false
        bridge = bridgeFactory(self)
        bridge.start()
    }
    public func lastModelExitCode() -> Int? { lastExitStatus }

    public func reportViewport(_ size: CGSize) {
        latestViewport = size
        guard started && !stopping else { return }
        bridge.submitViewportWidth(size.width, height: size.height)
    }
    public func swipe(_ delta: Int) {
        guard started && !stopping else { return }
        bridge.submitMobileSwipe(delta)
    }
    public func selectPage(_ token: UInt64) {
        guard started && !stopping else { return }
        bridge.submitMobilePage(token)
    }
    public func closeDetailFromUi() {
        guard let scene = activeScene, activeToken != pageTokens.first else { return }
        _ = bridge.requestCloseWindow(scene.id)
    }
    public func requestStop() {
        manualStop = true
        stopSession()
    }
    private func stopSession() {
        guard started && !stopping else { return }
        stopping = true
        bridge.requestStop()
    }
    public func sceneWillDeactivate() {
        // One save for this active-to-inactive transition. didEnterBackground
        // may follow the same deactivation without introducing another save.
        guard applicationActive else { return }
        applicationActive = false
        requestLiveSave()
    }
    private func requestLiveSave() {
        guard started && !stopping else { return }
        lastBackgroundSaveStatus = nil
        if pendingBackgroundSaves == 0 { backgroundSaveFailure = nil }
        pendingBackgroundSaves += 1
        if backgroundTask == .invalid {
            backgroundTask = UIApplication.shared.beginBackgroundTask(
                withName: "AppTraverse USER save") { [weak self] in
                    self?.endBackgroundTask()
                }
        }
        bridge.requestBackgroundSave()
    }
    public func sceneDidEnterBackground() { sceneWillDeactivate() }
    public func sceneDidBecomeActive() {
        applicationActive = true
        startIfNeeded()
    }
    private func endBackgroundTask() {
        guard backgroundTask != .invalid else { return }
        UIApplication.shared.endBackgroundTask(backgroundTask)
        backgroundTask = .invalid
    }
    public func requestBackgroundCheckpoint() { requestLiveSave() }
    public func lastBackgroundSaveExitCode() -> Int? { lastBackgroundSaveStatus }

    public func bridge(_ bridge: ATIosApplicationBridge, loadWindow token: UInt64) {
        let state = bridge.windowState(token)
        let scene = IosWindowSceneModel(token: token, identity: state.identity,
                                       commandSink: bridge)
        scene.sync(state)
        scenes[token] = scene
    }
    public func bridge(_ bridge: ATIosApplicationBridge, syncWindow token: UInt64) {
        scenes[token]!.sync(bridge.windowState(token))
    }
    public func bridge(_ bridge: ATIosApplicationBridge, unloadWindow token: UInt64) {
        scenes.removeValue(forKey: token)!.disconnect()
    }
    public func bridge(_ bridge: ATIosApplicationBridge,
                       applyMobilePages tokens: [NSNumber], activeToken token: UInt64) {
        pageTokens = tokens.map(\.uint64Value)
        activeToken = token
        if latestViewport.width > 0 && latestViewport.height > 0 && !stopping {
            bridge.submitViewportWidth(latestViewport.width, height: latestViewport.height)
        }
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
    public func bridgeModelDidFinish(_ bridge: ATIosApplicationBridge) {
        lastExitStatus = Int(bridge.finishTeardown())
        self.bridge = nil
        started = false
        stopping = false
        activeToken = nil
        pageTokens = []
        endBackgroundTask()
        precondition(lastExitStatus == 0, "AppTraverse iOS model session failed")
        if applicationActive && !manualStop { startIfNeeded() }
    }
    public func bridge(_ bridge: ATIosApplicationBridge,
                       backgroundSaveDidFinish result: NSInteger) {
        if result != 0 { backgroundSaveFailure = Int(result) }
        pendingBackgroundSaves -= 1
        precondition(pendingBackgroundSaves >= 0)
        if pendingBackgroundSaves == 0 {
            lastBackgroundSaveStatus = backgroundSaveFailure ?? 0
            endBackgroundTask()
            precondition(lastBackgroundSaveStatus == 0, "AppTraverse iOS USER save failed")
        }
    }
}
