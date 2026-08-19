import SwiftUI

@main
struct AppTraverseChatIosDemoApp: App {
    @StateObject private var model: ChatViewModel

    init() {
        #if APPTRAVERSE_APPLE_NATIVE
        _model = StateObject(
            wrappedValue: ChatViewModel(
                backend: AppleChatBackend.makeHostBackend(
                    clientName: "apptraverse-ios",
                    localClientName: "iOS"
                )
            )
        )
        #else
        _model = StateObject(wrappedValue: ChatViewModel(backend: FakeChatBackend()))
        #endif
    }

    var body: some Scene {
        WindowGroup {
            ChatView(model: model)
        }
    }
}
