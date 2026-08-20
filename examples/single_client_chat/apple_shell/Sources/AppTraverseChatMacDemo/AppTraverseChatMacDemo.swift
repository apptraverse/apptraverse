#if canImport(AppTraverseChatAppleUI)
import AppTraverseChatAppleUI
#endif
import AppKit
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

private func makeProductModel() -> ChatViewModel {
    ChatViewModel(
        backend: AppleChatBackend.makeHostBackend(
            clientName: "apptraverse-macos",
            localClientName: "Mac"
        )
    )
}

private func runSmoke() {
    let marker = "apple-product-\(Int(Date().timeIntervalSince1970))"
    var uid = ""

    do {
        let model = makeProductModel()
        fputs("AppTraverseChatMacDemo smoke start\n", stdout)
        fflush(stdout)

        var remaining = 240
        while remaining > 0 && model.localUID.isEmpty {
            pumpMainRunLoop(0.5)
            remaining -= 1
        }

        uid = model.localUID
        fputs("localUID=\(uid)\n", stdout)
        fputs("timelineCount=\(model.timelineRows.count)\n", stdout)
        fflush(stdout)
        if uid.isEmpty || uid == "APPLE-LOCAL" {
            fputs("SMOKE FAIL: real Aether UID was not published\n", stderr)
            exit(1)
        }

        model.draft = marker
        model.send()
        remaining = 40
        while remaining > 0 && !model.timelineRows.contains(where: { $0.contains(marker) }) {
            pumpMainRunLoop(0.5)
            remaining -= 1
        }
        fputs("sendVisible=\(model.timelineRows.contains(where: { $0.contains(marker) }))\n", stdout)
        fflush(stdout)
        if !model.timelineRows.contains(where: { $0.contains(marker) }) {
            fputs("SMOKE FAIL: local send did not appear\n", stderr)
            exit(1)
        }
    }

    pumpMainRunLoop(1.0)

    do {
        let model = makeProductModel()
        var remaining = 240
        while remaining > 0 && model.localUID.isEmpty {
            pumpMainRunLoop(0.5)
            remaining -= 1
        }
        remaining = 40
        while remaining > 0 && !model.timelineRows.contains(where: { $0.contains(marker) }) {
            pumpMainRunLoop(0.5)
            remaining -= 1
        }
        fputs("relaunchUID=\(model.localUID)\n", stdout)
        fputs("relaunchRestored=\(model.timelineRows.contains(where: { $0.contains(marker) }))\n", stdout)
        fflush(stdout)
        if model.localUID != uid {
            fputs("SMOKE FAIL: UID changed after relaunch\n", stderr)
            exit(1)
        }
        if !model.timelineRows.contains(where: { $0.contains(marker) }) {
            fputs("SMOKE FAIL: history was not restored\n", stderr)
            exit(1)
        }
    }

    fputs("SMOKE PASS\n", stdout)
    fflush(stdout)
}

private func pumpMainRunLoop(_ seconds: TimeInterval) {
    RunLoop.current.run(until: Date(timeIntervalSinceNow: seconds))
}

struct AppTraverseChatMacDemoApp: App {
    @StateObject private var model: ChatViewModel

    init() {
        NSApplication.shared.setActivationPolicy(.regular)
        _model = StateObject(wrappedValue: makeProductModel())
    }

    var body: some Scene {
        WindowGroup("AppTraverse Chat") {
            ChatView(model: model)
                .onAppear {
                    NSApplication.shared.activate(ignoringOtherApps: true)
                    fputs("AppTraverseChatMacDemo window opened\n", stdout)
                    fflush(stdout)
                }
        }
    }
}
