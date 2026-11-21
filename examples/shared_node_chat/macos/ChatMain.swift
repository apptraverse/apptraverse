import AppKit
import SwiftUI

struct ChatView: View {
    @ObservedObject var presenter: ChatPresenter
    var body: some View {
        ChatPanel(presenter: presenter)
            .frame(minWidth: 400, minHeight: 300)
            .navigationTitle(presenter.title)
    }
}

// SwiftUI owns application, scene, content and commands. The delegate forwards
// native APNs and termination callbacks that the App protocol does not expose.
final class ChatApplicationDelegate: NSObject, NSApplicationDelegate {
    weak var host: ChatHost?
    func applicationDidFinishLaunching(_ notification: Notification) {
        NSApp.setActivationPolicy(.regular)
    }
    func application(_ application: NSApplication, didRegisterForRemoteNotificationsWithDeviceToken token: Data) {
        host?.push.token(token)
    }
    func application(_ application: NSApplication, didFailToRegisterForRemoteNotificationsWithError error: Error) {
        NSLog("APNs registration failed: %@", error.localizedDescription)
    }
    func applicationDidBecomeActive(_ notification: Notification) { host?.push.active() }
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }
    func applicationWillTerminate(_ notification: Notification) { host?.stop() }
}

final class ChatHost: ObservableObject {
    let presenter: ChatPresenter
    let native: ChatNativeWindow
    lazy var push = ChatPush(register: { [weak self] in self?.presenter.registerApplePushToken($0) },
                             open: { [weak self] in self?.native.raise() })
    private var startRequested = false
    private var started = false
    // Acceptance inspects SwiftUI's actual content hierarchy, never a second view.
    var window: NSWindow? { native.window }
    var content: NSView? { window?.contentView }

    init() {
        var name = "Mac"
        var profile: String?
        var role: ChatRole = .client
        var roleSelected = false
        var initialHost = ""
        var index = 1
        let arguments = CommandLine.arguments
        while index < arguments.count {
            switch arguments[index] {
            case "--host", "--client":
                guard !roleSelected else { exit(2) }
                roleSelected = true; role = arguments[index] == "--host" ? .host : .client
                index += 1
                if role == .client && index < arguments.count && !arguments[index].hasPrefix("--") {
                    initialHost = arguments[index]; index += 1
                }
            case "--load-only": index += 1
            case "--profile", "--name":
                guard index + 1 < arguments.count else { exit(2) }
                if arguments[index] == "--profile" { profile = arguments[index + 1] }
                else { name = arguments[index + 1] }
                index += 2
            default: exit(2)
            }
        }
        let path = profile ?? FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("AppTraverse/SharedNodeChat/" + (role == .host ? "host" : "client")).path
        presenter = ChatPresenter(profile: path, name: name, role: role, initialHost: initialHost)
        native = ChatNativeWindow(presenter: presenter)
        presenter.chromeChanged = { [weak self] in self?.native.applyPublication() }
    }

    func mount() { startRequested = true; startIfAttached() }
    func attach(_ window: NSWindow) { native.attach(window); startIfAttached() }
    private func startIfAttached() {
        guard startRequested, !started, native.window != nil else { return }
        started = true
        #if CHAT_ACCEPTANCE
        installCommands()
        #endif
        guard presenter.start() else {
            fputs("Cannot open chat profile or it is already in use\n", stderr)
            stop(); exit(2)
        }
        push.start()
    }
    func stop() {
        native.report(commit: false)
        presenter.stop()
        #if CHAT_ACCEPTANCE
        commandSource?.cancel(); commandSource = nil
        if let activity = acceptanceActivity { ProcessInfo.processInfo.endActivity(activity) }
        acceptanceActivity = nil
        #endif
    }
    deinit { presenter.stop() }
    #if CHAT_ACCEPTANCE
    var acceptanceActivity: NSObjectProtocol?
    var commandSource: DispatchSourceRead?
    var pending = Data()
    #endif
}

// Only native outer geometry and focus cross this boundary. SwiftUI retains
// ownership of its window delegate, controls, layout and menus.
final class ChatNativeWindow {
    private let presenter: ChatPresenter
    private(set) weak var window: NSWindow?
    private var observers: [(NotificationCenter, NSObjectProtocol)] = []
    private var appearanceObservation: NSKeyValueObservation?
    private var applying = false
    private var placed = false
    private var moveStart: NSRect?
    private var resizeStart: NSRect?
    private var mouseUpMonitor: Any?
    private var fullscreenTransition = false

