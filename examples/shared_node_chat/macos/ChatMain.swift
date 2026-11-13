import AppKit
import SwiftUI

struct ChatView: View {
    @ObservedObject var presenter: ChatPresenter
    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack {
                Text(presenter.role == .host ? "My Aether ID" : "Host Aether ID")
                    .frame(width: 100, alignment: .leading)
                if presenter.role == .host {
                    Text(presenter.uid).textSelection(.enabled)
                        .frame(maxWidth: .infinity, alignment: .leading)
                    Button("Copy", action: presenter.copyID).accessibilityIdentifier("chat.copy")
                        .disabled(!presenter.ready)
                } else {
                    Group {
                        if presenter.inRoom {
                            Text(presenter.invite).textSelection(.enabled).lineLimit(1)
                                .frame(maxWidth: .infinity, alignment: .leading)
                        } else {
                            TextField("Host Aether ID", text: $presenter.invite).onSubmit(presenter.join)
                        }
                    }.accessibilityIdentifier("chat.invite")
                    Button("Join", action: presenter.join).accessibilityIdentifier("chat.join")
                        .disabled(!presenter.ready || presenter.inRoom)
                }
            }
            HStack(alignment: .top, spacing: 12) {
                VStack(alignment: .leading) {
                    Text("Participants").font(.headline)
                    List(Array(presenter.participants.enumerated()), id: \.offset) { _, value in Text(value) }
                        .accessibilityIdentifier("chat.participants")
                }.frame(width: 180)
                VStack(alignment: .leading) {
                    Text("Messages").font(.headline)
                    ScrollViewReader { reader in
                        ScrollView {
                            VStack(alignment: .leading, spacing: 6) {
                                ForEach(Array(presenter.messages.enumerated()), id: \.offset) { index, value in
                                    Text(value).textSelection(.enabled).frame(maxWidth: .infinity, alignment: .leading).id(index)
                                }
                            }.frame(maxWidth: .infinity, alignment: .leading).padding(8)
                        }.frame(maxWidth: .infinity, maxHeight: .infinity)
                            .background(Color(nsColor: .textBackgroundColor))
                            .accessibilityIdentifier("chat.messages")
                            .onChange(of: presenter.messages.count) { count in
                                if count > 0 { reader.scrollTo(count - 1, anchor: .bottom) }
                            }
                    }
                    HStack {
                        TextField("Message", text: $presenter.input).onSubmit(presenter.send)
                            .accessibilityIdentifier("chat.input").disabled(!presenter.ready || !presenter.inRoom)
                        Button("Send", action: presenter.send).accessibilityIdentifier("chat.send")
                            .disabled(!presenter.ready || !presenter.inRoom)
                    }
                }
            }
            Text(presenter.inputError.isEmpty ? presenter.status : presenter.inputError)
                .font(.caption).accessibilityIdentifier("chat.status")
        }.padding(12).frame(minWidth: 560, minHeight: 400)
    }
}

// AppKit owns only the native application/window lifecycle. All controls above
// are SwiftUI. No UI refresh timer; the bridge delivers changed publications.
final class ChatHost: NSObject, NSApplicationDelegate {
    let presenter: ChatPresenter
    let name: String
    var window: NSWindow!
    var content: NSHostingView<ChatView>!
    init(profile: String, name: String, role: ChatRole, initialHost: String) {
        self.name = name
        presenter = ChatPresenter(profile: profile, name: name, role: role, initialHost: initialHost)
    }
    func applicationDidFinishLaunching(_ notification: Notification) {
        window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 820, height: 520),
                          styleMask: [.titled, .closable, .miniaturizable, .resizable], backing: .buffered, defer: false)
        window.title = "SharedNode Chat — \(name)"; window.isReleasedWhenClosed = false
        content = NSHostingView(rootView: ChatView(presenter: presenter)); window.contentView = content
        window.center()
        #if CHAT_ACCEPTANCE
        window.orderBack(nil) // Keep the user's manual chat in front of tests.
        #else
        window.makeKeyAndOrderFront(nil); NSApp.activate(ignoringOtherApps: true)
        #endif
        guard presenter.start() else {
            fputs("Cannot open chat profile or it is already in use\n", stderr)
            presenter.stop(); exit(2)
        }
        #if CHAT_ACCEPTANCE
        installCommands()
        #endif
    }
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }
    func applicationWillTerminate(_ notification: Notification) { presenter.stop() }
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
        let menu = NSMenu(); let item = NSMenuItem(); menu.addItem(item)
        let applicationMenu = NSMenu()
        applicationMenu.addItem(withTitle: "Quit SharedNode Chat", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")
        item.submenu = applicationMenu
        let editItem = NSMenuItem(); menu.addItem(editItem)
        let editMenu = NSMenu(title: "Edit")
        for (title, action, key) in [("Cut", "cut:", "x"), ("Copy", "copy:", "c"),
                                     ("Paste", "paste:", "v"), ("Select All", "selectAll:", "a")] {
            editMenu.addItem(withTitle: title, action: NSSelectorFromString(action), keyEquivalent: key)
        }
        editItem.submenu = editMenu; app.mainMenu = menu
        let host = ChatHost(profile: profilePath, name: name, role: role, initialHost: initialHost); app.delegate = host
        app.run(); host.presenter.stop(); exit(0)
    }
}
