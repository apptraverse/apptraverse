import AppKit
import SwiftUI

struct ChatView: View {
    @ObservedObject var presenter: ChatPresenter
    var body: some View { ChatPanel(presenter: presenter) }
}

// AppKit owns only the native application/window lifecycle. All controls above
// are SwiftUI. No UI refresh timer; the bridge delivers changed publications.
final class ChatHost: NSObject, NSApplicationDelegate, NSWindowDelegate {
    let presenter: ChatPresenter
    private var nativeSizeApplied = false
    private var menuLabels: [String] = []
    var window: NSWindow!
    var content: NSHostingView<ChatView>!
    init(profile: String, name: String, role: ChatRole, initialHost: String) {
        presenter = ChatPresenter(profile: profile, name: name, role: role, initialHost: initialHost)
        super.init()
        presenter.chromeChanged = { [weak self] in
            guard let self, let window = self.window else { return }
            window.title = self.presenter.title
            self.installMenu()
            window.contentMinSize = NSSize(width: self.presenter.minimumWidth, height: self.presenter.minimumHeight)
            if !self.nativeSizeApplied {
                window.setContentSize(NSSize(width: self.presenter.restoreWidth, height: self.presenter.restoreHeight))
                if self.presenter.hasExplicitPlacement {
                    let outer = window.frameRect(forContentRect: CGRect(origin: .zero,
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
    func applicationDidFinishLaunching(_ notification: Notification) {
        window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 1, height: 1),
                          styleMask: [.titled, .closable, .miniaturizable, .resizable], backing: .buffered, defer: false)
        window.isReleasedWhenClosed = false
        window.delegate = self
        content = NSHostingView(rootView: ChatView(presenter: presenter)); window.contentView = content
        guard presenter.start() else {
            fputs("Cannot open chat profile or it is already in use\n", stderr)
            presenter.stop(); exit(2)
        }
        #if CHAT_ACCEPTANCE
        installCommands()
        #endif
    }
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }
    func applicationWillTerminate(_ notification: Notification) { reportFrame(); presenter.stop() }
    func windowDidResize(_ notification: Notification) { reportFrame() }
    func windowDidMove(_ notification: Notification) { reportFrame() }
    func windowWillClose(_ notification: Notification) { reportFrame() }
    private func reportFrame() {
        guard nativeSizeApplied, let window, !window.isMiniaturized else { return }
        let client = window.contentRect(forFrameRect: window.frame)
        presenter.reportFrame(CGRect(x: window.frame.minX,
            y: NSScreen.screens[0].frame.maxY - window.frame.maxY,
            width: client.width, height: client.height),
            updatesRestore: !window.isZoomed && !window.styleMask.contains(.fullScreen))
    }
    private func installMenu() {
        let labels = presenter.labels
        guard labels != menuLabels else { return }
        menuLabels = labels
        let menu = NSMenu(); let item = NSMenuItem(); menu.addItem(item)
        let applicationMenu = NSMenu()
        applicationMenu.addItem(withTitle: labels[18], action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")
        item.submenu = applicationMenu
        let editItem = NSMenuItem(); menu.addItem(editItem)
        let editMenu = NSMenu(title: labels[19])
        for (title, action, key) in [(labels[20], "cut:", "x"), (labels[3], "copy:", "c"),
                                     (labels[21], "paste:", "v"), (labels[22], "selectAll:", "a")] {
            editMenu.addItem(withTitle: title, action: NSSelectorFromString(action), keyEquivalent: key)
        }
        editItem.submenu = editMenu; NSApp.mainMenu = menu
    }
    #if CHAT_ACCEPTANCE
    var commandSource: DispatchSourceRead?
    var pending = Data()
    #endif
}

@main
enum ChatMain {
    static func main() {
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
        let profilePath = profile ?? FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("AppTraverse/SharedNodeChat/" + (role == .host ? "host" : "client")).path
        let app = NSApplication.shared; app.setActivationPolicy(.regular)
        let host = ChatHost(profile: profilePath, name: name, role: role, initialHost: initialHost); app.delegate = host
        app.run(); host.presenter.stop(); exit(0)
    }
}