    init(presenter: ChatPresenter) { self.presenter = presenter }
    func attach(_ window: NSWindow) {
        guard self.window !== window else { return }
        for (center, observer) in observers { center.removeObserver(observer) }
        observers.removeAll()
        if let monitor = mouseUpMonitor { NSEvent.removeMonitor(monitor) }
        mouseUpMonitor = nil
        moveStart = nil; resizeStart = nil
        placed = false
        presenter.resetWindowPlacement()
        self.window = window
        window.tabbingMode = .disallowed
        observe(NSWindow.willCloseNotification, window) { $0.report(commit: false) }
        observe(NSWindow.willMoveNotification, window) { boundary in
            guard NSEvent.pressedMouseButtons & 1 != 0 else { return }
            boundary.beginMove()
        }
        observe(NSWindow.didMoveNotification, window) { boundary in
            boundary.report(commit: false)
            if NSEvent.pressedMouseButtons & 1 == 0 { boundary.finishMove() }
        }
        observe(NSWindow.willStartLiveResizeNotification, window) { $0.resizeStart = $0.window?.frame }
        observe(NSWindow.didResizeNotification, window) { boundary in
            if boundary.window?.inLiveResize == false { boundary.report(commit: false) }
        }
        observe(NSWindow.didEndLiveResizeNotification, window) { boundary in
            let changed = boundary.resizeStart.map { $0 != boundary.window?.frame } == true
            boundary.resizeStart = nil
            boundary.finishInteraction(commit: changed)
        }
        observe(NSWindow.didDeminiaturizeNotification, window) { $0.finishInteraction(commit: false) }
        for name in [NSWindow.willEnterFullScreenNotification, NSWindow.willExitFullScreenNotification] {
            observe(name, window) { $0.fullscreenTransition = true }
        }
        for name in [NSWindow.didEnterFullScreenNotification, NSWindow.didExitFullScreenNotification] {
            observe(name, window) { boundary in
                boundary.fullscreenTransition = false
                boundary.presenter.resetWindowPlacement()
                boundary.environmentChanged()
                boundary.finishInteraction(commit: false)
            }
        }
        for name in [NSWindow.didChangeScreenNotification, NSWindow.didChangeBackingPropertiesNotification] {
            observe(name, window) { boundary in boundary.environmentChanged(); boundary.report(commit: false) }
        }
        for name in [NSApplication.didChangeScreenParametersNotification, NSApplication.didBecomeActiveNotification] {
            observeEnvironment(NotificationCenter.default, name)
        }
        observeEnvironment(NSWorkspace.shared.notificationCenter, NSWorkspace.accessibilityDisplayOptionsDidChangeNotification)
        appearanceObservation = NSApp.observe(\.effectiveAppearance, options: [.new]) { [weak self] _, _ in
            DispatchQueue.main.async { self?.environmentChanged() }
        }
        applyPublication()
    }

