import AppKit
import SwiftUI

// Native hooks for geometry, APNs and acceptance. SwiftUI owns the application,
// its window and hosting hierarchy; never replace SwiftUI's window delegate.
final class ChatHost: NSObject, NSApplicationDelegate {
    let presenter: ChatPresenter
    private lazy var push = ChatPush(register: { [weak self] in self?.presenter.registerApplePushToken($0) },
                                    open: { [weak self] in
        self?.window.makeKeyAndOrderFront(nil); NSApp.activate(ignoringOtherApps: true)
    })
    func application(_ application: NSApplication, didRegisterForRemoteNotificationsWithDeviceToken token: Data) { push.token(token) }
    func application(_ application: NSApplication, didFailToRegisterForRemoteNotificationsWithError error: Error) {
        NSLog("APNs registration failed: %@", error.localizedDescription)
    }
    func applicationDidBecomeActive(_ notification: Notification) { push.active() }
    private var nativeSizeApplied = false
    private var appearanceObservation: NSKeyValueObservation?
    weak var window: NSWindow!
    weak var content: NSView!
    override init() {
        let options = ChatLaunchOptions()
        presenter = ChatPresenter(profile: options.profilePath, name: options.name, role: options.role, initialHost: options.initialHost)
        super.init()
        presenter.chromeChanged = { [weak self] in
            guard let self, let window = self.window else { return }
            window.title = self.presenter.title
            if !self.nativeSizeApplied {
                if !self.presenter.restoreOuter {
                    window.setContentSize(NSSize(width: self.presenter.restoreWidth, height: self.presenter.restoreHeight))
                }
                if self.presenter.hasExplicitPlacement {
                    let outer = self.presenter.restoreOuter
                        ? CGRect(origin: .zero, size: CGSize(width: self.presenter.restoreWidth, height: self.presenter.restoreHeight))
                        : window.frameRect(forContentRect: CGRect(origin: .zero,
                            size: CGSize(width: self.presenter.restoreWidth, height: self.presenter.restoreHeight)))
                    window.setFrame(CGRect(x: self.presenter.x,
                        y: NSScreen.screens[0].frame.maxY - self.presenter.y - outer.height,
                        width: outer.width, height: outer.height), display: false)
                } else {
                    window.cascadeTopLeft(from: .zero)
                }
                self.nativeSizeApplied = true
                self.reportFrame()
                #if CHAT_ACCEPTANCE
                window.orderBack(nil)
                #else
                window.makeKeyAndOrderFront(nil); NSApp.activate(ignoringOtherApps: true)
                #endif
            }
        }
    }
    private var attached = false
    func attach(_ window: NSWindow) {
        guard !attached else { return }
        attached = true; self.window = window; content = window.contentView
        for name in [NSWindow.didResizeNotification, NSWindow.didMoveNotification, NSWindow.willCloseNotification, NSWindow.didEndLiveResizeNotification, NSWindow.didChangeBackingPropertiesNotification] {
            NotificationCenter.default.addObserver(self, selector: #selector(frameChanged(_:)), name: name, object: window)
        }
        NotificationCenter.default.addObserver(self, selector: #selector(environmentChanged(_:)), name: NSApplication.didChangeScreenParametersNotification, object: nil)
        NSWorkspace.shared.notificationCenter.addObserver(self, selector: #selector(environmentChanged(_:)), name: NSWorkspace.accessibilityDisplayOptionsDidChangeNotification, object: nil)
        appearanceObservation = NSApp.observe(\.effectiveAppearance, options: [.new]) { [weak self] _, _ in
            DispatchQueue.main.async { self?.presenter.observeEnvironment() }
        }
        guard presenter.start() else {
            fputs("Cannot open chat profile or it is already in use\n", stderr)
            presenter.stop(); exit(2)
        }
        push.start()
        #if CHAT_ACCEPTANCE
        installCommands()
        #endif
    }
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }
    func applicationWillTerminate(_ notification: Notification) {
        reportFrame(); presenter.stop()
        #if CHAT_ACCEPTANCE
        if let activity = acceptanceActivity { ProcessInfo.processInfo.endActivity(activity) }
        acceptanceActivity = nil
        #endif
    }
    @objc private func environmentChanged(_ notification: Notification) { presenter.observeEnvironment() }
    @objc private func frameChanged(_ notification: Notification) {
        if notification.name == NSWindow.didChangeBackingPropertiesNotification { presenter.observeEnvironment() }
        let dragged = NSApp.currentEvent?.type == .leftMouseDragged || NSApp.currentEvent?.type == .leftMouseUp
        let userChange = notification.name == NSWindow.didEndLiveResizeNotification || (notification.name == NSWindow.didMoveNotification && dragged)
        reportFrame(userChange: userChange)
    }
    private func reportFrame(userChange: Bool = false) {
        guard nativeSizeApplied, let window, !window.isMiniaturized else { return }
        let client = window.contentRect(forFrameRect: window.frame)
        presenter.reportFrame(CGRect(x: window.frame.minX,
            y: NSScreen.screens[0].frame.maxY - window.frame.maxY,
            width: client.width, height: client.height),
            updatesRestore: false)
        if !window.isZoomed && !window.styleMask.contains(.fullScreen), let screen = window.screen {
            let work = screen.visibleFrame, outer = window.frame
            let monitor = (screen.deviceDescription[NSDeviceDescriptionKey("NSScreenNumber")] as! NSNumber).stringValue
            presenter.reportLocalWindow(monitor,
                frame: CGRect(x: outer.minX - work.minX, y: work.maxY - outer.maxY, width: outer.width, height: outer.height),
                scale: screen.backingScaleFactor, userChange: userChange)
        }
    }
    #if CHAT_ACCEPTANCE
    var acceptanceActivity: NSObjectProtocol?
    var commandSource: DispatchSourceRead?
    var pending = Data()
    #endif
}

struct ChatLaunchOptions {
    var name: String
    var profilePath: String
    var role: ChatRole
    var initialHost: String
    init() {
        name = "Mac"
        var profile: String?
        role = .client
        var roleSelected = false
        initialHost = ""
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
        profilePath = profile ?? FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("AppTraverse/SharedNodeChat/" + (role == .host ? "host" : "client")).path
    }
}

struct ChatWindowAttachment: NSViewRepresentable {
    let host: ChatHost
    final class View: NSView {
        var attached: ((NSWindow) -> Void)?
        override func viewDidMoveToWindow() {
            super.viewDidMoveToWindow()
            if let window { DispatchQueue.main.async { [weak self] in self?.attached?(window) } }
        }
    }
    func makeNSView(context: Context) -> View {
        let view = View(); view.attached = { [weak host] in host?.attach($0) }; return view
    }
    func updateNSView(_ view: View, context: Context) {}
}
