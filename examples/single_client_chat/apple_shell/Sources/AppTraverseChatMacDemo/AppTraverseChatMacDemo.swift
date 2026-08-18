import AppKit
import AppTraverseChatAppleUI
import Darwin
import SwiftUI

@main
enum AppTraverseChatMacDemo {
    static func main() {
        if CommandLine.arguments.contains("--smoke") {
            runSmoke()
            return
        }
        AppTraverseChatMacDemoApp.main()
    }
}

private func runSmoke() {
    let model = ChatViewModel()
    fputs("localUID=\(model.localUID)\n", stdout)
    guard model.timelineRows == ["* Apple joined"] else {
        fputs("SMOKE FAIL initial transcript: \(model.timelineRows)\n", stderr)
        exit(1)
    }

    model.remoteUID = "PEER-UID"
    model.addPeer()
    model.draft = "hello from apple shell"
    model.send()

    let expected = [
        "* Apple joined",
        "* Peer added: PEER-UID",
        "Apple: hello from apple shell",
    ]
    guard model.timelineRows == expected else {
        fputs("SMOKE FAIL rows=\(model.timelineRows)\n", stderr)
        exit(1)
    }

    fputs("SMOKE PASS\n", stdout)
    fflush(stdout)
}

struct AppTraverseChatMacDemoApp: App {
    init() {
        NSApplication.shared.setActivationPolicy(.regular)
    }

    var body: some Scene {
        WindowGroup("App Traverse Chat") {
            ChatView()
                .onAppear {
                    NSApplication.shared.activate(ignoringOtherApps: true)
                    fputs("AppTraverseChatMacDemo window opened\n", stdout)
                    fflush(stdout)
                }
        }
    }
}