    func applyPublication() {
        guard let window, !presenter.controls.isEmpty, !applying, moveStart == nil,
              !window.inLiveResize, !window.isMiniaturized, !fullscreenTransition,
              !window.styleMask.contains(.fullScreen) else { return }
        applying = true
        let local = presenter.applyWindowPlacement(window)
        if !placed && !local {
            window.setContentSize(NSSize(width: presenter.restoreWidth, height: presenter.restoreHeight))
            if presenter.hasExplicitPlacement {
                let outer = window.frame
                window.setFrameOrigin(NSPoint(x: presenter.x,
                    y: NSScreen.screens[0].frame.maxY - presenter.y - outer.height))
            }
        }
        let first = !placed
        placed = true
        applying = false
        report(commit: false)
        if first {
            #if CHAT_ACCEPTANCE
            window.orderBack(nil)
            #else
            raise()
            #endif
        }
    }
    func report(commit: Bool) {
        guard placed, !applying, !fullscreenTransition, let window else { return }
        presenter.observeWindow(window, commit: commit)
    }
    func raise() {
        guard let window else { return }
        if window.isMiniaturized { window.deminiaturize(nil) }
        window.makeKeyAndOrderFront(nil); NSApp.activate(ignoringOtherApps: true)
    }
    private func environmentChanged() { presenter.observeEnvironment() }
    private func finishInteraction(commit: Bool) {
        if commit { report(commit: true) }
        else { applyPublication(); report(commit: false) }
    }
    private func beginMove() {
        guard !applying, moveStart == nil, let window else { return }
        moveStart = window.frame
        mouseUpMonitor = NSEvent.addLocalMonitorForEvents(matching: .leftMouseUp) { [weak self] event in
            DispatchQueue.main.async { self?.finishMove() }
            return event
        }
        RunLoop.main.perform(inModes: [.default]) { [weak self] in
            if NSEvent.pressedMouseButtons & 1 == 0 { self?.finishMove() }
        }
    }
    private func finishMove() {
        guard let start = moveStart, let window else { return }
        moveStart = nil
        if let monitor = mouseUpMonitor { NSEvent.removeMonitor(monitor) }
        mouseUpMonitor = nil
        finishInteraction(commit: start != window.frame)
    }
    private func observe(_ name: Notification.Name, _ window: NSWindow,
                         _ action: @escaping (ChatNativeWindow) -> Void) {
        let center = NotificationCenter.default
        observers.append((center, center.addObserver(forName: name, object: window, queue: .main) { [weak self] _ in
            if let self { action(self) }
        }))
    }
    private func observeEnvironment(_ center: NotificationCenter, _ name: Notification.Name) {
        observers.append((center, center.addObserver(forName: name, object: nil, queue: .main) { [weak self] _ in
            self?.environmentChanged()
        }))
    }
    deinit {
        if let monitor = mouseUpMonitor { NSEvent.removeMonitor(monitor) }
        for (center, observer) in observers { center.removeObserver(observer) }
    }
}

private struct ChatWindowAttachment: NSViewRepresentable {
    let host: ChatHost
    func makeNSView(context: Context) -> Attachment { Attachment(host: host) }
    func updateNSView(_ view: Attachment, context: Context) {}
    final class Attachment: NSView {
        private weak var host: ChatHost?
        init(host: ChatHost) { self.host = host; super.init(frame: .zero) }
        required init?(coder: NSCoder) { fatalError("Window attachments are not decoded") }
        override func hitTest(_ point: NSPoint) -> NSView? { nil }
        override func viewDidMoveToWindow() {
            super.viewDidMoveToWindow()
            DispatchQueue.main.async { [weak self] in
                guard let self, let window = self.window else { return }
                self.host?.attach(window)
            }
        }
    }
}

private struct ChatCommands: Commands {
    @ObservedObject var presenter: ChatPresenter
    var body: some Commands {
        CommandGroup(replacing: .appTermination) {
            if !presenter.labels.isEmpty {
                Button(presenter.labels[18]) { NSApp.terminate(nil) }
                    .keyboardShortcut("q", modifiers: .command)
            }
        }
        if !presenter.labels.isEmpty {
            CommandMenu(presenter.labels[19]) {
                Button(presenter.labels[20]) { NSApp.sendAction(#selector(NSText.cut(_:)), to: nil, from: nil) }
                    .keyboardShortcut("x", modifiers: .command)
                Button(presenter.labels[3]) { NSApp.sendAction(#selector(NSText.copy(_:)), to: nil, from: nil) }
                    .keyboardShortcut("c", modifiers: .command)
                Button(presenter.labels[21]) { NSApp.sendAction(#selector(NSText.paste(_:)), to: nil, from: nil) }
                    .keyboardShortcut("v", modifiers: .command)
                Button(presenter.labels[22]) { NSApp.sendAction(#selector(NSText.selectAll(_:)), to: nil, from: nil) }
                    .keyboardShortcut("a", modifiers: .command)
            }
        }
    }
}

private struct ChatScene: Scene {
    let host: ChatHost
    @ObservedObject var presenter: ChatPresenter
    let mount: () -> Void
    var body: some Scene {
        Window(Text(presenter.title), id: "shared-node-chat") {
            ChatView(presenter: presenter)
                .background(ChatWindowAttachment(host: host).frame(width: 0, height: 0))
                .onAppear(perform: mount)
        }
        .defaultSize(width: 840, height: 560)
        .windowResizability(.contentMinSize)
        .commandsReplaced { ChatCommands(presenter: presenter) }
    }
}

@main
struct ChatMain: App {
    @NSApplicationDelegateAdaptor(ChatApplicationDelegate.self) private var delegate
    @StateObject private var host = ChatHost()
    var body: some Scene {
        ChatScene(host: host, presenter: host.presenter) {
            delegate.host = host
            host.mount()
        }
    }
}
