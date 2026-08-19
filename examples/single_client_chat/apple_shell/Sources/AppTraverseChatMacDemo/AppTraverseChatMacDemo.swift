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
    #if APPTRAVERSE_APPLE_NATIVE
    return ChatViewModel(
        backend: AppleChatBackend.makeHostBackend(
            clientName: "apptraverse-macos",
            localClientName: "Mac"
        )
    )
    #else
    return ChatViewModel(backend: FakeChatBackend())
    #endif
}

private func runSmoke() {
    let model = makeProductModel()
    fputs("AppTraverseChatMacDemo smoke start\n", stdout)
    fflush(stdout)

    #if APPTRAVERSE_APPLE_NATIVE
    var remaining = 40
    while remaining > 0 && model.localUID.isEmpty {
        Thread.sleep(forTimeInterval: 0.5)
        remaining -= 1
    }
    #endif

    fputs("localUID=\(model.localUID)\n", stdout)
    fputs("timelineCount=\(model.timelineRows.count)\n", stdout)
    fflush(stdout)
    exit(0)
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
