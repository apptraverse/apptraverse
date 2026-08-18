import AppTraverseChatAppleUI
import SwiftUI

@main
struct AppTraverseChatIosDemoApp: App {
    @StateObject private var model = ChatViewModel(backend: FakeChatBackend())

    var body: some Scene {
        WindowGroup {
            ChatView(model: model)
        }
    }
}
